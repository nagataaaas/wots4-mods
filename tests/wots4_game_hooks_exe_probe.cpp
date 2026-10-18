// Checks the ModLoader's hook sites against a copy of the decrypted game exe,
// mapped into this process's memory. Only the copy is patched; no game code runs.
// Usage: wots4_game_hooks_exe_probe <WayOfTheSamurai4.unpacked.exe>
#include "wots4_game_hooks.cpp"
#include "wots4_game_conversation.cpp"
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

// Moves the absolute addresses in the code to where the copy is, as Windows does with ASLR.
void applyRelocations(uint8_t* image, const IMAGE_NT_HEADERS32& nt) {
    const IMAGE_DATA_DIRECTORY relocations = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const auto delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image) - nt.OptionalHeader.ImageBase);
    for (uint32_t offset = relocations.VirtualAddress; offset < relocations.VirtualAddress + relocations.Size;) {
        auto block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image + offset);
        if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) return;
        auto entries = reinterpret_cast<const WORD*>(block + 1);
        const uint32_t entryCount = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        for (uint32_t i = 0; i < entryCount; ++i) {
            if ((entries[i] >> 12) == IMAGE_REL_BASED_HIGHLOW)
                *reinterpret_cast<uint32_t*>(image + block->VirtualAddress + (entries[i] & 0xfff)) += delta;
        }
        offset += block->SizeOfBlock;
    }
}

// Copies the headers and sections to where the loader would put them, and relocates them.
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
    applyRelocations(image, *nt);
    return image;
}

void WINAPI readNothing(void*, uint32_t*, uint32_t) {}
void WINAPI ignoreChange(void*, uint32_t, uint32_t) {}
void WINAPI ignoreEvent(void*) {}

ConversationReply WINAPI endTalk(void*, const ConversationContext*, ConversationSession, uint32_t) {
    return conversationEnd;
}

void registerOnePageAndEvents() {
    const auto mod = reinterpret_cast<HMODULE>(0x10000);
    const LocalizedText onOff[] = {localizedGameText(211), localizedGameText(212)};
    const OptionItem item{optionToggle, {L"切り替え", L"Toggle"}, onOff, 2};
    const OptionsPageCallbacks callbacks{nullptr, readNothing, ignoreChange};
    const LocalizedText title{L"ページ", L"Page"};
    hostRegistry.registrationOpen = true;
    check("page accepted on the supported exe", addOptionsPage(mod, &title, &item, 1, &callbacks));
    check("onFrame accepted", onFrame(mod, ignoreEvent, nullptr));
    check("onOptionsSaved accepted", onOptionsSaved(mod, ignoreEvent, nullptr));
    const int32_t ghostChoices[] = {601, 602, 603, 604};
    const ConversationChoice choice{sizeof(ConversationChoice), 143, ghostChoices, 4, {L"戦う", L"Fight"},
        conversationBeforeLast, nullptr, endTalk, nullptr};
    check("a conversation line accepted", addConversationChoice(mod, &choice));
    const LocalizedText greeting{L"よく来た", L"Welcome"};
    const ScriptTextReplacement replacement{sizeof(ScriptTextReplacement), 143, 100, &greeting, 1, nullptr, nullptr};
    check("a script text replacement accepted", replaceScriptText(mod, &replacement));
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
        hostRegistry.supported.options && hostRegistry.supported.frame && hostRegistry.supported.optionsSaved &&
        hostRegistry.supported.gameTexts && hostRegistry.supported.conversations && hostRegistry.supported.scriptTexts);
    check("every site is reserved for the ModLoader",
        sitesReserved(optionsMenuSites) && sitesReserved(frameSites) && sitesReserved(optionsSavedSites) &&
        sitesReserved(conversationSites) && sitesReserved(scriptTextSites));
    check("a Mod cannot reserve the choice update", !claimHook(reinterpret_cast<HMODULE>(0x10000), gameAddress(0x419a50), 1));
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
    for (const HookSite& site : conversationSites)
        allEnabled &= MH_EnableHook(reinterpret_cast<void*>(gameAddress(site.address))) == MH_ERROR_ENABLED;
    allEnabled &= MH_EnableHook(reinterpret_cast<void*>(gameAddress(scriptTextSites[0].address))) == MH_ERROR_ENABLED;
    check("MinHook installed all 16 hooks on the copy", allEnabled);
    check("the Mod page methods start from the game's page vtable",
        modPageMethods[0] == *reinterpret_cast<uintptr_t*>(gameAddress(optionsPageMethods)) &&
        modPageMethods[buildItemsSlot / 4] == reinterpret_cast<uintptr_t>(buildItemsMethod));
    check("the scrolling page's descriptor joins the game's options page and control mapping arrows",
        scrollingLayoutsMatch() && !strcmp(scrollingLayoutFiles[0].name, "u_st_op_kamera.layout") &&
        !strcmp(scrollingLayoutFiles[1].name, "u_st_op_mpl.layout") &&
        !strcmp(scrollingLayoutFiles[downArrowLayout].name, "u_sign.layout") &&
        !strcmp(scrollingAnimations[2].name, "pl_00") && !strcmp(scrollingAnimations[upArrowShown].file, "u_sign.lyanim") &&
        !strcmp(scrollingAnimations[downArrowMoved].file, "u_sign_act.lyanim") &&
        scrollingAnimations[downArrowMoved].layout == static_cast<int32_t>(downArrowLayout) &&
        scrollingLayouts.archives == 0x31 && scrollingLayouts.slotValue == 9);
    check("the scrolling page methods replace only update, draw and input",
        scrollingPageMethods[updateSlot / 4] == reinterpret_cast<uintptr_t>(scrollingUpdateMethod) &&
        scrollingPageMethods[drawSlot / 4] == reinterpret_cast<uintptr_t>(scrollingDrawMethod) &&
        scrollingPageMethods[inputSlot / 4] == reinterpret_cast<uintptr_t>(scrollingInputMethod) &&
        baseDraw == reinterpret_cast<PageDraw>(gameAddress(0x6458e0)) &&
        baseInput == reinterpret_cast<PageInput>(gameAddress(0x645f90)) &&
        scrollingPageMethods[buildItemsSlot / 4] == modPageMethods[buildItemsSlot / 4]);

    check("hooks removed", MH_DisableHook(MH_ALL_HOOKS) == MH_OK && MH_Uninitialize() == MH_OK);
    check("every byte of the copy restored", !memcmp(image, original.data(), imageSize));

    // The menu language is read where 0x537b28 stores it; other code there means no options pages.
    uint8_t* storeLanguage = image + (0x537b28 - exeImageBase);
    storeLanguage[2] ^= 1;
    hookClaims.clear();
    prepareGameHooks(reinterpret_cast<HMODULE>(image), owner);
    check("with the language code changed, options pages and game texts are unavailable and the rest still works",
        !hostRegistry.supported.options && !hostRegistry.supported.gameTexts && hostRegistry.supported.frame &&
        hostRegistry.supported.optionsSaved && !hostRegistry.supported.conversations);
    storeLanguage[2] ^= 1;

    // The conversation hooks rely on the window's cursor at +0x218 (0x419bc0).
    uint8_t* readCursor = image + (0x419bc2 - exeImageBase);
    readCursor[0] ^= 1;
    hookClaims.clear();
    prepareGameHooks(reinterpret_cast<HMODULE>(image), owner);
    check("with the cursor read changed, conversations are unavailable and the options still work",
        !hostRegistry.supported.conversations && !hostRegistry.supported.scriptTexts && hostRegistry.supported.options);
    readCursor[0] ^= 1;

    uint8_t* wrongVersion = image;
    reinterpret_cast<IMAGE_NT_HEADERS32*>(wrongVersion + reinterpret_cast<IMAGE_DOS_HEADER*>(wrongVersion)->e_lfanew)
        ->FileHeader.TimeDateStamp ^= 1;
    hookClaims.clear();
    prepareGameHooks(reinterpret_cast<HMODULE>(wrongVersion), owner);
    check("another exe version is refused without reserving anything",
        !hostRegistry.supported.options && !hostRegistry.supported.frame && !hostRegistry.supported.gameTexts &&
        hookClaims.empty());

    VirtualFree(image, 0, MEM_RELEASE);
    printf("RESULT %u passed, %u failed (a mapped copy of the exe only; no game process, no game function called)\n",
        tests - failures, failures);
    return failures ? 1 : 0;
}
