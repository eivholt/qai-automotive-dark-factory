[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Source = '',
    [string]$Output = '',
    [string]$Python = 'python',
    [switch]$UseWSL,
    [string]$WslDistribution = 'Ubuntu',
    [string]$WslPython = 'python3',
    [switch]$SkipBuild,
    [switch]$SkipScan,
    [switch]$ScanOnly
)
$ErrorActionPreference = 'Stop'
$Repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$AssetRoot = [IO.Path]::GetFullPath((Join-Path $Repo 'assets'))
if (-not $Source) { $Source = Join-Path $AssetRoot 'Autodesk_FBX_2009/Car_Body_Welding_Robotics_Cell.FBX' }
if (-not $Output) { $Output = Join-Path $AssetRoot 'derived/welding_cell' }
$Source = [IO.Path]::GetFullPath($Source)
$Output = [IO.Path]::GetFullPath($Output)
if (-not $Output.StartsWith($AssetRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Derived output must remain inside ignored assets/.' }
if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Source FBX not found: $Source" }
$Prepared = Join-Path $Output 'prepared.fbx'
if ($Source.Equals($Prepared, [StringComparison]::OrdinalIgnoreCase)) { throw 'Source must not be the prepared output.' }
$SourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
$BuildDir = Join-Path $Repo 'artifacts/asset-tools'
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$SDK = Join-Path $EngineRoot 'Engine/Source/ThirdParty/FBX/2020.2'
$RuntimeDir = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/FBX/2020.2/Win64'
$Tool = Join-Path $BuildDir 'fbx_prepare.exe'
$OriginalPath = $env:PATH
$OriginalLocation = Get-Location
try {
    Set-Location -LiteralPath $Repo
    if (-not $SkipBuild) {
        $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $VS = & $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $VS) { throw 'MSVC C++ build tools were not found.' }
        $VcVars = Join-Path $VS 'VC/Auxiliary/Build/vcvars64.bat'
        $Compile = @"
@echo off
call "$VcVars" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /O2 /std:c++17 /MD /DFBXSDK_SHARED /I"$SDK/include" scripts\assets\fbx_prepare.cpp /Foartifacts\asset-tools\fbx_prepare.obj /Feartifacts\asset-tools\fbx_prepare.exe /link "$SDK/lib/vs2017/x64/release/libfbxsdk.lib"
"@
        $Batch = Join-Path $BuildDir 'build_fbx_tool.cmd'
        [IO.File]::WriteAllText($Batch, $Compile, [Text.UTF8Encoding]::new($false))
        & $Batch
        if ($LASTEXITCODE -ne 0) { throw "FBX tool compilation failed: $LASTEXITCODE" }
    }
    if (-not (Test-Path -LiteralPath $Tool)) { throw 'Build the FBX preparation tool first.' }
    $env:PATH = $RuntimeDir + ';' + $OriginalPath
    if (-not $SkipScan) {
        & $Tool scan $Source (Join-Path $Output 'scan')
        if ($LASTEXITCODE -ne 0) { throw "FBX scan failed: $LASTEXITCODE" }
        $Provenance = @{ version=1; source=$Source; source_sha256=$SourceHash; tool_sha256=(Get-FileHash -LiteralPath $Tool -Algorithm SHA256).Hash }
        [IO.File]::WriteAllText((Join-Path $Output 'scan/source.json'), ($Provenance | ConvertTo-Json) + "`n", [Text.UTF8Encoding]::new($false))
    } else {
        $ProvenancePath = Join-Path $Output 'scan/source.json'
        if (-not (Test-Path -LiteralPath $ProvenancePath)) { throw 'Scan provenance is missing. Run once without -SkipScan.' }
        $Provenance = Get-Content -LiteralPath $ProvenancePath -Raw | ConvertFrom-Json
        if ($Provenance.source_sha256 -ne $SourceHash -or $Provenance.tool_sha256 -ne (Get-FileHash -LiteralPath $Tool -Algorithm SHA256).Hash) { throw 'Scan source or preparation tool changed. Run without -SkipScan.' }
    }
    if (-not $ScanOnly) {
        if ($UseWSL) {
            $LinuxRepo = (& wsl.exe -d $WslDistribution -- wslpath -a ($Repo.Replace('\','/'))).Trim()
            $LinuxOutput = (& wsl.exe -d $WslDistribution -- wslpath -a ($Output.Replace('\','/'))).Trim()
            & wsl.exe -d $WslDistribution --cd $LinuxRepo -- $WslPython scripts/assets/reconstruct_robot_joints.py --output $LinuxOutput
            if ($LASTEXITCODE -ne 0) { throw 'Joint reconstruction failed.' }
            & wsl.exe -d $WslDistribution --cd $LinuxRepo -- $WslPython scripts/assets/prepare_welding_cell.py --output $LinuxOutput
        } else {
            & $Python scripts/assets/reconstruct_robot_joints.py --output $Output
            if ($LASTEXITCODE -ne 0) { throw 'Joint reconstruction failed.' }
            & $Python scripts/assets/prepare_welding_cell.py --output $Output
        }
        if ($LASTEXITCODE -ne 0) { throw 'Island grouping failed.' }
        if ($UseWSL) {
            & wsl.exe -d $WslDistribution --cd $LinuxRepo -- $WslPython scripts/assets/prepare_control_cabinets.py --root $LinuxOutput
        } else {
            & $Python scripts/assets/prepare_control_cabinets.py --root $Output
        }
        if ($LASTEXITCODE -ne 0) { throw 'Control cabinet placement preparation failed.' }
        # Regroup only derived islands before either FBX export, then solve the
        # process poses from the same custom output root and preserved source scan.
        foreach ($PanelGenerator in @('prepare_panel_assembly.py', 'prepare_pinch_welder.py', 'prepare_panel_motion.py', 'prepare_panel_frame.py', 'prepare_gripper_rack_bar.py')) {
            if ($UseWSL) {
                & wsl.exe -d $WslDistribution --cd $LinuxRepo -- $WslPython ("scripts/assets/" + $PanelGenerator) --root $LinuxOutput
            } else {
                & $Python (Join-Path $PSScriptRoot $PanelGenerator) --root $Output
            }
            if ($LASTEXITCODE -ne 0) { throw "Panel preparation failed ($PanelGenerator): $LASTEXITCODE" }
        }
        & $Tool group $Source (Join-Path $Output 'assignments.tsv') $Prepared
        if ($LASTEXITCODE -ne 0) { throw "Grouped FBX export failed: $LASTEXITCODE" }
        & $Tool group-subset $Source (Join-Path $Output 'assignments.tsv') (Join-Path $Output 'holder_delta.fbx') (Join-Path $Output 'holder_delta_groups.txt')
        if ($LASTEXITCODE -ne 0) { throw "Holder delta export failed: $LASTEXITCODE" }
        & $Tool group-subset $Source (Join-Path $Output 'assignments.tsv') (Join-Path $Output 'panel_delta.fbx') (Join-Path $Output 'panel_delta_groups.txt')
        if ($LASTEXITCODE -ne 0) { throw "Panel delta export failed: $LASTEXITCODE" }
        & $Tool group-subset $Source (Join-Path $Output 'gripper_bar_assignments.tsv') (Join-Path $Output 'gripper_bar.fbx') (Join-Path $Output 'gripper_bar_groups.txt')
        if ($LASTEXITCODE -ne 0) { throw "Gripper rack-bar copy export failed: $LASTEXITCODE" }
        & $Tool group-subset $Source (Join-Path $Output 'pinch_gun_assignments.tsv') (Join-Path $Output 'pinch_gun.fbx') (Join-Path $Output 'pinch_gun_groups.txt')
        if ($LASTEXITCODE -ne 0) { throw "Pinch gun copy export failed: $LASTEXITCODE" }
        Write-Host "Prepared local scene: $(Join-Path $Output 'scene.json')"
        Write-Host 'Next: unreal/scripts/import_welding_cell.ps1 -Mode prepared -Manifest assets/derived/welding_cell/manifest.json'
    }
} finally {
    $env:PATH = $OriginalPath
    Set-Location -LiteralPath $OriginalLocation
    if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne $SourceHash) { throw 'Original source FBX changed unexpectedly.' }
}
