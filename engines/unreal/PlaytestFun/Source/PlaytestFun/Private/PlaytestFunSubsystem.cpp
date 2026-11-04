#include "PlaytestFunSubsystem.h"

#include "GeneralProjectSettings.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "OnlineSubsystem.h"
#include "PlaytestFunSettings.h"

#include "playtestfun/playtestfun.hpp"

DEFINE_LOG_CATEGORY_STATIC(LogPlaytestFun, Log, All);

namespace
{
std::string ToUtf8(const FString& S)
{
    return std::string(TCHAR_TO_UTF8(*S));
}
}

void UPlaytestFunSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const UPlaytestFunSettings* Settings = GetDefault<UPlaytestFunSettings>();
    if (Settings->GameKey.IsEmpty())
    {
        UE_LOG(LogPlaytestFun, Warning, TEXT("No game key set in Project Settings > Plugins > PLAYTEST.FUN"));
        return;
    }

    ptf::Config Cfg;
    Cfg.game_key = ToUtf8(Settings->GameKey);
    Cfg.playtest_id = ToUtf8(Settings->PlaytestId);
    Cfg.build_version = ToUtf8(Settings->BuildVersion.IsEmpty() ? GetDefault<UGeneralProjectSettings>()->ProjectVersion
                                                                 : Settings->BuildVersion);
    Cfg.heartbeat_seconds = static_cast<uint32_t>(FMath::Max(10, Settings->HeartbeatSeconds));
    Cfg.storage_dir = ToUtf8(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PlaytestFun")));
    Cfg.open_url_on_shutdown = false;  // LaunchURL below covers every platform
    Cfg.log = [](ptf::LogLevel Level, const std::string& Msg)
    {
        const FString Text = UTF8_TO_TCHAR(Msg.c_str());
        if (Level >= ptf::LogLevel::Error) UE_LOG(LogPlaytestFun, Error, TEXT("%s"), *Text);
        else if (Level == ptf::LogLevel::Warn) UE_LOG(LogPlaytestFun, Warning, TEXT("%s"), *Text);
        else UE_LOG(LogPlaytestFun, Verbose, TEXT("%s"), *Text);
    };
    Client = new ptf::Client(MoveTemp(Cfg));

    BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddWeakLambda(this, [this]
    {
        if (Client) Client->SetPaused(true);
    });
    ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddWeakLambda(this, [this]
    {
        if (Client) Client->SetPaused(false);
    });

    if (Settings->bAutoSteamId && !TrySteamIdFromOnlineSubsystem())
        UE_LOG(LogPlaytestFun, Log, TEXT("Steam not ready; call SetSteamId once it is"));
}

void UPlaytestFunSubsystem::Deinitialize()
{
    const UPlaytestFunSettings* Settings = GetDefault<UPlaytestFunSettings>();
    Finish(Settings->bOpenSiteOnExit && (!GIsEditor || Settings->bOpenSiteInEditor));
    Super::Deinitialize();
}

void UPlaytestFunSubsystem::Finish(bool bOpenSite)
{
    FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
    FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
    if (!Client) return;

    Client->Shutdown();  // bounded final send
    delete Client;
    Client = nullptr;

    if (bOpenSite)
    {
        const FString Url = GetDefault<UPlaytestFunSettings>()->ExitUrl;
        if (Url.StartsWith(TEXT("https://")) || Url.StartsWith(TEXT("http://")))
            FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
    }
}

bool UPlaytestFunSubsystem::SetSteamId(const FString& SteamId64)
{
    return Client && Client->SetSteamId(ToUtf8(SteamId64));
}

bool UPlaytestFunSubsystem::TrySteamIdFromOnlineSubsystem()
{
    IOnlineSubsystem* Oss = IOnlineSubsystem::Get(STEAM_SUBSYSTEM);
    if (!Oss) return false;
    IOnlineIdentityPtr Identity = Oss->GetIdentityInterface();
    if (!Identity.IsValid()) return false;
    FUniqueNetIdPtr Id = Identity->GetUniquePlayerId(0);
    return Id.IsValid() && SetSteamId(Id->ToString());
}

bool UPlaytestFunSubsystem::SetSessionData(const FString& JsonObject)
{
    return Client && Client->SetSessionData(ToUtf8(JsonObject));
}

bool UPlaytestFunSubsystem::GoalDone(const FString& Goal)
{
    return Client && Client->Goal(ToUtf8(Goal));
}

bool UPlaytestFunSubsystem::GoalProgress(const FString& Goal, float Value)
{
    return Client && Client->Goal(ToUtf8(Goal), Value);
}

bool UPlaytestFunSubsystem::TestResult(int32 TestId, bool bPassed, const FString& Message)
{
    return Client && Client->TestResult(TestId, bPassed, ToUtf8(Message));
}

bool UPlaytestFunSubsystem::Crash(const FString& Fingerprint, const FString& Message, const FString& Stack)
{
    ptf::JsonObject Hardware;
    Hardware.Set("gpu", ToUtf8(FPlatformMisc::GetPrimaryGPUBrand()))
        .Set("cpu", ToUtf8(FPlatformMisc::GetCPUBrand()))
        .Set("ramGb", static_cast<int>(FPlatformMemory::GetConstants().TotalPhysicalGB));
    return Client && Client->Crash(ToUtf8(Fingerprint), ToUtf8(Message), ToUtf8(Stack), Hardware);
}

bool UPlaytestFunSubsystem::SendEvent(const FString& EventType, const FString& Status, const FString& DataJson)
{
    ptf::JsonObject Data;
    return Client && (DataJson.IsEmpty() || ptf::JsonObject::FromRaw(ToUtf8(DataJson), Data)) &&
           Client->Event(ToUtf8(EventType), ToUtf8(Status), Data);
}

void UPlaytestFunSubsystem::FlushNow()
{
    if (Client) Client->Flush();
}

FString UPlaytestFunSubsystem::GetSessionId() const
{
    return Client ? FString(UTF8_TO_TCHAR(Client->GetStats().session_id.c_str())) : FString();
}

float UPlaytestFunSubsystem::GetSessionPlaytime() const
{
    return Client ? static_cast<float>(Client->GetStats().local_session_seconds) : 0.f;
}

float UPlaytestFunSubsystem::GetTotalPlaytime() const
{
    return Client ? static_cast<float>(Client->GetStats().server_total_seconds) : 0.f;
}

int32 UPlaytestFunSubsystem::IsPlayerAccepted() const
{
    return Client ? Client->GetStats().player_accepted : -1;
}
