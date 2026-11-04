// C API over one process-wide Client.
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "playtestfun/playtestfun.h"
#include "playtestfun/playtestfun.hpp"

namespace {

std::mutex g_mu;
std::shared_ptr<ptf::Client> g_client;

std::shared_ptr<ptf::Client> Current() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_client;
}

std::string Str(const char* s) {
    return s ? s : "";
}

void Copy(char* dst, size_t cap, const std::string& src) {
    const size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

// Adapts a C callback to ptf::Transport.
class CallbackTransport final : public ptf::Transport {
public:
    CallbackTransport(ptf_http_fn fn, void* user) : fn_(fn), user_(user), buffer_(64 * 1024) {}

    ptf::HttpResponse Send(const ptf::HttpRequest& r) override {
        std::string headers = "Authorization: " + r.authorization + "\r\nAccept: application/json\r\n";
        if (!r.body.empty()) headers += "Content-Type: application/json\r\n";
        buffer_[0] = '\0';
        ptf::HttpResponse out;
        out.status = fn_(user_, r.method.c_str(), r.url.c_str(), headers.c_str(), r.body.c_str(), buffer_.data(),
                         buffer_.size(), r.timeout_ms);
        buffer_.back() = '\0';
        out.body = buffer_.data();
        return out;
    }

private:
    ptf_http_fn fn_;
    void* user_;
    std::vector<char> buffer_;  // worker thread only
};

}  // namespace

extern "C" {

PTF_API void ptf_config_init(ptf_config* config) {
    if (!config) return;
    std::memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->open_url_on_shutdown = 1;
}

PTF_API int ptf_init(const ptf_config* c) {
    if (!c || c->struct_size < sizeof(ptf_config) || !c->game_key) return 0;

    ptf::Config cfg;
    cfg.game_key = c->game_key;
    cfg.playtest_id = Str(c->playtest_id);
    cfg.build_version = Str(c->build_version);
    if (c->base_url) cfg.base_url = c->base_url;
    cfg.storage_dir = Str(c->storage_dir);
    if (c->exit_url) cfg.exit_url = c->exit_url;
    cfg.open_url_on_shutdown = c->open_url_on_shutdown != 0;
    cfg.heartbeat_seconds = c->heartbeat_seconds;
    cfg.shutdown_timeout_ms = c->shutdown_timeout_ms;
    if (c->http) cfg.transport = std::make_shared<CallbackTransport>(c->http, c->http_user);
    if (c->log) {
        ptf_log_fn fn = c->log;
        void* user = c->log_user;
        cfg.log = [fn, user](ptf::LogLevel level, const std::string& msg) { fn(static_cast<int>(level), msg.c_str(), user); };
    }

    auto next = std::make_shared<ptf::Client>(std::move(cfg));
    std::shared_ptr<ptf::Client> old;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        old = std::move(g_client);
        g_client = std::move(next);
    }
    if (old) old->Shutdown();
    return 1;
}

PTF_API int ptf_set_steam_id(uint64_t steam_id64) {
    auto c = Current();
    return c && c->SetSteamId(steam_id64) ? 1 : 0;
}

PTF_API int ptf_set_steam_id_str(const char* steam_id64) {
    auto c = Current();
    return c && steam_id64 && c->SetSteamId(std::string(steam_id64)) ? 1 : 0;
}

PTF_API void ptf_set_paused(int paused) {
    if (auto c = Current()) c->SetPaused(paused != 0);
}

PTF_API int ptf_set_session_data(const char* json_object) {
    auto c = Current();
    return c && c->SetSessionData(Str(json_object)) ? 1 : 0;
}

PTF_API int ptf_goal(const char* key) {
    auto c = Current();
    return c && key && c->Goal(key) ? 1 : 0;
}

PTF_API int ptf_goal_value(const char* key, double value) {
    auto c = Current();
    return c && key && c->Goal(key, value) ? 1 : 0;
}

PTF_API int ptf_test_result(int test_id, int passed, const char* message) {
    auto c = Current();
    return c && c->TestResult(test_id, passed != 0, Str(message)) ? 1 : 0;
}

PTF_API int ptf_crash(const char* fingerprint, const char* message, const char* stack) {
    auto c = Current();
    return c && fingerprint && c->Crash(fingerprint, Str(message), Str(stack)) ? 1 : 0;
}

PTF_API int ptf_event(const char* event_type, const char* status, const char* data_json) {
    auto c = Current();
    if (!c || !event_type || !status) return 0;
    ptf::JsonObject data;
    if (data_json && !ptf::JsonObject::FromRaw(data_json, data)) return 0;
    return c->Event(event_type, status, data) ? 1 : 0;
}

PTF_API void ptf_flush(void) {
    if (auto c = Current()) c->Flush();
}

PTF_API int ptf_get_stats(ptf_stats* out) {
    if (!out) return 0;
    std::memset(out, 0, sizeof(*out));
    out->player_linked = out->player_accepted = -1;
    auto c = Current();
    if (!c) return 0;
    const ptf::Stats s = c->GetStats();
    out->local_session_seconds = s.local_session_seconds;
    out->server_session_seconds = s.server_session_seconds;
    out->server_total_seconds = s.server_total_seconds;
    out->last_http_status = s.last_http_status;
    out->player_linked = s.player_linked;
    out->player_accepted = s.player_accepted;
    Copy(out->last_error, sizeof(out->last_error), s.last_error);
    Copy(out->session_id, sizeof(out->session_id), s.session_id);
    out->pending_events = static_cast<int32_t>(s.pending_events);
    return 1;
}

PTF_API void ptf_shutdown(void) {
    std::shared_ptr<ptf::Client> c;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        c = std::move(g_client);
    }
    if (c) c->Shutdown();  // outside the lock: may wait for the final send
}

PTF_API int ptf_open_url(const char* url) {
    return url && ptf::OpenUrl(url) ? 1 : 0;
}

PTF_API const char* ptf_version(void) {
    return PTF_VERSION;
}

}  // extern "C"
