[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Endpoint = 'http://127.0.0.1:18190/inspect',
    [int]$Seed = 42,
    [ValidateRange(0,20000)][float]$RoofLightLumens = 80,
    [switch]$NoInference,
    [ValidateSet('auto','procedural','welding')][string]$Scene = 'auto',
    [string]$SceneManifest = '',
    [ValidateRange(0,20000)][float]$WeldingRoofLightLumens = 80,
    [switch]$WeldingSmoke,
    [switch]$RealismSmoke,
    [switch]$ColliderSmoke,
    [switch]$CabinetSmoke,
    [switch]$Smoke,
    [switch]$NightVisionSmoke,
    [switch]$ThermalSmoke,
    [switch]$ModalityScreenshots,
    [switch]$ReadbackSmoke,
    [switch]$NightVisionQuality,
    [switch]$BridgeSmoke,
    [ValidateSet('G','A','R','ERROR')][string]$Expected = 'G',
    [int]$Dataset = 0,
    [string]$Plan = '',
    [string]$Output = ''
)
$ErrorActionPreference = 'Stop'
if (([int]$RealismSmoke.IsPresent + [int]$ColliderSmoke.IsPresent + [int]$CabinetSmoke.IsPresent) -gt 1) { throw 'Select one feature smoke check' }
if ($RealismSmoke -or $ColliderSmoke -or $CabinetSmoke) { $WeldingSmoke = $true }
$Project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\DarkFactory.uproject'))
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$Arguments = @($Project, '-game', '-windowed', '-ResX=1600', '-ResY=900', '-NoSplash', "-DFEndpoint=$Endpoint", "-DFSeed=$Seed", "-DFRoofLightLumens=$RoofLightLumens")
if ($WeldingSmoke -and ($Smoke -or $NightVisionSmoke -or $ThermalSmoke -or $ModalityScreenshots -or $ReadbackSmoke -or $BridgeSmoke -or $Dataset -gt 0 -or $Plan)) { throw 'WeldingSmoke cannot use procedural smoke or training flags' }
$Arguments += "-DFScene=$Scene"
$Arguments += "-DFWeldingRoofLightLumens=$WeldingRoofLightLumens"
if ($SceneManifest) { $Arguments += "-DFSceneManifest=$([IO.Path]::GetFullPath($SceneManifest))" }
if ($WeldingSmoke) { $Arguments += '-DFWeldingSmoke' }
if ($RealismSmoke) { $Arguments += '-DFRealismSmoke' }
if ($ColliderSmoke) { $Arguments += '-DFColliderSmoke' }
if ($CabinetSmoke) { $Arguments += '-DFCabinetSmoke' }
if ($NoInference) { $Arguments += '-DFNoInference' }
if ($Smoke) { $Arguments += '-DFSmoke' }
if ($ModalityScreenshots) { $Arguments += '-DFModalityScreenshots' }
if ($ThermalSmoke) { $Arguments += '-DFThermalSmoke' }
if ($NightVisionSmoke) { $Arguments += '-DFNightVisionSmoke' }
if ($ReadbackSmoke) { $Arguments += '-DFReadbackSmoke' }
if ($NightVisionQuality) { $Arguments += '-DFNVQuality' }
if ($BridgeSmoke) { $Arguments += '-DFBridgeSmoke'; $Arguments += "-DFExpected=$Expected" }
if ($Dataset -gt 0) { $Arguments += "-DFDataset=$Dataset" }
if ($Plan) { $Arguments += "-DFPlan=$([IO.Path]::GetFullPath($Plan))" }
if ($Output) { $Arguments += "-DFOutput=$([IO.Path]::GetFullPath($Output))" }
& $Exe @Arguments | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Simulation exited with code $LASTEXITCODE" }


