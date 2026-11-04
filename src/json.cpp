#include "playtestfun/json.hpp"

#include "util.hpp"

namespace ptf {

namespace {
constexpr const char* kWs = " \t\r\n";
}

void JsonObject::Key(std::string_view key) {
    if (!body_.empty()) body_ += ',';
    detail::AppendQuoted(body_, key);
    body_ += ':';
}

JsonObject& JsonObject::Set(std::string_view key, std::string_view value) {
    Key(key);
    detail::AppendQuoted(body_, value);
    return *this;
}

JsonObject& JsonObject::Set(std::string_view key, const char* value) {
    return value ? Set(key, std::string_view(value)) : SetNull(key);
}

JsonObject& JsonObject::Set(std::string_view key, bool value) {
    return SetRaw(key, value ? "true" : "false");
}

JsonObject& JsonObject::Set(std::string_view key, double value) {
    return SetRaw(key, detail::FormatNumber(value));
}

JsonObject& JsonObject::SetRaw(std::string_view key, std::string_view json) {
    Key(key);
    body_ += json;
    return *this;
}

bool JsonObject::FromRaw(std::string_view object_json, JsonObject& out) {
    const auto first = object_json.find_first_not_of(kWs);
    const auto last = object_json.find_last_not_of(kWs);
    if (first == std::string_view::npos || object_json[first] != '{' || object_json[last] != '}') return false;
    const std::string_view inner = object_json.substr(first + 1, last - first - 1);
    out.body_ = inner.find_first_not_of(kWs) == std::string_view::npos ? std::string() : std::string(inner);
    return true;
}

}  // namespace ptf
