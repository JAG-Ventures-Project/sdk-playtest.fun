// libcurl transport (Linux, BSD, or anywhere PTF_USE_CURL is defined).
#include "platform.hpp"

#if defined(PTF_USE_CURL)

#include <curl/curl.h>

#include <mutex>
#include <string>

#include "playtestfun/playtestfun.h"
#include "playtestfun/transport.hpp"

namespace ptf::detail {

namespace {

constexpr size_t kMaxBody = 1 << 20;

size_t OnWrite(char* data, size_t size, size_t count, void* user) {
    auto* body = static_cast<std::string*>(user);
    const size_t n = size * count;
    if (body->size() + n > kMaxBody) return 0;
    body->append(data, n);
    return n;
}

class CurlTransport final : public Transport {
public:
    CurlTransport() {
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
        handle_ = curl_easy_init();
    }

    ~CurlTransport() override {
        if (handle_) curl_easy_cleanup(handle_);
    }

    bool ok() const { return handle_ != nullptr; }

    HttpResponse Send(const HttpRequest& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        HttpResponse out;
        CURL* h = handle_;
        curl_easy_reset(h);  // keeps the connection cache

        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: " + r.authorization).c_str());
        headers = curl_slist_append(headers, "Accept: application/json");
        if (!r.body.empty()) headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(h, CURLOPT_URL, r.url.c_str());
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(h, CURLOPT_USERAGENT, "playtestfun-cpp/" PTF_VERSION);
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(r.timeout_ms));
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(r.timeout_ms / 2));
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, OnWrite);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, &out.body);
        if (r.method == "POST") {
            curl_easy_setopt(h, CURLOPT_POST, 1L);
            curl_easy_setopt(h, CURLOPT_POSTFIELDS, r.body.c_str());
            curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(r.body.size()));
        } else {
            curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
        }

        if (curl_easy_perform(h) == CURLE_OK) {
            long status = 0;
            curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
            out.status = static_cast<int>(status);
        } else {
            out.body.clear();
        }
        curl_slist_free_all(headers);
        return out;
    }

private:
    std::mutex mu_;
    CURL* handle_ = nullptr;
};

}  // namespace

std::shared_ptr<Transport> CreateCurlTransport() {
    auto t = std::make_shared<CurlTransport>();
    return t->ok() ? t : nullptr;
}

}  // namespace ptf::detail

#endif
