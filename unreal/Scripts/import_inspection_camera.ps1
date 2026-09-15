[CmdletBinding()]
param(
    [string]$Blender = 'blender',
    [switch]$SkipPrepare,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)
$ErrorActionPreference = 'Stop'
$CameraRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$CameraOutput = Join-Path $CameraRepo 'artifacts/asset-inspection'
[IO.Directory]::CreateDirectory($CameraOutput) | Out-Null
if (-not $SkipPrepare) {
    & $Blender --background --factory-startup --disable-autoexec --python-exit-code 1 --python (Join-Path $CameraRepo 'scripts/assets/prepare_inspection_camera.py')
    if ($LASTEXITCODE -ne 0) { throw 'Camera preparation failed.' }
}
$CameraArguments = @(
    (Join-Path $CameraRepo 'unreal/DarkFactory.uproject'),
    '-run=pythonscript', ('-script=' + (Join-Path $PSScriptRoot 'import_inspection_camera.py')),
    '-Unattended', '-NullRHI', '-nothreading', '-NoSound', '-NoSplash',
    ('-abslog=' + (Join-Path $CameraOutput 'inspection_camera_import.log'))
)
foreach ($Argument in $CameraArguments) {
    if ($Argument.Contains('"')) { throw 'Double quotes are not allowed in paths.' }
}
$CameraQuoted = ($CameraArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
$CameraProcess = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $CameraQuoted -WindowStyle Hidden -PassThru
if (-not $CameraProcess.WaitForExit(300000)) {
    Stop-Process -Id $CameraProcess.Id
    throw 'Camera import timed out.'
}
$CameraProcess.Refresh()
if ($CameraProcess.ExitCode -ne 0) { throw 'Camera import failed; inspect artifacts/asset-inspection/inspection_camera_import.log.' }
$CameraReport = Get-Content (Join-Path $CameraOutput 'inspection_camera_import.json') -Raw | ConvertFrom-Json
if (-not $CameraReport.passed) { throw 'Camera import audit failed.' }
Write-Output $CameraReport
