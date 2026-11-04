#include "playtestfun_node.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "playtestfun/playtestfun.hpp"

namespace godot {

namespace {
std::string Utf8(const String& s) {
    return std::string(s.utf8().get_data());
}
}  // namespace

PlaytestFun::PlaytestFun() = default;

PlaytestFun::~PlaytestFun() {
    if (client) client->Shutdown();  // no site from a destructor
}

void PlaytestFun::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_game_key", "value"), &PlaytestFun::set_game_key);
    ClassDB::bind_method(D_METHOD("get_game_key"), &PlaytestFun::get_game_key);
    ClassDB::bind_method(D_METHOD("set_playtest_id", "value"), &PlaytestFun::set_playtest_id);
    ClassDB::bind_method(D_METHOD("get_playtest_id"), &PlaytestFun::get_playtest_id);
    ClassDB::bind_method(D_METHOD("set_heartbeat_seconds", "value"), &PlaytestFun::set_heartbeat_seconds);
    ClassDB::bind_method(D_METHOD("get_heartbeat_seconds"), &PlaytestFun::get_heartbeat_seconds);
    ClassDB::bind_method(D_METHOD("set_open_site_on_exit", "value"), &PlaytestFun::set_open_site_on_exit);
    ClassDB::bind_method(D_METHOD("get_open_site_on_exit"), &PlaytestFun::get_open_site_on_exit);
    ClassDB::bind_method(D_METHOD("set_exit_url", "value"), &PlaytestFun::set_exit_url);
    ClassDB::bind_method(D_METHOD("get_exit_url"), &PlaytestFun::get_exit_url);
    ClassDB::bind_method(D_METHOD("set_build_version", "value"), &PlaytestFun::set_build_version);
    ClassDB::bind_method(D_METHOD("get_build_version"), &PlaytestFun::get_build_version);

    ADD_PROPERTY(PropertyInfo(Variant::STRING, "game_key"), "set_game_key", "get_game_key");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "playtest_id"), "set_playtest_id", "get_playtest_id");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "build_version"), "set_build_version", "get_build_version");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "heartbeat_seconds"), "set_heartbeat_seconds", "get_heartbeat_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "open_site_on_exit"), "set_open_site_on_exit", "get_open_site_on_exit");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "exit_url"), "set_exit_url", "get_exit_url");

    ClassDB::bind_method(D_METHOD("set_steam_id", "steam_id64"), &PlaytestFun::set_steam_id);
    ClassDB::bind_method(D_METHOD("set_session_data", "data"), &PlaytestFun::set_session_data);
    ClassDB::bind_method(D_METHOD("goal", "key"), &PlaytestFun::goal);
    ClassDB::bind_method(D_METHOD("goal_value", "key", "value"), &PlaytestFun::goal_value);
    ClassDB::bind_method(D_METHOD("test_result", "test_id", "passed", "message"), &PlaytestFun::test_result, DEFVAL(""));
    ClassDB::bind_method(D_METHOD("crash", "fingerprint", "message", "stack"), &PlaytestFun::crash, DEFVAL(""),
                         DEFVAL(""));
    ClassDB::bind_method(D_METHOD("send_event", "event_type", "status", "data"), &PlaytestFun::send_event,
                         DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("flush"), &PlaytestFun::flush);
    ClassDB::bind_method(D_METHOD("get_session_id"), &PlaytestFun::get_session_id);
    ClassDB::bind_method(D_METHOD("get_session_playtime"), &PlaytestFun::get_session_playtime);
    ClassDB::bind_method(D_METHOD("get_total_playtime"), &PlaytestFun::get_total_playtime);
    ClassDB::bind_method(D_METHOD("get_player_accepted"), &PlaytestFun::get_player_accepted);
    ClassDB::bind_method(D_METHOD("finish"), &PlaytestFun::finish);
}

void PlaytestFun::_notification(int p_what) {
    if (Engine::get_singleton()->is_editor_hint()) return;
    switch (p_what) {
        case NOTIFICATION_READY: start(); break;
        case NOTIFICATION_APPLICATION_PAUSED:
            if (client) client->SetPaused(true);
            break;
        case NOTIFICATION_APPLICATION_RESUMED:
            if (client) client->SetPaused(false);
            break;
        case NOTIFICATION_WM_CLOSE_REQUEST:  // window closed
        case NOTIFICATION_EXIT_TREE:         // get_tree().quit() for an autoload
            finish();
            break;
        default: break;
    }
}

void PlaytestFun::start() {
    if (client) return;
    if (game_key.is_empty()) {
        UtilityFunctions::push_warning("playtest.fun: game_key is empty");
        return;
    }
    ptf::Config cfg;
    cfg.game_key = Utf8(game_key);
    cfg.playtest_id = Utf8(playtest_id);
    cfg.build_version = Utf8(build_version.is_empty()
                                 ? String(ProjectSettings::get_singleton()->get_setting("application/config/version", ""))
                                 : build_version);
    cfg.heartbeat_seconds = static_cast<uint32_t>(heartbeat_seconds < 10 ? 10 : heartbeat_seconds);
    cfg.storage_dir = Utf8(ProjectSettings::get_singleton()->globalize_path("user://playtestfun"));
    cfg.open_url_on_shutdown = false;  // OS.shell_open below
    // No log callback: SDK warnings go to stderr from its worker thread.
    client = std::make_unique<ptf::Client>(std::move(cfg));
}

void PlaytestFun::finish() {
    if (!client) return;
    client->Shutdown();
    client.reset();
    if (open_site_on_exit && (exit_url.begins_with("https://") || exit_url.begins_with("http://")))
        OS::get_singleton()->shell_open(exit_url);
}

bool PlaytestFun::set_steam_id(const String& steam_id64) {
    return client && client->SetSteamId(Utf8(steam_id64));
}

bool PlaytestFun::set_session_data(const Dictionary& data) {
    return client && client->SetSessionData(Utf8(JSON::stringify(data)));
}

bool PlaytestFun::goal(const String& key) {
    return client && client->Goal(Utf8(key));
}

bool PlaytestFun::goal_value(const String& key, double value) {
    return client && client->Goal(Utf8(key), value);
}

bool PlaytestFun::test_result(int test_id, bool passed, const String& message) {
    return client && client->TestResult(test_id, passed, Utf8(message));
}

bool PlaytestFun::crash(const String& fingerprint, const String& message, const String& stack) {
    ptf::JsonObject hardware;
    hardware.Set("cpu", Utf8(OS::get_singleton()->get_processor_name()))
        .Set("os", Utf8(OS::get_singleton()->get_name() + " " + OS::get_singleton()->get_version()));
    return client && client->Crash(Utf8(fingerprint), Utf8(message), Utf8(stack), hardware);
}

bool PlaytestFun::send_event(const String& event_type, const String& status, const Dictionary& data) {
    ptf::JsonObject obj;
    return client && ptf::JsonObject::FromRaw(Utf8(JSON::stringify(data)), obj) &&
           client->Event(Utf8(event_type), Utf8(status), obj);
}

void PlaytestFun::flush() {
    if (client) client->Flush();
}

String PlaytestFun::get_session_id() const {
    return client ? String::utf8(client->GetStats().session_id.c_str()) : String();
}

double PlaytestFun::get_session_playtime() const {
    return client ? client->GetStats().local_session_seconds : 0.0;
}

double PlaytestFun::get_total_playtime() const {
    return client ? client->GetStats().server_total_seconds : 0.0;
}

int PlaytestFun::get_player_accepted() const {
    return client ? client->GetStats().player_accepted : -1;
}

}  // namespace godot
