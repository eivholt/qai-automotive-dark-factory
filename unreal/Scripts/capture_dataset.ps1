[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Plan,
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)
$ErrorActionPreference = 'Stop'
$PlanPath = [IO.Path]::GetFullPath($Plan)
$OutputPath = [IO.Path]::GetFullPath($Output)
if (-not (Test-Path -LiteralPath $PlanPath -PathType Leaf)) { throw "Missing capture plan: $PlanPath" }
if (Test-Path -LiteralPath (Join-Path $OutputPath 'manifest.jsonl')) { throw 'Output already contains manifest.jsonl. Choose a fresh dataset directory.' }
& (Join-Path $PSScriptRoot 'run.ps1') -EngineRoot $EngineRoot -Plan $PlanPath -Output $OutputPath -NoInference
if (-not (Test-Path -LiteralPath (Join-Path $OutputPath 'manifest.jsonl'))) { throw 'Renderer exited without a captured manifest.' }
Write-Host "Capture complete: $OutputPath\manifest.jsonl"
Write-Host 'Next run scripts/training/prepare.py validate with --manifest and --dataset-root before freezing the dataset (image checks are on by default).'
