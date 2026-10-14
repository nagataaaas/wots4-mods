// HTTPS downloads with WinHTTP (part of Windows, so the installer needs no other files).
#pragma once
#include "failure.h"
#include <filesystem>
#include <optional>
#include <string>

typedef void* HINTERNET;

namespace wots4::installer {

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // The Location of a redirect from url, without following it.
    std::optional<Failure> redirectTarget(const std::string& url, std::string& location);

    std::optional<Failure> fetchText(const std::string& url, std::string& body);

    // Follows redirects (GitHub serves release files from another host).
    std::optional<Failure> download(const std::string& url, const std::filesystem::path& file);

private:
    struct Request;
    std::optional<Failure> send(const std::string& url, bool followRedirects, Request& request);
    std::optional<Failure> readBody(const std::string& url, Request& request, std::string& body);

    HINTERNET session_ = nullptr;
};

}  // namespace wots4::installer
