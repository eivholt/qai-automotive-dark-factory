# CPU-only regression checks for the production launcher's report validation.
# Every report below is synthetic; no Unreal, model, asset or GPU is used.
[CmdletBinding()]
param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$Repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$RunPath = Join-Path $Repository 'unreal\Scripts\run.ps1'
$Tokens = $null; $ParseErrors = $null
$Ast = [System.Management.Automation.Language.Parser]::ParseFile($RunPath, [ref]$Tokens, [ref]$ParseErrors)
if ($ParseErrors.Count) { throw 'run.ps1 has parse errors' }
# Load only these production functions, never the launcher's executable body.
$FunctionNames = @('Assert-DarkFactoryProcessReport', 'Get-DarkFactoryProcessOutput')
$Functions = @($Ast.FindAll({param($Node)
    $Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -in $FunctionNames
}, $true))
if ($Functions.Count -ne $FunctionNames.Count) { throw 'Required launcher functions were not found' }
foreach ($Function in $Functions) { . ([scriptblock]::Create($Function.Extent.Text)) }
$Session = [Guid]::NewGuid().ToString('N')
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $Repository 'artifacts\process-launcher-tests' }
$RunOutput = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $Session
[IO.Directory]::CreateDirectory($RunOutput) | Out-Null
$Checks = [System.Collections.Generic.List[object]]::new()
function Check {
    param([string]$Name, [scriptblock]$Action, [bool]$ExpectedAcceptance = $true)
    $Accepted = $true; $Detail = ''
    try { & $Action | Out-Null } catch { $Accepted = $false; $Detail = $_.Exception.Message }
    $Checks.Add([pscustomobject]@{name=$Name; passed=($Accepted -eq $ExpectedAcceptance); accepted=$Accepted; detail=$Detail})
}
function Save-Fixture {
    param([string]$Name, $Value)
    $Path = Join-Path $RunOutput ($Name + '.json')
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding utf8
    return $Path
}
$Model = [pscustomobject]@{
    session_id=$Session; passed=$true; source='host_gpu'; contract_version='automotive-process-v1'; model_smoke=$true
    completed_bodies=3; passed_bodies=2; returned_bodies=1; body_outcomes_correct=$true; thermal_limits_respected=$true
    ground_truth_fields_sent_to_model=0; legacy_bridge_requests=0
    body_outcomes=@(foreach ($Sequence in 1..3) {
        $Destination = if ($Sequence -eq 2) { 'entry' } else { 'exit' }
        [pscustomobject]@{body_sequence=$Sequence; body_id="fixture-body-$Sequence"; expected_destination=$Destination; actual_destination=$Destination; correct=$true}
    })
    thermal_gate_checks=@(foreach ($Sequence in @(1,3)) { foreach ($Stage in @('adhesive','weld')) {
        $Limit = if ($Stage -eq 'adhesive') { 45 } else { 55 }
        [pscustomobject]@{body_sequence=$Sequence; body_id="fixture-body-$Sequence"; stage=$Stage; maximum_apparent_c_at_acceptance=30; limit_c=$Limit; ready_at_acceptance=$true}
    }})
}
$Policy = [pscustomobject]@{
    session_id=$Session; passed=$true; source='policy_test'; schema='darkfactory-process-policy-smoke-v1'
    model_accuracy_evidence=$false; http_requests_submitted=0; checks_count=2
    checks=@([pscustomobject]@{name='synthetic-policy-a';passed=$true}, [pscustomobject]@{name='synthetic-policy-b';passed=$true})
}
$ModelJson = $Model | ConvertTo-Json -Depth 12
$PolicyJson = $Policy | ConvertTo-Json -Depth 12
$ModelPath = Save-Fixture 'valid-model' $Model
$PolicyPath = Save-Fixture 'valid-policy' $Policy
Check 'valid_synthetic_model' { Assert-DarkFactoryProcessReport $ModelPath model $Session }
Check 'valid_synthetic_policy' { Assert-DarkFactoryProcessReport $PolicyPath policy $Session }
$OtherSession = [Guid]::NewGuid().ToString('N')
Check 'stale_model_report' { Assert-DarkFactoryProcessReport $ModelPath model $OtherSession } $false
Check 'stale_policy_report' { Assert-DarkFactoryProcessReport $PolicyPath policy $OtherSession } $false
Check 'missing_report' { Assert-DarkFactoryProcessReport (Join-Path $RunOutput 'missing.json') model $Session } $false
$Malformed = Join-Path $RunOutput 'malformed.json'
'{' | Set-Content -LiteralPath $Malformed -Encoding utf8
Check 'malformed_report' { Assert-DarkFactoryProcessReport $Malformed model $Session } $false
$FailedModel = $ModelJson | ConvertFrom-Json
$FailedModel.passed = $false
$FailedPath = Save-Fixture 'failed-model' $FailedModel
Check 'failed_report_overrides_zero_engine_exit' {
    $LASTEXITCODE = 0 # Reproduce UE's observed graceful-exit status; do not launch UE.
    Assert-DarkFactoryProcessReport $FailedPath model $Session
} $false
$ModelMutations = @(
    @('string_passed', {param($r) $r.passed='true'}),
    @('wrong_source', {param($r) $r.source='policy_test'}),
    @('wrong_mode', {param($r) $r.model_smoke=$false}),
    @('incomplete_run', {param($r) $r.completed_bodies=2}),
    @('swapped_destinations_correct_totals', {param($r) $r.body_outcomes[0].actual_destination='entry'; $r.body_outcomes[1].actual_destination='exit'}),
    @('duplicate_body', {param($r) $r.body_outcomes[1].body_sequence=1}),
    @('thermal_flag_false', {param($r) $r.thermal_limits_respected=$false}),
    @('missing_thermal_check', {param($r) $r.thermal_gate_checks=@($r.thermal_gate_checks[0])}),
    @('duplicate_thermal_check', {param($r) $r.thermal_gate_checks[1]=$r.thermal_gate_checks[0]}),
    @('hot_temperature_with_ready_flag', {param($r) $r.thermal_gate_checks[0].maximum_apparent_c_at_acceptance=50}),
    @('wrong_thermal_identity', {param($r) $r.thermal_gate_checks[0].body_id='unrelated-fixture-body'}),
    @('unexpected_legacy_request', {param($r) $r.legacy_bridge_requests=1})
)
foreach ($Mutation in $ModelMutations) {
    $Row = $ModelJson | ConvertFrom-Json
    & $Mutation[1] $Row
    $Path = Save-Fixture ('model-' + $Mutation[0]) $Row
    Check ('reject_model_' + $Mutation[0]) { Assert-DarkFactoryProcessReport $Path model $Session } $false
}
$PolicyMutations = @(
    @('false_check_with_passed_flag', {param($r) $r.checks[0].passed=$false}),
    @('string_check_pass', {param($r) $r.checks[0].passed='true'}),
    @('count_mismatch', {param($r) $r.checks_count=3}),
    @('duplicate_check', {param($r) $r.checks[1].name=$r.checks[0].name}),
    @('model_request', {param($r) $r.http_requests_submitted=1}),
    @('accuracy_claim', {param($r) $r.model_accuracy_evidence=$true}),
    @('wrong_mode', {param($r) $r.source='host_gpu'})
)
foreach ($Mutation in $PolicyMutations) {
    $Row = $PolicyJson | ConvertFrom-Json
    & $Mutation[1] $Row
    $Path = Save-Fixture ('policy-' + $Mutation[0]) $Row
    Check ('reject_policy_' + $Mutation[0]) { Assert-DarkFactoryProcessReport $Path policy $Session } $false
}
$Project = Join-Path $Repository 'unreal\DarkFactory.uproject'
Check 'default_output_matches_native' {
    if ((Get-DarkFactoryProcessOutput '' $Project) -ne (Join-Path $Repository 'artifacts\process-live')) { throw 'Default output differs from native path' }
}
Check 'explicit_output_matches_native' {
    if ((Get-DarkFactoryProcessOutput $RunOutput $Project) -ne $RunOutput) { throw 'Explicit output was changed' }
}
$Failures = @($Checks | Where-Object passed -eq $false)
$ReportPath = Join-Path $RunOutput 'verification.json'
[pscustomobject]@{
    scope='CPU-only synthetic launcher validation; no model accuracy evidence'; unreal_launched=$false
    passed=($Failures.Count -eq 0); checks_count=$Checks.Count; checks=$Checks
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding utf8
[pscustomobject]@{passed=($Failures.Count -eq 0); checks=$Checks.Count; failed_checks=@($Failures | ForEach-Object { $_.name }); report=$ReportPath} | ConvertTo-Json
if ($Failures.Count) { exit 1 }
