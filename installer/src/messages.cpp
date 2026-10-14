#include "messages.h"
#include "text_utils.h"
#include <windows.h>

namespace wots4::installer {
namespace {

const Messages japanese{
    L"↑↓ で移動、Space で切り替え、Enter で確定します。",
    L"はい",
    L"いいえ",

    L"ゲームが見つかりました：\n  {0}\nここにインストールしますか？",
    L"ゲームのフォルダが見つかりませんでした。",
    L"WayOfTheSamurai4.exe を選んでください。",
    L"WayOfTheSamurai4.exe を選んでください",
    L"侍道4 (WayOfTheSamurai4.exe)",
    L"選んだファイルは WayOfTheSamurai4.exe ではありません：{0}",
    L"ゲームが選ばれなかったので、何も変えずに終わります。",
    L"インストール先：{0}",
    L"ゲームが起動しています。ゲームを閉じてから、もう一度インストーラーを起動してください。",

    L"ゲームのフォルダに、別のツールの dinput8.dll があります。\n"
    L"名前を {0} に変えて、ModLoader に置き換えますか？\n"
    L"（名前を変えると、そのツールは読み込まれなくなります）",
    L"ModLoader を置かなかったので、Mod も入れずに終わります。",

    L"Mod の一覧を取得しています…",
    L"Mod の一覧を取得できませんでした：{0}\nModLoader だけをインストールします。",
    L"インストールする Mod を選んでください。",
    L"インストール済み",

    L"{0} の最新版を取得しています…",
    L"  {0} {1} をダウンロードしました。",
    L"  {0} を取得できませんでした：{1}",
    L"ModLoader を取得できなかったので、何も変えずに終わります。",
    L"ゲームのフォルダに書き込んでいます…",
    L"ModLoader を書き込めませんでした：{0}\nMod は入れずに終わります。",

    L"結果",
    L"  {0} {1}：インストールしました",
    L"  {0} {1}：更新しました",
    L"  {0}：失敗しました（{1}）",
    L"  元の dinput8.dll は {0} に名前を変えました",
    L"Enter キーを押すと閉じます。",

    L"{0} に接続できませんでした（{1}）",
    L"{0} が HTTP {1} を返しました",
    L"{0} にリリースがありません",
    L"{0} を読み書きできませんでした（{1}）",
    L"{0} を展開できませんでした（zip が壊れています）",
    L"{0} に、入れてはいけない場所のファイルがあります：{1}",
};

const Messages english{
    L"Use Up and Down to move, Space to toggle and Enter to confirm.",
    L"Yes",
    L"No",

    L"Found the game:\n  {0}\nInstall here?",
    L"Could not find the game folder.",
    L"Choose WayOfTheSamurai4.exe.",
    L"Choose WayOfTheSamurai4.exe",
    L"Way of the Samurai 4 (WayOfTheSamurai4.exe)",
    L"The chosen file is not WayOfTheSamurai4.exe: {0}",
    L"No game was chosen, so nothing was changed.",
    L"Install folder: {0}",
    L"The game is running. Close the game, then start the installer again.",

    L"The game folder has a dinput8.dll from another tool.\n"
    L"Rename it to {0} and replace it with the ModLoader?\n"
    L"(After renaming, that tool is no longer loaded.)",
    L"The ModLoader was not installed, so no Mods were installed either.",

    L"Getting the list of Mods...",
    L"Could not get the list of Mods: {0}\nInstalling only the ModLoader.",
    L"Choose the Mods to install.",
    L"installed",

    L"Getting the latest {0}...",
    L"  Downloaded {0} {1}.",
    L"  Could not get {0}: {1}",
    L"Could not get the ModLoader, so nothing was changed.",
    L"Writing to the game folder...",
    L"Could not write the ModLoader: {0}\nNo Mods were installed.",

    L"Result",
    L"  {0} {1}: installed",
    L"  {0} {1}: updated",
    L"  {0}: failed ({1})",
    L"  The old dinput8.dll was renamed to {0}",
    L"Press Enter to close.",

    L"Could not connect to {0} ({1})",
    L"{0} returned HTTP {1}",
    L"{0} has no releases",
    L"Could not read or write {0} ({1})",
    L"Could not extract {0} (the zip is broken)",
    L"{0} has a file in a place it must not write to: {1}",
};

// The Windows text for a Win32 or WinHTTP error code, in the Windows display language.
std::wstring systemMessage(unsigned long code) {
    wchar_t* text = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_FROM_HMODULE |
        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, GetModuleHandleW(L"winhttp.dll"), code, 0,
        reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring message = length ? std::wstring(text, length) : std::wstring();
    LocalFree(text);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return formatText(L"{0}, code {1}", {message, std::to_wstring(code)});
}

}  // namespace

const Messages& messagesFor(Language language) {
    return language == Language::japanese ? japanese : english;
}

std::wstring describeFailure(const Failure& failure, const Messages& messages) {
    switch (failure.kind) {
    case FailureKind::connection:
        return formatText(messages.connectionFailure, {failure.subject, systemMessage(failure.code)});
    case FailureKind::httpStatus:
        return formatText(messages.httpStatusFailure, {failure.subject, std::to_wstring(failure.code)});
    case FailureKind::noRelease:
        return formatText(messages.noReleaseFailure, {failure.subject});
    case FailureKind::fileAccess:
        return formatText(messages.fileAccessFailure, {failure.subject, systemMessage(failure.code)});
    case FailureKind::zipUnreadable:
        return formatText(messages.zipUnreadableFailure, {failure.subject});
    case FailureKind::zipUnexpectedEntry:
        return formatText(messages.zipUnexpectedEntryFailure, {failure.subject, failure.detail});
    }
    return failure.subject;
}

}  // namespace wots4::installer
