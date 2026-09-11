using UnrealBuildTool;
public class DarkFactoryTarget : TargetRules {
    public DarkFactoryTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("DarkFactory");
    }
}
