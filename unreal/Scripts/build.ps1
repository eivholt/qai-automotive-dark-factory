[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [switch]$SkipBuild,
    [switch]$SkipContent
)
$ErrorActionPreference = 'Stop'
$Project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\DarkFactory.uproject'))
if (-not $SkipBuild) {
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') DarkFactoryEditor Win64 Development $Project -WaitMutex
    if ($LASTEXITCODE -ne 0) { throw "Unreal build failed: $LASTEXITCODE" }
}
if (-not $SkipContent) {
    $Generator = Join-Path $PSScriptRoot 'setup_content.py'
    & (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $Project '-run=pythonscript' "-script=$Generator" -Unattended -NullRHI -nothreading -NoSound
    if ($LASTEXITCODE -ne 0) { throw "Content generation failed: $LASTEXITCODE" }
}
Write-Host 'Ready. Run Scripts/run.ps1 to open the interactive simulation.'
