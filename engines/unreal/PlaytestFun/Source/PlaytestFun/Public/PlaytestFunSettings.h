#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PlaytestFunSettings.generated.h"

// Project Settings > Plugins > PLAYTEST.FUN
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "PLAYTEST.FUN"))
class PLAYTESTFUN_API UPlaytestFunSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPlaytestFunSettings() { CategoryName = TEXT("Plugins"); }

    // phub_gk_... game key (write-only, safe to ship). Never a studio key.
    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN")
    FString GameKey;

    // Optional; needed only when a player is in more than one playtest of the game.
    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN")
    FString PlaytestId;

    // Empty = Project Settings > Description > Project Version. Prefixes the session id, tags crashes.
    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN")
    FString BuildVersion;

    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN", meta = (ClampMin = 10))
    int32 HeartbeatSeconds = 60;

    // Read the SteamID64 from OnlineSubsystemSteam at start-up.
    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN")
    bool bAutoSteamId = true;

    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN")
    bool bOpenSiteOnExit = true;

    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN")
    bool bOpenSiteInEditor = false;

    UPROPERTY(Config, EditAnywhere, Category = "PLAYTEST.FUN")
    FString ExitUrl = TEXT("https://playtest.fun");
};
