[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Endpoint = 'http://127.0.0.1:18190/inspect',
    [int]$Seed = 42,
    [ValidateRange(0,20000)][float]$RoofLightLumens = 80,
    [switch]$NoInference,
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
$Project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\DarkFactory.uproject'))
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$Arguments = @($Project, '-game', '-windowed', '-ResX=1600', '-ResY=900', '-NoSplash', "-DFEndpoint=$Endpoint", "-DFSeed=$Seed", "-DFRoofLightLumens=$RoofLightLumens")
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


