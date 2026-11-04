// Small internal helpers: ids, time, number formatting, minimal JSON.
#pragma once

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace ptf::detail {

std::string NewUuid();
std::string IsoTime(std::chrono::system_clock::time_point tp);     // 2026-11-04T18:02:11.000Z
std::string CompactUtc(std::chrono::system_clock::time_point tp);  // 20261104T180211Z
double RandomUnit();  // [0, 1)

// "<version>-<utc>-<random>" when a build version is given, else a UUID.
std::string MakeSessionId(std::string_view build_version, std::chrono::system_clock::time_point now);

// Locale-independent "123.456".
std::string FormatSeconds(double seconds);
// Locale-independent JSON number; NaN/inf become null.
std::string FormatNumber(double value);

// Cuts at a UTF-8 character boundary.
std::string TruncateUtf8(std::string_view s, size_t max_bytes);

// "windows", "macos", "ios", "android", "linux" or "unknown".
const char* PlatformName();

bool IsSteamId64(std::string_view s);

// JSON writing
void AppendQuoted(std::string& out, std::string_view value);

// JSON reading: raw value at an object path, e.g. {"data", "totalPlaytimeSeconds"}.
bool JsonFind(std::string_view json, std::initializer_list<std::string_view> path, std::string_view& out);
bool JsonNumber(std::string_view raw, double& out);
bool JsonBool(std::string_view raw, bool& out);
bool JsonString(std::string_view raw, std::string& out);

}  // namespace ptf::detail
