// Picks the platform transport.
#include "playtestfun/transport.hpp"

#include "platform.hpp"

namespace ptf {

namespace detail {
std::shared_ptr<Transport> CreateWinHttpTransport();
std::shared_ptr<Transport> CreateAppleTransport();
std::shared_ptr<Transport> CreateCurlTransport();
}  // namespace detail

std::shared_ptr<Transport> CreateDefaultTransport() {
#if defined(PTF_WIN_DESKTOP) && !defined(PTF_NO_WINHTTP)
    return detail::CreateWinHttpTransport();
#elif defined(__APPLE__) && !defined(PTF_NO_NSURLSESSION)
    return detail::CreateAppleTransport();
#elif defined(PTF_USE_CURL)
    return detail::CreateCurlTransport();
#else
    return nullptr;  // consoles, Android: inject Config::transport
#endif
}

}  // namespace ptf
