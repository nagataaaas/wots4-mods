// Why a step failed. The screen text is made from it in the chosen language (messages.h).
#pragma once
#include <string>

namespace wots4::installer {

enum class FailureKind {
    connection,         // code: WinHTTP or Win32 error; subject: URL
    httpStatus,         // code: HTTP status; subject: URL
    noRelease,          // subject: repository URL
    fileAccess,         // code: Win32 error; subject: path
    zipUnreadable,      // subject: zip file name
    zipUnexpectedEntry, // subject: zip file name; detail: the entry
};

struct Failure {
    FailureKind kind;
    unsigned long code = 0;
    std::wstring subject;
    std::wstring detail;
};

}  // namespace wots4::installer
