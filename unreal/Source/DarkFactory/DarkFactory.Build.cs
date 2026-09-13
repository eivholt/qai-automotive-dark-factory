using UnrealBuildTool;
public class DarkFactory : ModuleRules {
    public DarkFactory(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "HTTP", "Json", "JsonUtilities", "RenderCore", "RHI", "PhysicsCore", "AudioMixer"
        });
    }
}
