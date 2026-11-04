using System.IO;
using UnrealBuildTool;

public class PlaytestFun : ModuleRules
{
    public PlaytestFun(ReadOnlyTargetRules Target) : base(Target)
    {
        // Core sources are plain C++: keep them out of the UE PCH and unity blobs.
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "DeveloperSettings" });
        PrivateDependencyModuleNames.AddRange(new[] { "OnlineSubsystem" });

        // Filled by: cmake -P tools/sync_unreal.cmake
        string CoreDir = Path.Combine(ModuleDirectory, "Private", "Core");
        PrivateIncludePaths.Add(Path.Combine(CoreDir, "include"));
        PrivateIncludePaths.Add(Path.Combine(CoreDir, "src"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.AddRange(new[] { "winhttp.lib", "shell32.lib" });
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicFrameworks.Add("Foundation");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            // Engine-bundled libcurl. Android/consoles: pass a custom ptf::Transport.
            AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
            PrivateDefinitions.Add("PTF_USE_CURL=1");
        }
    }
}
