[CmdletBinding()]
param(
    [ValidateSet('raw', 'prepared')][string]$Mode = 'prepared',
    [string]$Manifest,
    [string]$Source,
    [switch]$SkipMeshImport,
    [switch]$SkipMaterialBuild,
    [string]$Report,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(30, 3600)][int]$TimeoutSeconds = 900
)
$ErrorActionPreference = 'Stop'
$Repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Project = Join-Path $Repo 'unreal/DarkFactory.uproject'
$Script = Join-Path $PSScriptRoot 'import_welding_cell.py'
if (-not $Report) { $Report = Join-Path $Repo "artifacts/asset-inspection/welding_${Mode}_import.json" }
$Report = [IO.Path]::GetFullPath($Report)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Report)) | Out-Null
$Log = [IO.Path]::ChangeExtension($Report, '.log')
$Arguments = @($Project, '-run=pythonscript', "-script=$Script", '-Unattended', '-NullRHI', '-nothreading', '-NoSound', '-NoSplash', "-DFImportMode=$Mode", "-DFImportReport=$Report", "-abslog=$Log")
if ($SkipMaterialBuild) { $Arguments += '-DFImportSkipMaterials=1' }
if ($SkipMeshImport) { $Arguments += '-DFImportSkipMesh=1' }
if ($Manifest) { $Arguments += '-DFImportManifest=' + [IO.Path]::GetFullPath($Manifest) }
if ($Source) { $Arguments += '-DFImportSource=' + [IO.Path]::GetFullPath($Source) }
foreach ($Argument in $Arguments) { if ($Argument.Contains('"')) { throw 'Double quotes are not allowed in importer paths.' } }
$QuotedArguments = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
$Process = Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $QuotedArguments -WindowStyle Hidden -PassThru
Write-Host "Unreal asset import PID $($Process.Id); log $Log"
if (-not $Process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $Process.Id -Force
    throw "Asset import timed out after $TimeoutSeconds seconds; stopped PID $($Process.Id)."
}
$Process.Refresh()
$ExitCode = $Process.ExitCode
$ProcessResult = @{ process_id = $Process.Id; exit_code = $ExitCode; log = $Log; report = $Report }
$ProcessResult | ConvertTo-Json | Set-Content -LiteralPath ([IO.Path]::ChangeExtension($Report, '.process.json')) -Encoding utf8
if ($ExitCode -ne 0) { throw "Asset import exited $ExitCode; inspect $Log and $Report" }
$Result = Get-Content -LiteralPath $Report -Raw | ConvertFrom-Json
if ($Result.status -ne 'imported_and_audited' -or -not $Result.source_files_unchanged) { throw "Import audit did not pass: $Report" }
Write-Host "Imported $($Result.counts.meshes) meshes, $($Result.counts.materials) materials, $($Result.counts.textures) textures. Report $Report"