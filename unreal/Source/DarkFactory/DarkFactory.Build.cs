using UnrealBuildTool;
public class DarkFactory : ModuleRules {
    public DarkFactory(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // Keep each implementation's anonymous helper namespace isolated.
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "HTTP", "Json", "JsonUtilities", "RenderCore", "RHI", "PhysicsCore", "AudioMixer", "MeshDescription", "StaticMeshDescription"
        });
    }
}
