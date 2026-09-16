[CmdletBinding()]
param(
    [ValidateSet('Graphics','Process','Motion','Realism','ThermalReflections','Policy','EquipmentHeat','IQ9Live')][string]$Mode='Graphics',
    [Parameter(Mandatory=$true)][string]$Output,
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$runOutput=[IO.Path]::GetFullPath((Join-Path $repoRoot $Output))
if (Test-Path -LiteralPath $runOutput) { throw 'Choose a fresh output directory.' }
[IO.Directory]::CreateDirectory($runOutput) | Out-Null
$runArgs=@(('"'+(Join-Path $repoRoot 'unreal/DarkFactory.uproject')+'"'),'-game','-windowed','-ForceRes','-ResX=1600','-ResY=900','-NoSound','-NoSplash','-Unattended','-RenderOffscreen','-novsync','-DFScene=welding','-DFEnhancements','-DFProcessObserverMode=3','-ExecCmds="r.SetRes 1600x900w,t.MaxFPS 60"',('-DFOutput="'+$runOutput+'"'),('-abslog="'+(Join-Path $runOutput 'engine.log')+'"'))
switch ($Mode) {
    'IQ9Live' { $runArgs+=@('-DFProcess','-DFNoInference','-DFIQ9LiveSmoke'); $reportName='iq9_live_clock.json' }
    'EquipmentHeat' { $runArgs+=@('-DFProcess','-DFNoInference','-DFEquipmentHeatSmoke'); $reportName='equipment_heat.json' }
    'ThermalReflections' { $runArgs=$runArgs | Where-Object { $_ -ne '-DFScene=welding' -and $_ -ne '-DFEnhancements' }; $runArgs+=@('-DFScene=procedural','-DFNoInference','-DFThermalSmoke','-DFThermalReflectionSmoke'); $reportName='thermal_reflections.json' }
    'Policy' { $runArgs+=@('-DFProcess','-DFProcessPolicySmoke'); $reportName='process_policy.json' }
    'Graphics' { $runArgs+=@('-DFProcess','-DFNoInference','-DFGraphicsSmoke'); $reportName='observer_graphics.json' }
    'Process' { $runArgs+=@('-DFProcess','-DFProcessSmoke','-DFFaultRecovery','-DFInspectionReplay','-DFProcessEndpoint=http://127.0.0.1:18191/v1/inspect'); $reportName='process_runtime.json' }
    'Motion' { $runArgs+=@('-DFProcess','-DFNoInference','-DFPanelMotionSmoke'); $reportName='panel_motion.json' }
    'Realism' { $runArgs+=@('-DFNoInference','-DFRealismSmoke'); $reportName='realism.json' }
}
$runProcess=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $runArgs -WindowStyle Hidden -PassThru
$runProcess.Id | Set-Content -LiteralPath (Join-Path $runOutput 'engine.pid')
$runProcess.WaitForExit()
if ($Mode -eq 'Realism') { Copy-Item -LiteralPath (Join-Path $runOutput 'engine.log') -Destination (Join-Path $runOutput 'runtime.log') }
if ($runProcess.ExitCode -ne 0) { throw "Unreal exited with $($runProcess.ExitCode). See $runOutput" }
$report=Get-Content -LiteralPath (Join-Path $runOutput $reportName) -Raw | ConvertFrom-Json
if (!$report.passed) { throw "Native $Mode failed. See $runOutput" }
if ($Mode -eq 'Process') {
    $recoveries=@($report.events | Where-Object { $_.event -eq 'model_rejections_activated_local_cooling' })
    if (!$recoveries.Count -or $report.completed_bodies -ne 3 -or !$report.thermal_limits_respected -or $report.ground_truth_fields_sent_to_model) {
        throw 'Recovery integration did not demonstrate correction, three accepted bodies and isolated model inputs.'
    }
}
Write-Host "$Mode native smoke passed: $runOutput"
