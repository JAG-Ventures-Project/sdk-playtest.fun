// Opens a URL in the default browser without blocking or a shell.
#include "playtestfun/playtestfun.hpp"

#include "platform.hpp"

#if defined(PTF_WIN_DESKTOP)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#elif defined(PTF_MACOS) || defined(PTF_LINUX_DESKTOP)
#  include <spawn.h>
#  include <sys/types.h>
extern char** environ;
#endif

namespace ptf {

namespace {

// Only web links: never let a config value launch a local file or program.
bool IsWebUrl(const std::string& url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return false;
    for (char c : url)
        if (static_cast<unsigned char>(c) < 0x20 || c == '"') return false;
    return true;
}

#if defined(PTF_MACOS) || defined(PTF_LINUX_DESKTOP)
bool Spawn(const char* program, const std::string& url) {
    char* argv[] = {const_cast<char*>(program), const_cast<char*>(url.c_str()), nullptr};
    pid_t pid = 0;
    return posix_spawnp(&pid, program, nullptr, nullptr, argv, environ) == 0;
}
#endif

}  // namespace

bool OpenUrl(const std::string& url) {
    if (!IsWebUrl(url)) return false;
#if defined(PTF_WIN_DESKTOP)
    const int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (n <= 0) return false;
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, w.data(), n);
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return r > 32;
#elif defined(PTF_MACOS)
    return Spawn("/usr/bin/open", url);
#elif defined(PTF_LINUX_DESKTOP)
    return Spawn("xdg-open", url);
#else
    return false;  // mobile/consoles: use the engine's URL API
#endif
}

}  // namespace ptf
