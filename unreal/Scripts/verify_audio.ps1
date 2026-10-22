[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Output = 'artifacts/audio-native'
)
$ErrorActionPreference = 'Stop'
$Repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$Project = Join-Path $Repository 'unreal\DarkFactory.uproject'
if (-not (Test-Path -LiteralPath (Join-Path $Repository 'assets\audio\runtime\bank.dfau'))) {
    throw 'Private approved audio bank missing. Run scripts/audio/prepare_runtime_audio.py first.'
}
$ReportBase = [IO.Path]::GetFullPath((Join-Path $Repository $Output))
$ReportRoot = Join-Path $ReportBase ('run-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $ReportRoot | Out-Null
$AudioArguments = @("`"$Project`"", '-game', '-windowed', '-ForceRes', '-ResX=1280', '-ResY=720',
    '-RenderOffscreen', '-NoSplash', '-novsync', '-DFProcess', '-DFNoInference', '-DFAudioSmoke',
    '-DFEnhancements', "-DFOutput=`"$ReportRoot`"", "-abslog=`"$ReportRoot\engine.log`"", '-ExecCmds="t.MaxFPS 60"')
$AudioProcess = Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $AudioArguments -PassThru -WindowStyle Hidden
if (-not $AudioProcess.WaitForExit(120000)) {
    Stop-Process -Id $AudioProcess.Id
    throw "Audio fixture timed out; inspect $ReportRoot\engine.log"
}
if ($AudioProcess.ExitCode -ne 0) { throw "Audio fixture failed ($($AudioProcess.ExitCode)); inspect $ReportRoot\engine.log" }
$Report = Get-Content -LiteralPath (Join-Path $ReportRoot 'audio.json') -Raw | ConvertFrom-Json
if (-not $Report.passed -or -not $Report.engine_mix_saved -or $Report.maximum_sources -ne 6 -or $Report.sources.Count -ne 6) { throw 'Native audio report failed.' }
Write-Host "Native six-source audio test passed. Engine mixer recording: $ReportRoot\engine_audio.wav"
