// HTTP abstraction: platform default or engine-provided.
#pragma once

#include <memory>
#include <string>

namespace ptf {

struct HttpRequest {
    std::string method;         // "GET" / "POST"
    std::string url;
    std::string authorization;  // full header value, e.g. "Bearer phub_gk_..."
    std::string body;           // JSON, empty for GET
    int timeout_ms = 10000;
};

struct HttpResponse {
    int status = 0;  // 0 = network failure
    std::string body;
};

// Blocking; called only from the SDK worker thread.
class Transport {
public:
    virtual ~Transport() = default;
    virtual HttpResponse Send(const HttpRequest& request) = 0;
};

// WinHTTP (Windows), NSURLSession (Apple), libcurl (PTF_USE_CURL). nullptr if none.
std::shared_ptr<Transport> CreateDefaultTransport();

}  // namespace ptf
