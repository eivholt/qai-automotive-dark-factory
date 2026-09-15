using UnrealBuildTool;
using System.IO;
public class DarkFactory : ModuleRules {
    public DarkFactory(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // Keep each implementation's anonymous helper namespace isolated.
        PrivateDependencyModuleNames.Add("Renderer");
        bool HasDLSS = Target.Platform == UnrealTargetPlatform.Win64 && File.Exists(Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Plugins/NVIDIA/DLSS/DLSS.uplugin")));
        PublicDefinitions.Add("WITH_DF_DLSS=" + (HasDLSS ? "1" : "0"));
        if (HasDLSS) PrivateDependencyModuleNames.Add("DLSSBlueprint");
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "HTTP", "Json", "JsonUtilities", "RenderCore", "RHI", "PhysicsCore", "AudioMixer", "MeshDescription", "StaticMeshDescription"
        });
    }
}
