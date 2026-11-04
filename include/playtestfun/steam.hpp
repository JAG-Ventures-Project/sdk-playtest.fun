// Optional helper for custom engines that link the Steamworks SDK.
// Include after <steam/steam_api.h>, once SteamAPI_Init() succeeded.
#pragma once

#include "playtestfun.hpp"

#if defined(__has_include)
#  if __has_include(<steam/steam_api.h>)
#    define PTF_HAS_STEAM_API_HEADER
#  endif
#else
#  define PTF_HAS_STEAM_API_HEADER
#endif

#ifdef PTF_HAS_STEAM_API_HEADER
#include <steam/steam_api.h>

namespace ptf {

inline bool SetSteamIdFromSteamworks(Client& client) {
    ISteamUser* user = SteamUser();
    if (!user || !user->BLoggedOn()) return false;
    return client.SetSteamId(static_cast<uint64_t>(user->GetSteamID().ConvertToUint64()));
}

}  // namespace ptf
#endif
