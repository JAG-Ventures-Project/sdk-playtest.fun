// Godot 4 node: add as an autoload. Session starts once set_steam_id() is called.
#pragma once

#include <memory>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace ptf { class Client; }

namespace godot {

class PlaytestFun : public Node {
    GDCLASS(PlaytestFun, Node)

public:
    PlaytestFun();
    ~PlaytestFun() override;

    void set_game_key(const String& v) { game_key = v; }
    String get_game_key() const { return game_key; }
    void set_playtest_id(const String& v) { playtest_id = v; }
    String get_playtest_id() const { return playtest_id; }
    void set_heartbeat_seconds(int v) { heartbeat_seconds = v; }
    int get_heartbeat_seconds() const { return heartbeat_seconds; }
    void set_open_site_on_exit(bool v) { open_site_on_exit = v; }
    bool get_open_site_on_exit() const { return open_site_on_exit; }
    void set_exit_url(const String& v) { exit_url = v; }
    String get_exit_url() const { return exit_url; }

    void set_build_version(const String& v) { build_version = v; }
    String get_build_version() const { return build_version; }

    bool set_steam_id(const String& steam_id64);  // GodotSteam: str(Steam.getSteamID())
    bool set_session_data(const Dictionary& data);

    // Events: queued on disk, retried until delivered.
    bool goal(const String& key);                       // goal without a target: done
    bool goal_value(const String& key, double value);   // safe every frame: pending values collapse
    bool test_result(int test_id, bool passed, const String& message);
    bool crash(const String& fingerprint, const String& message, const String& stack);
    bool send_event(const String& event_type, const String& status, const Dictionary& data);

    void flush();
    String get_session_id() const;
    double get_session_playtime() const;
    double get_total_playtime() const;
    int get_player_accepted() const;  // -1 unknown, 0 no, 1 yes
    void finish();                    // final send + site; also runs on quit

protected:
    static void _bind_methods();
    void _notification(int p_what);

private:
    void start();

    String game_key;
    String playtest_id;
    String build_version;  // empty = application/config/version
    String exit_url = "https://playtest.fun";
    int heartbeat_seconds = 60;
    bool open_site_on_exit = true;
    std::unique_ptr<ptf::Client> client;
};

}  // namespace godot
