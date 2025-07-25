using System.IO;
using EpicGames.Core;
using UnrealBuildTool;

// ReSharper disable once InconsistentNaming
public class SocketIOClient : ModuleRules
{
    public SocketIOClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Correct include paths based on your actual structure
        PublicIncludePaths.AddRange(new string[] {
            Path.Combine(ModuleDirectory, "Public"),
            Path.Combine(ModuleDirectory, "Private"),
    
            // Only add paths that actually exist
            Path.Combine(ModuleDirectory, "../ThirdParty/asio"),  // No /include needed
            Path.Combine(ModuleDirectory, "../ThirdParty/websocketpp")
        });


        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "Json",
            "JsonUtilities",
            "SIOJson",
            "CoreUtility",
            "SocketIOLib",
            "WebSockets"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "HTTP",
            "SSL"
        });

        // Platform-specific configurations
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.Add("ws2_32.lib");
            PublicSystemLibraries.Add("crypt32.lib");  // Often needed for SSL
        }

        // Add definitions
        PublicDefinitions.AddRange(new string[] {
            "ASIO_STANDALONE",  // Important for Asio
            "RAPIDJSON_HAS_STDSTRING=1",
            "WITH_SOCKETIO=1",
            "WITH_WEBSOCKETS=1"
        });

        // Add these if you're using Opus for voice
        if (Directory.Exists(Path.Combine(ModuleDirectory, "../ThirdParty/opus")))
        {
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "../ThirdParty/opus/include"));
            
            string OpusLibPath = Path.Combine(ModuleDirectory, "../ThirdParty/opus/lib/Win64");
            if (Directory.Exists(OpusLibPath))
            {
                PublicAdditionalLibraries.Add(Path.Combine(OpusLibPath, "opus.lib"));
                RuntimeDependencies.Add(Path.Combine(OpusLibPath, "opus.dll"));
            }
        }
        
        // Add path verification
        if (!Directory.Exists(Path.Combine(ModuleDirectory, "../ThirdParty/asio")))
        {
            Log.TraceWarning("Asio directory not found at: " + Path.Combine(ModuleDirectory, "../ThirdParty/asio"));
        }

        if (!Directory.Exists(Path.Combine(ModuleDirectory, "../ThirdParty/websocketpp")))
        {
            Log.TraceWarning("Websocketpp directory not found at: " + Path.Combine(ModuleDirectory, "../ThirdParty/websocketpp"));
        }
    }
}