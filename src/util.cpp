#include "util.hpp"

#include "platform.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>

namespace ptf::detail {

namespace {

std::mt19937_64& Rng() {
    thread_local std::mt19937_64 rng([] {
        std::random_device rd;
        auto now = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ now;
    }());
    return rng;
}

size_t SkipWs(std::string_view s, size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    return i;
}

// s[i] == '"'; returns index past the closing quote.
size_t SkipString(std::string_view s, size_t i) {
    for (++i; i < s.size(); ++i) {
        if (s[i] == '\\') ++i;
        else if (s[i] == '"') return i + 1;
    }
    return std::string_view::npos;
}

size_t SkipValue(std::string_view s, size_t i) {
    if (i >= s.size()) return std::string_view::npos;
    if (s[i] == '"') return SkipString(s, i);
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        while (i < s.size()) {
            char c = s[i];
            if (c == '"') {
                i = SkipString(s, i);
                if (i == std::string_view::npos) return i;
                continue;
            }
            if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') {
                if (--depth == 0) return i + 1;
            }
            ++i;
        }
        return std::string_view::npos;
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' &&
           s[i] != '\r' && s[i] != '\t')
        ++i;
    return i;
}

}  // namespace

std::string NewUuid() {
    uint64_t a = Rng()(), b = Rng()();
    a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;  // version 4
    b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;  // RFC 4122 variant
    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(a >> 32),
                  static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFFF),
                  static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
    return buf;
}

namespace {
std::tm Utc(long long ms) {
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    return tm;
}

long long EpochMs(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}
}  // namespace

std::string IsoTime(std::chrono::system_clock::time_point tp) {
    const long long ms = EpochMs(tp);
    const std::tm tm = Utc(ms);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return buf;
}

std::string CompactUtc(std::chrono::system_clock::time_point tp) {
    const std::tm tm = Utc(EpochMs(tp));
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string MakeSessionId(std::string_view build_version, std::chrono::system_clock::time_point now) {
    const std::string uuid = NewUuid();
    std::string id;
    for (char c : build_version) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (ok && id.size() < 32) id += c;
    }
    if (id.empty()) return uuid;
    // The random tail keeps ids unique when two players start in the same second.
    return id + "-" + CompactUtc(now) + "-" + uuid.substr(24);
}

double RandomUnit() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(Rng());
}

std::string FormatSeconds(double seconds) {
    if (!(seconds > 0)) seconds = 0;  // also catches NaN
    if (seconds > 86400) seconds = 86400;  // API maximum per session
    const long long ms = std::llround(seconds * 1000.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld.%03lld", ms / 1000, ms % 1000);
    return buf;
}

std::string FormatNumber(double value) {
    if (!std::isfinite(value)) return "null";
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.15g", value);
    for (char* p = buf; *p; ++p)
        if (*p == ',') *p = '.';  // setlocale() may have changed the decimal point
    return buf;
}

std::string TruncateUtf8(std::string_view s, size_t max_bytes) {
    if (s.size() <= max_bytes) return std::string(s);
    size_t n = max_bytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return std::string(s.substr(0, n));
}

const char* PlatformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(PTF_MACOS)
    return "macos";
#elif defined(PTF_IOS)
    return "ios";
#elif defined(PTF_ANDROID)
    return "android";
#elif defined(PTF_LINUX_DESKTOP)
    return "linux";
#else
    return "unknown";
#endif
}

bool IsSteamId64(std::string_view s) {
    if (s.size() != 17) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

void AppendQuoted(std::string& out, std::string_view value) {
    static const char kHex[] = "0123456789abcdef";
    out.reserve(out.size() + value.size() + 2);
    out += '"';
    for (char ch : value) {
        auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0xF];
                } else {
                    out += ch;  // UTF-8 passes through
                }
        }
    }
    out += '"';
}

bool JsonFind(std::string_view json, std::initializer_list<std::string_view> path, std::string_view& out) {
    std::string_view cur = json;
    for (std::string_view key : path) {
        size_t i = SkipWs(cur, 0);
        if (i >= cur.size() || cur[i] != '{') return false;
        ++i;
        bool found = false;
        while (!found) {
            i = SkipWs(cur, i);
            if (i >= cur.size() || cur[i] != '"') return false;
            const size_t key_end = SkipString(cur, i);
            if (key_end == std::string_view::npos) return false;
            const std::string_view k = cur.substr(i + 1, key_end - i - 2);
            i = SkipWs(cur, key_end);
            if (i >= cur.size() || cur[i] != ':') return false;
            i = SkipWs(cur, i + 1);
            const size_t value_end = SkipValue(cur, i);
            if (value_end == std::string_view::npos) return false;
            if (k == key) {
                cur = cur.substr(i, value_end - i);
                found = true;
            } else {
                i = SkipWs(cur, value_end);
                if (i < cur.size() && cur[i] == ',') ++i;
            }
        }
    }
    out = cur;
    return true;
}

// Hand-rolled: strtod depends on the C locale ("1,5" in tr_TR).
bool JsonNumber(std::string_view raw, double& out) {
    size_t i = 0;
    bool neg = false;
    if (i < raw.size() && raw[i] == '-') {
        neg = true;
        ++i;
    }
    if (i >= raw.size() || raw[i] < '0' || raw[i] > '9') return false;
    double value = 0;
    while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9') value = value * 10 + (raw[i++] - '0');
    if (i < raw.size() && raw[i] == '.') {
        double scale = 0.1;
        for (++i; i < raw.size() && raw[i] >= '0' && raw[i] <= '9'; ++i, scale *= 0.1) value += (raw[i] - '0') * scale;
    }
    if (i < raw.size() && (raw[i] == 'e' || raw[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < raw.size() && (raw[i] == '+' || raw[i] == '-')) eneg = raw[i++] == '-';
        int exp = 0;
        while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9' && exp < 400) exp = exp * 10 + (raw[i++] - '0');
        value *= std::pow(10.0, eneg ? -exp : exp);
    }
    out = neg ? -value : value;
    return true;
}

bool JsonBool(std::string_view raw, bool& out) {
    if (raw == "true") return out = true, true;
    if (raw == "false") return out = false, true;
    return false;
}

bool JsonString(std::string_view raw, std::string& out) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return false;
    out.clear();
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i + 1 >= raw.size()) break;
        switch (raw[i]) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': out += '?'; i += 4; break;  // non-ASCII not needed for codes/ids
            default: out += raw[i];
        }
    }
    return true;
}

}  // namespace ptf::detail
