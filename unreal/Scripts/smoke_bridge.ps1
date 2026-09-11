[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('G','A','R','ERROR')][string]$Expected,
    [string]$Endpoint = 'http://127.0.0.1:18190/inspect',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'run.ps1') -EngineRoot $EngineRoot -Endpoint $Endpoint -BridgeSmoke -Expected $Expected
$ReportPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Saved\Smoke\bridge_$Expected.json"))
$Report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json
if (-not $Report.passed) { throw "Bridge smoke failed: $($Report.status)" }
Write-Host "Passed $Expected plumbing / source=$($Report.source) / travel=$($Report.travel_cm) cm"
Write-Host "Evidence: $ReportPath"
