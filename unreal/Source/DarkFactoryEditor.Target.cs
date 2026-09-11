using UnrealBuildTool;
public class DarkFactoryEditorTarget : TargetRules {
    public DarkFactoryEditorTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("DarkFactory");
    }
}
