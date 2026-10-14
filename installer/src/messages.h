// Screen texts in Japanese and English. {0}, {1}, ... are filled in by formatText.
#pragma once
#include "failure.h"
#include <string>

namespace wots4::installer {

enum class Language { japanese, english };

struct Messages {
    const wchar_t* toggleHelp;
    const wchar_t* yes;
    const wchar_t* no;

    const wchar_t* gameFound;          // {0}: path of the exe
    const wchar_t* gameNotFound;
    const wchar_t* chooseGameExe;
    const wchar_t* gameExeDialogTitle;
    const wchar_t* gameExeFilterName;
    const wchar_t* wrongExeChosen;     // {0}: chosen file
    const wchar_t* noGameChosen;
    const wchar_t* installFolder;      // {0}: game folder
    const wchar_t* gameRunning;

    const wchar_t* foreignLoaderFound; // {0}: backup file name
    const wchar_t* foreignLoaderKept;

    const wchar_t* fetchingModList;
    const wchar_t* modListFailed;      // {0}: reason
    const wchar_t* chooseMods;
    const wchar_t* installedNote;

    const wchar_t* downloading;        // {0}: ModLoader or Mod name
    const wchar_t* downloaded;         // {0}: name, {1}: tag
    const wchar_t* downloadFailed;     // {0}: name, {1}: reason
    const wchar_t* loaderDownloadFailed;
    const wchar_t* writing;
    const wchar_t* loaderWriteFailed;  // {0}: reason

    const wchar_t* resultHeader;
    const wchar_t* resultInstalled;    // {0}: name, {1}: tag
    const wchar_t* resultUpdated;      // {0}: name, {1}: tag
    const wchar_t* resultFailed;       // {0}: name, {1}: reason
    const wchar_t* resultBackup;       // {0}: backup file name
    const wchar_t* pressEnterToClose;

    const wchar_t* connectionFailure;  // {0}: URL, {1}: system message
    const wchar_t* httpStatusFailure;  // {0}: URL, {1}: status
    const wchar_t* noReleaseFailure;   // {0}: repository URL
    const wchar_t* fileAccessFailure;  // {0}: path, {1}: system message
    const wchar_t* zipUnreadableFailure;      // {0}: zip name
    const wchar_t* zipUnexpectedEntryFailure; // {0}: zip name, {1}: entry
};

const Messages& messagesFor(Language language);

// One line about why a step failed, in the language of messages.
std::wstring describeFailure(const Failure& failure, const Messages& messages);

}  // namespace wots4::installer
