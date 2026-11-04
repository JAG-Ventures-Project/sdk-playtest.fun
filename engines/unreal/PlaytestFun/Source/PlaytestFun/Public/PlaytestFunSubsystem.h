#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlaytestFunSubsystem.generated.h"

namespace ptf { class Client; }

// Lives as long as the GameInstance: starts the session on init, final send + site on exit.
UCLASS()
class PLAYTESTFUN_API UPlaytestFunSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // 17-digit SteamID64. Nothing is sent until this is known.
    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN")
    bool SetSteamId(const FString& SteamId64);

    // Reads the id from OnlineSubsystemSteam; false if Steam isn't logged in yet.
    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN")
    bool TrySteamIdFromOnlineSubsystem();

    // Free-form JSON object (max 16 KB), e.g. {"level":3}.
    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN")
    bool SetSessionData(const FString& JsonObject);

    // Events: queued on disk, retried until delivered.

    // Goal without a target: done.
    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN|Events")
    bool GoalDone(const FString& Goal);

    // Goal with a target. Safe to call every frame: pending values collapse to the highest.
    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN|Events")
    bool GoalProgress(const FString& Goal, float Value);

    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN|Events")
    bool TestResult(int32 TestId, bool bPassed, const FString& Message);

    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN|Events")
    bool Crash(const FString& Fingerprint, const FString& Message, const FString& Stack);

    // Any event type; DataJson is a JSON object.
    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN|Events")
    bool SendEvent(const FString& EventType, const FString& Status, const FString& DataJson);

    UFUNCTION(BlueprintCallable, Category = "PLAYTEST.FUN")
    void FlushNow();

    UFUNCTION(BlueprintPure, Category = "PLAYTEST.FUN")
    FString GetSessionId() const;

    UFUNCTION(BlueprintPure, Category = "PLAYTEST.FUN")
    float GetSessionPlaytime() const;

    // Player's total in this playtest, from the last server reply.
    UFUNCTION(BlueprintPure, Category = "PLAYTEST.FUN")
    float GetTotalPlaytime() const;

    // -1 unknown, 0 no, 1 yes.
    UFUNCTION(BlueprintPure, Category = "PLAYTEST.FUN")
    int32 IsPlayerAccepted() const;

private:
    void Finish(bool bOpenSite);

    ptf::Client* Client = nullptr;
    FDelegateHandle BackgroundHandle;
    FDelegateHandle ForegroundHandle;
};
