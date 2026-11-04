// macOS / iOS transport (NSURLSession). Works with or without ARC.
#include "platform.hpp"

#if defined(__APPLE__) && !defined(PTF_NO_NSURLSESSION)

#import <Foundation/Foundation.h>

#include <memory>
#include <string>

#include "playtestfun/transport.hpp"

namespace ptf::detail {

namespace {

constexpr size_t kMaxBody = 1 << 20;

NSString* Str(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

class AppleTransport final : public Transport {
public:
    HttpResponse Send(const HttpRequest& r) override {
        @autoreleasepool {
            NSURL* url = [NSURL URLWithString:Str(r.url)];
            if (!url) return {};

            NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:url
                                                               cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                           timeoutInterval:r.timeout_ms / 1000.0];
            req.HTTPMethod = Str(r.method);
            [req setValue:Str(r.authorization) forHTTPHeaderField:@"Authorization"];
            [req setValue:@"application/json" forHTTPHeaderField:@"Accept"];
            if (!r.body.empty()) {
                [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
                req.HTTPBody = [NSData dataWithBytes:r.body.data() length:r.body.size()];
            }

            // Shared result outlives this frame if the wait times out.
            auto result = std::make_shared<HttpResponse>();
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            NSURLSessionDataTask* task = [[NSURLSession sharedSession]
                dataTaskWithRequest:req
                  completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                    if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
                        result->status = static_cast<int>(((NSHTTPURLResponse*)response).statusCode);
                        if (data && data.length <= kMaxBody)
                            result->body.assign(static_cast<const char*>(data.bytes), data.length);
                    }
                    dispatch_semaphore_signal(sem);
                  }];
            [task resume];

            const int64_t wait_ns = static_cast<int64_t>(r.timeout_ms + 1000) * NSEC_PER_MSEC;
            const bool done = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, wait_ns)) == 0;
            if (!done) [task cancel];
#if !__has_feature(objc_arc) && !OS_OBJECT_USE_OBJC
            dispatch_release(sem);
#endif
            return done ? *result : HttpResponse{};
        }
    }
};

}  // namespace

std::shared_ptr<Transport> CreateAppleTransport() {
    return std::make_shared<AppleTransport>();
}

}  // namespace ptf::detail

#endif
