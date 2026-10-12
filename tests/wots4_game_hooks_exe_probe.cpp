// Checks the ModLoader's hook sites against a copy of the decrypted game exe,
// mapped into this process's memory. Only the copy is patched; no game code runs.
// Usage: wots4_game_hooks_exe_probe <WayOfTheSamurai4.unpacked.exe>
#include "wots4_game_hooks.cpp"
#include <cstdio>
#include <vector>

using namespace wots4;
using namespace wots4::game;

namespace {

unsigned tests, failures;

void check(const char* name, bool ok) {
    ++tests;
    if (!ok) ++failures;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
}

std::vector<uint8_t> readFile(const wchar_t* path) {
    std::vector<uint8_t> bytes;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"rb") || !file) return bytes;
    fseek(file, 0, SEEK_END);
    bytes.resize(ftell(file));
    rewind(file);
    fread(bytes.data(), 1, bytes.size(), file);
    fclose(file);
    return bytes;
}

// Copies the headers and sections to where the loader would put them. No relocations.
uint8_t* mapImage(const std::vector<uint8_t>& file, size_t& imageSize) {
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    imageSize = nt->OptionalHeader.SizeOfImage;
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!image) return nullptr;
    memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(image + sections[i].VirtualAddress, file.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
    return image;
}

void WINAPI readNothing(void*, uint32_t*, uint32_t) {}
void WINAPI ignoreChange(void*, uint32_t, uint32_t) {}
void WINAPI ignoreEvent(void*) {}

void registerOnePageAndEvents() {
    const auto mod = reinterpret_cast<HMODULE>(0x10000);
    const wchar_t* const onOff[] = {L"On", L"Off"};
    const OptionItem item{optionToggle, L"Toggle", onOff, 2};
    const OptionsPageCallbacks callbacks{nullptr, readNothing, ignoreChange};
    hostRegistry.registrationOpen = true;
    check("page accepted on the supported exe", addOptionsPage(mod, L"Page", &item, 1, &callbacks));
    check("onFrame accepted", onFrame(mod, ignoreEvent, nullptr));
    check("onOptionsSaved accepted", onOptionsSaved(mod, ignoreEvent, nullptr));
    hostRegistry.registrationOpen = false;
}

template <size_t N>
bool sitesReserved(const HookSite (&sites)[N]) {
    for (const HookSite& site : sites) {
        bool found = false;
        for (const HookClaim& claim : hookClaims) {
            found |= claim.owner == hookOwner && claim.address == gameAddress(site.address) && claim.length == site.length;
        }
        if (!found) return false;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        puts("usage: wots4_game_hooks_exe_probe <WayOfTheSamurai4.unpacked.exe>");
        return 2;
    }
    std::vector<uint8_t> file = readFile(argv[1]);
    size_t imageSize = 0;
    uint8_t* image = file.empty() ? nullptr : mapImage(file, imageSize);
    if (!image) {
        puts("could not read or map the exe copy");
        return 2;
    }
    const std::vector<uint8_t> original(image, image + imageSize);
    const auto owner = GetModuleHandleW(nullptr);

    prepareGameHooks(reinterpret_cast<HMODULE>(image), owner);
    check("the exe copy is the supported version, every site matches",
        hostRegistry.supported.options && hostRegistry.supported.frame && hostRegistry.supported.optionsSaved);
    check("every site is reserved for the ModLoader",
        sitesReserved(optionsMenuSites) && sitesReserved(frameSites) && sitesReserved(optionsSavedSites));
    check("a Mod cannot reserve the pause menu builder",
        !claimHook(reinterpret_cast<HMODULE>(0x10000), gameAddress(0x676cb0), 32));
    check("a Mod cannot reserve the frame update", !claimHook(reinterpret_cast<HMODULE>(0x10000), gameAddress(0x50e870), 1));

    registerOnePageAndEvents();
    installGameHooks();
    bool allEnabled = true;
    for (const HookSite& site : optionsMenuSites)
        allEnabled &= MH_EnableHook(reinterpret_cast<void*>(gameAddress(site.address))) == MH_ERROR_ENABLED;
    allEnabled &= MH_EnableHook(reinterpret_cast<void*>(gameAddress(frameSites[0].address))) == MH_ERROR_ENABLED;
    allEnabled &= MH_EnableHook(reinterpret_cast<void*>(gameAddress(optionsSavedSites[0].address))) == MH_ERROR_ENABLED;
    check("MinHook installed all 13 hooks on the copy", allEnabled);
    check("the Mod page methods start from the game's page vtable",
        modPageMethods[0] == *reinterpret_cast<uintptr_t*>(gameAddress(optionsPageMethods)) &&
        modPageMethods[buildItemsSlot / 4] == reinterpret_cast<uintptr_t>(buildItemsMethod));

    check("hooks removed", MH_DisableHook(MH_ALL_HOOKS) == MH_OK && MH_Uninitialize() == MH_OK);
    check("every byte of the copy restored", !memcmp(image, original.data(), imageSize));

    uint8_t* wrongVersion = image;
    reinterpret_cast<IMAGE_NT_HEADERS32*>(wrongVersion + reinterpret_cast<IMAGE_DOS_HEADER*>(wrongVersion)->e_lfanew)
        ->FileHeader.TimeDateStamp ^= 1;
    hookClaims.clear();
    prepareGameHooks(reinterpret_cast<HMODULE>(wrongVersion), owner);
    check("another exe version is refused without reserving anything",
        !hostRegistry.supported.options && !hostRegistry.supported.frame && hookClaims.empty());

    VirtualFree(image, 0, MEM_RELEASE);
    printf("RESULT %u passed, %u failed (a mapped copy of the exe only; no game process, no game function called)\n",
        tests - failures, failures);
    return failures ? 1 : 0;
}
