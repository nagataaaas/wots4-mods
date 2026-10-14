#include "http_client.h"
#include "file_io.h"
#include "text_utils.h"
#include "version_text.h"
#include <windows.h>
#include <winhttp.h>
#include <memory>

namespace wots4::installer {
namespace {

const std::wstring userAgent = std::wstring(L"WayOfTheSamurai4ModInstaller/") + installerVersion;

constexpr DWORD httpOk = 200;
constexpr DWORD readChunkSize = 64 * 1024;

struct InternetHandleCloser {
    void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
};
using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

Failure connectionFailure(const std::string& url, DWORD error) {
    return {FailureKind::connection, error, fromUtf8(url)};
}

bool isRedirect(DWORD status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

}  // namespace

struct HttpClient::Request {
    InternetHandle connection;
    InternetHandle request;
    DWORD status = 0;
};

HttpClient::HttpClient() {
    // Automatic proxy needs Windows 8.1; older Windows falls back to the WinHTTP proxy setting
    session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) {
        session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session_) return;
    // GitHub accepts only TLS 1.2 and later, which Windows 7 does not turn on by default.
    // Windows without TLS 1.3 refuses the first set, so fall back to TLS 1.2 alone.
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
}

HttpClient::~HttpClient() {
    if (session_) WinHttpCloseHandle(session_);
}

std::optional<Failure> HttpClient::send(const std::string& url, bool followRedirects, Request& request) {
    if (!session_) return connectionFailure(url, GetLastError());
    const std::wstring wideUrl = fromUtf8(url);
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256], path[2048];
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) return connectionFailure(url, GetLastError());

    request.connection.reset(WinHttpConnect(session_, host, parts.nPort, 0));
    if (!request.connection) return connectionFailure(url, GetLastError());
    const DWORD secure = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    request.request.reset(WinHttpOpenRequest(request.connection.get(), L"GET", path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, secure));
    if (!request.request) return connectionFailure(url, GetLastError());
    if (!followRedirects) {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    }
    if (!WinHttpSendRequest(request.request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.request.get(), nullptr)) {
        return connectionFailure(url, GetLastError());
    }
    DWORD statusSize = sizeof(request.status);
    if (!WinHttpQueryHeaders(request.request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &request.status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        return connectionFailure(url, GetLastError());
    }
    return std::nullopt;
}

std::optional<Failure> HttpClient::readBody(const std::string& url, Request& request, std::string& body) {
    body.clear();
    std::string chunk(readChunkSize, '\0');
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.request.get(), chunk.data(), readChunkSize, &read)) {
            return connectionFailure(url, GetLastError());
        }
        if (read == 0) return std::nullopt;
        body.append(chunk.data(), read);
    }
}

std::optional<Failure> HttpClient::redirectTarget(const std::string& url, std::string& location) {
    Request request;
    if (auto failure = send(url, false, request)) return failure;
    if (!isRedirect(request.status)) return Failure{FailureKind::httpStatus, request.status, fromUtf8(url)};
    wchar_t header[2048];
    DWORD headerSize = sizeof(header);
    if (!WinHttpQueryHeaders(request.request.get(), WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, header,
            &headerSize, WINHTTP_NO_HEADER_INDEX)) {
        return connectionFailure(url, GetLastError());
    }
    location = toUtf8(header);
    return std::nullopt;
}

std::optional<Failure> HttpClient::fetchText(const std::string& url, std::string& body) {
    Request request;
    if (auto failure = send(url, true, request)) return failure;
    if (request.status != httpOk) return Failure{FailureKind::httpStatus, request.status, fromUtf8(url)};
    return readBody(url, request, body);
}

std::optional<Failure> HttpClient::download(const std::string& url, const std::filesystem::path& file) {
    std::string body;
    if (auto failure = fetchText(url, body)) return failure;
    return writeWholeFile(file, body);
}

}  // namespace wots4::installer
