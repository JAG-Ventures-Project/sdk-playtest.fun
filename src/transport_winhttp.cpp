// Windows desktop transport (WinHTTP, system TLS and proxy, keep-alive).
#include "platform.hpp"

#if defined(PTF_WIN_DESKTOP) && !defined(PTF_NO_WINHTTP)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <mutex>
#include <string>

#include "playtestfun/playtestfun.h"
#include "playtestfun/transport.hpp"

#define PTF_WIDEN2(x) L##x
#define PTF_WIDEN(x) PTF_WIDEN2(x)

namespace ptf::detail {

namespace {

constexpr size_t kMaxBody = 1 << 20;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

class WinHttpTransport final : public Transport {
public:
    WinHttpTransport() {
        session_ = WinHttpOpen(L"playtestfun-cpp/" PTF_WIDEN(PTF_VERSION), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_)  // before Windows 8.1
            session_ = WinHttpOpen(L"playtestfun-cpp/" PTF_WIDEN(PTF_VERSION), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_) {
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
                protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
            }
        }
    }

    ~WinHttpTransport() override {
        if (connect_) WinHttpCloseHandle(connect_);
        if (session_) WinHttpCloseHandle(session_);
    }

    bool ok() const { return session_ != nullptr; }

    HttpResponse Send(const HttpRequest& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        HttpResponse out;

        const std::wstring url = Widen(r.url);
        wchar_t host[256], path[2048], extra[2048];
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        uc.lpszHostName = host;
        uc.dwHostNameLength = ARRAYSIZE(host);
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = ARRAYSIZE(path);
        uc.lpszExtraInfo = extra;
        uc.dwExtraInfoLength = ARRAYSIZE(extra);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return out;

        HINTERNET conn = Connect(host, uc.nPort);
        if (!conn) return out;

        const std::wstring target = std::wstring(path) + extra;
        const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET req = WinHttpOpenRequest(conn, Widen(r.method).c_str(), target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!req) return out;

        // Split the budget across resolve/connect/send/receive.
        const int t = r.timeout_ms > 0 ? r.timeout_ms : 10000;
        WinHttpSetTimeouts(req, t / 4, t / 4, t / 4, t / 2);
        DWORD no_redirect = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &no_redirect, sizeof(no_redirect));

        std::wstring headers = L"Authorization: " + Widen(r.authorization) + L"\r\nAccept: application/json\r\n";
        if (!r.body.empty()) headers += L"Content-Type: application/json\r\n";

        const DWORD len = static_cast<DWORD>(r.body.size());
        BOOL sent = WinHttpSendRequest(req, headers.c_str(), static_cast<DWORD>(-1L),
                                       len ? const_cast<char*>(r.body.data()) : WINHTTP_NO_REQUEST_DATA, len, len, 0);
        if (sent && WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);
            out.status = static_cast<int>(status);
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0 && out.body.size() < kMaxBody) {
                const size_t old = out.body.size();
                out.body.resize(old + avail);
                DWORD read = 0;
                if (!WinHttpReadData(req, &out.body[old], avail, &read)) {
                    out.body.resize(old);
                    break;
                }
                out.body.resize(old + read);
            }
        } else {
            // Drop the cached connection after a transport error.
            WinHttpCloseHandle(connect_);
            connect_ = nullptr;
        }
        WinHttpCloseHandle(req);
        return out;
    }

private:
    HINTERNET Connect(const wchar_t* host, INTERNET_PORT port) {
        if (connect_ && host_ == host && port_ == port) return connect_;
        if (connect_) WinHttpCloseHandle(connect_);
        connect_ = WinHttpConnect(session_, host, port, 0);
        host_ = host;
        port_ = port;
        return connect_;
    }

    std::mutex mu_;
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;
    std::wstring host_;
    INTERNET_PORT port_ = 0;
};

}  // namespace

std::shared_ptr<Transport> CreateWinHttpTransport() {
    auto t = std::make_shared<WinHttpTransport>();
    return t->ok() ? t : nullptr;
}

}  // namespace ptf::detail

#endif
