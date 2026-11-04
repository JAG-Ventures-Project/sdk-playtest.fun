// Tiny JSON object builder for session data and event payloads. Escapes strings for you.
//   ptf::JsonObject d;
//   d.Set("level", 3).Set("difficulty", "hard").Set("speedrun", true);
#pragma once

#include <string>
#include <string_view>
#include <type_traits>

namespace ptf {

class JsonObject {
public:
    JsonObject& Set(std::string_view key, std::string_view value);
    JsonObject& Set(std::string_view key, const char* value);  // else const char* would pick bool
    JsonObject& Set(std::string_view key, const std::string& value) { return Set(key, std::string_view(value)); }
    JsonObject& Set(std::string_view key, bool value);
    JsonObject& Set(std::string_view key, double value);
    JsonObject& Set(std::string_view key, const JsonObject& value) { return SetRaw(key, value.str()); }

    template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    JsonObject& Set(std::string_view key, T value) {
        return SetRaw(key, std::to_string(value));
    }

    JsonObject& SetNull(std::string_view key) { return SetRaw(key, "null"); }

    // Pre-built JSON (arrays, nested data). Not validated.
    JsonObject& SetRaw(std::string_view key, std::string_view json);

    // Wraps an existing JSON object text ("{...}"). Only the outer braces are checked.
    static bool FromRaw(std::string_view object_json, JsonObject& out);

    std::string str() const { return "{" + body_ + "}"; }
    bool empty() const { return body_.empty(); }
    size_t size_bytes() const { return body_.size() + 2; }

private:
    void Key(std::string_view key);
    std::string body_;  // members without the braces; keys are appended, don't repeat one
};

}  // namespace ptf
