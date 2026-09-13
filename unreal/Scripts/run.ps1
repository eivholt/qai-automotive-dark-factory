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
    [switch]$CurtainSmoke,
    [switch]$Process,
    [switch]$ProcessCapture,
    [switch]$ProcessSmoke,
    [switch]$ProcessPolicySmoke,
    [ValidateRange(0,1024)][int]$ProcessEpisodes = 0,
    [int]$ProcessDatasetSeed = 701,
    [ValidateSet('overview','cabinet','inspection')][string]$ProcessView = 'overview',
    [ValidateRange(0,3)][int]$ProcessObserverMode = 0,
    [string]$ProcessEndpoint = 'http://127.0.0.1:18191/v1/inspect',
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
function Get-DarkFactoryProcessOutput {
    param([string]$RequestedOutput, [string]$ProjectPath)
    if ($RequestedOutput) { return [IO.Path]::GetFullPath($RequestedOutput) }
    return [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($ProjectPath)) '..\artifacts\process-live'))
}

function Assert-DarkFactoryProcessReport {
    param(
        [Parameter(Mandatory)][string]$ReportPath,
        [Parameter(Mandatory)][ValidateSet('model','policy')][string]$Mode,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{32}$')][string]$SessionId
    )
    $Require = {
        param([bool]$Condition, [string]$Reason)
        if (-not $Condition) { throw "Process $Mode smoke result rejected: $Reason. Report: $ReportPath" }
    }
    $FiniteNumber = {
        param($Value)
        return (($Value -is [int] -or $Value -is [long] -or $Value -is [double] -or $Value -is [decimal]) -and
            -not [double]::IsNaN([double]$Value) -and -not [double]::IsInfinity([double]$Value))
    }
    $EqualNumber = {
        param($Value, [double]$Expected)
        return ((& $FiniteNumber $Value) -and [double]$Value -eq $Expected)
    }
    & $Require (Test-Path -LiteralPath $ReportPath -PathType Leaf) 'native report is missing'
    $ReportFile = Get-Item -LiteralPath $ReportPath -ErrorAction Stop
    & $Require ($ReportFile.Length -gt 0 -and $ReportFile.Length -le 8388608) 'native report is empty or exceeds the report size limit'
    try { $Report = Get-Content -LiteralPath $ReportPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Process $Mode smoke report is unreadable JSON: $ReportPath. $($_.Exception.Message)" }
    & $Require ($Report -is [pscustomobject]) 'native report must be a JSON object'
    & $Require ($Report.session_id -is [string] -and $Report.session_id -ceq $SessionId) 'session_id does not match this launch (stale report or native binary needs rebuilding)'
    & $Require ($Report.passed -is [bool] -and $Report.passed) 'native checks did not pass'
    if ($Mode -eq 'policy') {
        & $Require ($Report.source -ceq 'policy_test' -and $Report.schema -ceq 'darkfactory-process-policy-smoke-v1') 'wrong native report mode or schema'
        & $Require ($Report.model_accuracy_evidence -is [bool] -and -not $Report.model_accuracy_evidence) 'policy checks must not claim model accuracy'
        & $Require (& $EqualNumber $Report.http_requests_submitted 0) 'policy fixture submitted model requests'
        & $Require ($Report.checks -is [array] -and $Report.checks.Count -gt 0) 'policy checks are missing'
        & $Require (& $EqualNumber $Report.checks_count $Report.checks.Count) 'policy check count is inconsistent'
        $Names = @{}
        foreach ($Check in $Report.checks) {
            & $Require ($Check.name -is [string] -and -not [string]::IsNullOrWhiteSpace($Check.name) -and -not $Names.ContainsKey($Check.name)) 'policy check names are missing or duplicated'
            & $Require ($Check.passed -is [bool] -and $Check.passed) "policy check failed: $($Check.name)"
            $Names[$Check.name] = $true
        }
        return
    }
    & $Require ($Report.source -ceq 'host_gpu' -and $Report.contract_version -ceq 'automotive-process-v1' -and $Report.model_smoke -is [bool] -and $Report.model_smoke) 'wrong native report mode or source'
    & $Require ((& $EqualNumber $Report.completed_bodies 3) -and (& $EqualNumber $Report.passed_bodies 2) -and (& $EqualNumber $Report.returned_bodies 1)) 'three-body fixture did not finish with two departures and one return'
    & $Require ($Report.body_outcomes_correct -is [bool] -and $Report.body_outcomes_correct -and $Report.thermal_limits_respected -is [bool] -and $Report.thermal_limits_respected) 'body destinations or thermal readiness failed'
    & $Require ((& $EqualNumber $Report.ground_truth_fields_sent_to_model 0) -and (& $EqualNumber $Report.legacy_bridge_requests 0)) 'unexpected ground-truth or legacy bridge use'
    & $Require ($Report.body_outcomes -is [array] -and $Report.body_outcomes.Count -eq 3) 'exactly three body outcomes are required'
    $Bodies = @{}
    $BodyIds = @{}
    foreach ($Outcome in $Report.body_outcomes) {
        & $Require ((& $FiniteNumber $Outcome.body_sequence) -and $Outcome.body_sequence -in @(1,2,3)) 'invalid body sequence'
        $Sequence = [int]$Outcome.body_sequence
        & $Require (-not $Bodies.ContainsKey($Sequence)) 'duplicate body outcome'
        & $Require ($Outcome.body_id -is [string] -and -not [string]::IsNullOrWhiteSpace($Outcome.body_id) -and -not $BodyIds.ContainsKey($Outcome.body_id)) 'body identities are missing or duplicated'
        $Expected = if ($Sequence -eq 2) { 'entry' } else { 'exit' }
        & $Require ($Outcome.expected_destination -ceq $Expected -and $Outcome.actual_destination -ceq $Expected -and $Outcome.correct -is [bool] -and $Outcome.correct) "body $Sequence reached the wrong destination"
        $Bodies[$Sequence] = $Outcome.body_id
        $BodyIds[$Outcome.body_id] = $true
    }
    & $Require ($Report.thermal_gate_checks -is [array] -and $Report.thermal_gate_checks.Count -eq 4) 'both healthy bodies require adhesive and weld acceptance audits'
    $ThermalKeys = @{}
    foreach ($Check in $Report.thermal_gate_checks) {
        & $Require ((& $FiniteNumber $Check.body_sequence) -and $Check.body_sequence -in @(1,3) -and $Check.stage -cin @('adhesive','weld')) 'unexpected thermal gate audit'
        $Sequence = [int]$Check.body_sequence
        $Key = "$Sequence/$($Check.stage)"
        & $Require (-not $ThermalKeys.ContainsKey($Key) -and $Check.body_id -ceq $Bodies[$Sequence]) 'thermal audit identity is duplicated or mismatched'
        $Limit = if ($Check.stage -ceq 'adhesive') { 45 } else { 55 }
        & $Require ((& $EqualNumber $Check.limit_c $Limit) -and (& $FiniteNumber $Check.maximum_apparent_c_at_acceptance) -and $Check.maximum_apparent_c_at_acceptance -lt $Limit -and $Check.ready_at_acceptance -is [bool] -and $Check.ready_at_acceptance) "thermal readiness failed at $Key"
        $ThermalKeys[$Key] = $true
    }
}

function Assert-DarkFactoryCurtainReport {
    param([Parameter(Mandatory)][string]$ReportPath,[Parameter(Mandatory)][string]$SessionId)
    if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) { throw "Curtain smoke report is missing: $ReportPath" }
    $Report = Get-Content -LiteralPath $ReportPath -Raw -Encoding utf8 | ConvertFrom-Json -ErrorAction Stop
    if ($Report.session_id -cne $SessionId -or $Report.schema -cne 'darkfactory-curtain-smoke-v1' -or
        $Report.source -cne 'chaos_physics_fixture' -or $Report.passed -isnot [bool] -or -not $Report.passed -or
        $Report.model_http_requests -ne 0 -or @($Report.passes).Count -ne 4) {
        throw "Curtain smoke report failed or belongs to another launch: $ReportPath"
    }
}

$ErrorActionPreference = 'Stop'
if ($CurtainSmoke -and ($Process -or $ProcessCapture -or $ProcessSmoke -or $ProcessPolicySmoke -or $WeldingSmoke -or $RealismSmoke -or $ColliderSmoke -or $CabinetSmoke -or $Smoke -or $NightVisionSmoke -or $ThermalSmoke -or $ModalityScreenshots -or $ReadbackSmoke -or $BridgeSmoke -or $Dataset -gt 0 -or $Plan)) { throw 'CurtainSmoke is a separate Chaos physics fixture; do not combine it with other automation or process mode.' }
if ($CurtainSmoke) { $NoInference = $true; $Scene = 'welding' }
if (([int]$RealismSmoke.IsPresent + [int]$ColliderSmoke.IsPresent + [int]$CabinetSmoke.IsPresent) -gt 1) { throw 'Select one feature smoke check' }
if ($RealismSmoke -or $ColliderSmoke -or $CabinetSmoke) { $WeldingSmoke = $true }
if (($Process -or $ProcessCapture -or $ProcessSmoke -or $ProcessPolicySmoke) -and ($WeldingSmoke -or $Smoke -or $NightVisionSmoke -or $ThermalSmoke -or $ModalityScreenshots -or $ReadbackSmoke -or $BridgeSmoke -or $Dataset -gt 0 -or $Plan)) { throw 'Process mode has a separate sensor and inspection contract; do not mix with legacy automation.' }
if (([int]$ProcessCapture.IsPresent + [int]$ProcessSmoke.IsPresent + [int]$ProcessPolicySmoke.IsPresent) -gt 1) { throw 'Select one process automation mode.' }
if ($ProcessSmoke -and $NoInference) { throw 'ProcessSmoke requires actual inference.' }
if ($ProcessEpisodes -gt 0 -and -not $ProcessCapture) { throw 'ProcessEpisodes requires ProcessCapture.' }
$Project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\DarkFactory.uproject'))
$Exe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$Arguments = @($Project, '-game', '-windowed', '-ResX=1600', '-ResY=900', '-NoSplash', "-DFEndpoint=$Endpoint", "-DFSeed=$Seed", "-DFRoofLightLumens=$RoofLightLumens")
if ($WeldingSmoke -and ($Smoke -or $NightVisionSmoke -or $ThermalSmoke -or $ModalityScreenshots -or $ReadbackSmoke -or $BridgeSmoke -or $Dataset -gt 0 -or $Plan)) { throw 'WeldingSmoke cannot use procedural smoke or training flags' }
$Arguments += "-DFScene=$Scene"
$Arguments += "-DFWeldingRoofLightLumens=$WeldingRoofLightLumens"
if ($SceneManifest) { $Arguments += "-DFSceneManifest=$([IO.Path]::GetFullPath($SceneManifest))" }
if ($WeldingSmoke) { $Arguments += '-DFWeldingSmoke' }
if ($CurtainSmoke) { $Arguments += '-DFCurtainSmoke'; $Arguments += '-ExecCmds=r.SetRes 1600x900w,t.MaxFPS 60'; $Arguments += '-ForceRes' }
if ($Process -or $ProcessCapture -or $ProcessSmoke -or $ProcessPolicySmoke) { $Arguments += '-DFProcess'; $Arguments += "-DFProcessEndpoint=$ProcessEndpoint" }
if ($ProcessCapture) { $Arguments += '-DFProcessCapture' }
if ($ProcessSmoke) { $Arguments += '-DFProcessSmoke' }
if ($ProcessPolicySmoke) { $Arguments += '-DFProcessPolicySmoke' }
if ($ProcessCapture) { $Arguments += "-DFProcessEpisodes=$ProcessEpisodes"; $Arguments += "-DFProcessDatasetSeed=$ProcessDatasetSeed" }
if ($Process -or $ProcessCapture -or $ProcessSmoke -or $ProcessPolicySmoke) { $Arguments += '-ExecCmds=t.MaxFPS 60'; $Arguments += "-DFProcessObserverMode=$ProcessObserverMode"; $Arguments += "-DFProcessView=$ProcessView" }
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
if ($ProcessSmoke -or $ProcessPolicySmoke) {
    $ProcessRunId = [Guid]::NewGuid().ToString('N')
    $ProcessRunOutput = Get-DarkFactoryProcessOutput -RequestedOutput $Output -ProjectPath $Project
    [IO.Directory]::CreateDirectory($ProcessRunOutput) | Out-Null
    $ProcessReportName = if ($ProcessPolicySmoke) { 'process_policy.json' } else { 'process_runtime.json' }
    $ProcessCheckMode = if ($ProcessPolicySmoke) { 'policy' } else { 'model' }
    $ProcessReportPath = Join-Path $ProcessRunOutput $ProcessReportName
    # Native writes the fixed report names. Preserve earlier evidence before a
    # deliberate directory reuse; the new nonce, not a file timestamp, proves freshness.
    $PriorReportNames = if ($ProcessPolicySmoke) { @('process_runtime.json','process_policy.json') } else { @('process_runtime.json') }
    foreach ($PriorName in $PriorReportNames) {
        $PriorPath = Join-Path $ProcessRunOutput $PriorName
        if (Test-Path -LiteralPath $PriorPath -PathType Leaf) {
            $ArchivePath = Join-Path $ProcessRunOutput ([IO.Path]::GetFileNameWithoutExtension($PriorName)+'.before-'+$ProcessRunId+'.json')
            if (Test-Path -LiteralPath $ArchivePath) { throw "Prior report archive already exists: $ArchivePath" }
            Copy-Item -LiteralPath $PriorPath -Destination $ArchivePath -ErrorAction Stop
        }
    }
    $Arguments += "-DFProcessRunId=$ProcessRunId"
    $Arguments += "-DFOutput=$ProcessRunOutput"
 } elseif ($CurtainSmoke) {
    $CurtainRunId = [Guid]::NewGuid().ToString('N')
    $CurtainRunOutput = if ($Output) { [IO.Path]::GetFullPath($Output) } else { [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($Project)) "..\artifacts\curtain-smoke\$CurtainRunId")) }
    [IO.Directory]::CreateDirectory($CurtainRunOutput) | Out-Null
    $CurtainReportPath = Join-Path $CurtainRunOutput 'curtains.json'
    if (Test-Path -LiteralPath $CurtainReportPath -PathType Leaf) {
        Copy-Item -LiteralPath $CurtainReportPath -Destination (Join-Path $CurtainRunOutput "curtains.before-$CurtainRunId.json") -ErrorAction Stop
    }
    $Arguments += "-DFCurtainRunId=$CurtainRunId"
    $Arguments += "-DFOutput=$CurtainRunOutput"
} elseif ($Output) { $Arguments += "-DFOutput=$([IO.Path]::GetFullPath($Output))" }
& $Exe @Arguments | Out-Host
$SimulationExitCode = $LASTEXITCODE
if ($SimulationExitCode -ne 0) { throw "Simulation exited with code $SimulationExitCode" }
if ($ProcessSmoke -or $ProcessPolicySmoke) {
    # UE can record RequestExitWithStatus(1) yet return Windows exit code 0.
    # Require the actual native verdict and this launch's identity as well.
    Assert-DarkFactoryProcessReport -ReportPath $ProcessReportPath -Mode $ProcessCheckMode -SessionId $ProcessRunId
    Write-Host "Process $ProcessCheckMode smoke passed: $ProcessReportPath"
}



if ($CurtainSmoke) {
    Assert-DarkFactoryCurtainReport -ReportPath $CurtainReportPath -SessionId $CurtainRunId
    Write-Host "Curtain native smoke passed: $CurtainReportPath"
    Write-Host "Verify physical evidence and screenshots: python scripts/verify_curtains.py --directory `"$CurtainRunOutput`""
}
