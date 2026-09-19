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
    execution_policy=[pscustomobject]@{required_consensus_batches=1}
    decision_policy=[pscustomobject]@{consecutive_green=1}
    session_id=$Session; passed=$true; source='host_gpu'; contract_version='automotive-panel-process-v3'; model_smoke=$true
    completed_bodies=3; passed_bodies=3; body_outcomes_correct=$true; thermal_limits_respected=$true
    ground_truth_fields_sent_to_model=0; legacy_bridge_requests=0
    body_outcomes=@(foreach ($Sequence in 1..3) {
        $Branch = if ($Sequence -eq 2) { 'preassembled' } else { 'assembly' }
        [pscustomobject]@{body_sequence=$Sequence; body_id="fixture-body-$Sequence"; expected_destination='exit'; actual_destination='exit'; correct=$true
            expected_branch=$Branch; actual_branch=$Branch; assembly_performed=($Sequence -ne 2); welding_authorized=($Sequence -ne 2)}
    })
    thermal_gate_checks=@(foreach ($Sequence in @(1,2,3)) { foreach ($Stage in @('adhesive','weld')) {
        $Limit = if ($Stage -eq 'adhesive') { 45 } else { 55 }
        [pscustomobject]@{body_sequence=$Sequence; body_id="fixture-body-$Sequence"; stage=$Stage; maximum_apparent_c_at_acceptance=30; limit_c=$Limit; ready_at_acceptance=$true}
    }})
}
$Policy = [pscustomobject]@{
    execution_policy=[pscustomobject]@{required_consensus_batches=1}
    session_id=$Session; passed=$true; source='policy_test'; schema='darkfactory-process-policy-smoke-v1'; contract_version='automotive-panel-process-v3'
    model_accuracy_evidence=$false; http_requests_submitted=0; checks_count=2
    checks=@([pscustomobject]@{name='synthetic-policy-a';passed=$true}, [pscustomobject]@{name='synthetic-policy-b';passed=$true})
}
$ModelJson = $Model | ConvertTo-Json -Depth 12
$PolicyJson = $Policy | ConvertTo-Json -Depth 12
$ModelPath = Save-Fixture 'valid-model' $Model
$PolicyPath = Save-Fixture 'valid-policy' $Policy
Check 'valid_synthetic_model' { Assert-DarkFactoryProcessReport $ModelPath model $Session }
$EdgeModel = $ModelJson | ConvertFrom-Json
$EdgeModel.source = 'evk_hexagon'
$EdgePath = Save-Fixture 'valid-edge-model' $EdgeModel
Check 'explicit_edge_source' { Assert-DarkFactoryProcessReport $EdgePath model $Session -ModelSource evk_hexagon }
Check 'edge_cannot_masquerade_as_host' { Assert-DarkFactoryProcessReport $EdgePath model $Session } $false
Check 'host_cannot_masquerade_as_edge' { Assert-DarkFactoryProcessReport $ModelPath model $Session -ModelSource evk_hexagon } $false
Check 'valid_synthetic_policy' { Assert-DarkFactoryProcessReport $PolicyPath policy $Session }
foreach ($Mode in @('model','policy')) {
    $Old = $(if ($Mode -eq 'model') { $ModelJson } else { $PolicyJson }) | ConvertFrom-Json
    $Old.execution_policy.required_consensus_batches=2
    $OldPath=Save-Fixture ("old-two-batch-$Mode") $Old
    Check ("reject_two_batch_$Mode") { Assert-DarkFactoryProcessReport $OldPath $Mode $Session } $false
}
Check 'consensus_switch_removed' {
    if (@($Ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'ProcessConsensusBatches' }).Count) { throw 'Removed switch still exposed' }
}
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
    @('wrong_consensus', {param($r) $r.execution_policy.required_consensus_batches=2}),
    @('changed_model_contract', {param($r) $r.decision_policy.consecutive_green=2}),
    @('string_passed', {param($r) $r.passed='true'}),
    @('wrong_source', {param($r) $r.source='policy_test'}),
    @('wrong_mode', {param($r) $r.model_smoke=$false}),
    @('incomplete_run', {param($r) $r.completed_bodies=2}),
    @('wrong_destination', {param($r) $r.body_outcomes[0].actual_destination='entry'}),
    @('old_contract', {param($r) $r.contract_version='automotive-process-v1'}),
    @('swapped_branches_three_exits', {param($r) $r.body_outcomes[0].actual_branch='preassembled'; $r.body_outcomes[1].actual_branch='assembly'}),
    @('missing_required_assembly', {param($r) $r.body_outcomes[0].assembly_performed=$false}),
    @('unwanted_preassembled_weld', {param($r) $r.body_outcomes[1].welding_authorized=$true}),
    @('string_assembly_flag', {param($r) $r.body_outcomes[0].assembly_performed='true'}),
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
    @('wrong_mode', {param($r) $r.source='host_gpu'}),
    @('old_policy_contract', {param($r) $r.contract_version='automotive-process-v1'})
)
foreach ($Mutation in $PolicyMutations) {
    $Row = $PolicyJson | ConvertFrom-Json
    & $Mutation[1] $Row
    $Path = Save-Fixture ('policy-' + $Mutation[0]) $Row
    Check ('reject_policy_' + $Mutation[0]) { Assert-DarkFactoryProcessReport $Path policy $Session } $false
}
$Project = Join-Path $Repository 'unreal\DarkFactory.uproject'
$AgentFixture = $ModelJson | ConvertFrom-Json
$AgentFixture | Add-Member control_mode 'agent'
$AgentFixture | Add-Member model_http_requests 10
$AgentFixture | Add-Member agent ([pscustomobject]@{
    schema='darkfactory-live-agent-v1'; recipe_vetoes_enabled=$false; requests=20; rejected_calls=0; audit_failures=0; held=$false
    outcomes=@([pscustomobject]@{body_sequence=1},[pscustomobject]@{body_sequence=2},[pscustomobject]@{body_sequence=3})
    audits=@([pscustomobject]@{action='send_forward'; failures=@()})
})
$AgentJson = $AgentFixture | ConvertTo-Json -Depth 12
Check 'accept_live_agent_report' { Assert-DarkFactoryProcessReport (Save-Fixture 'agent-good' $AgentFixture) agent $Session }
foreach ($Mutation in @(
    @('veto_enabled',{param($r) $r.agent.recipe_vetoes_enabled=$true}),
    @('missing_audits',{param($r) $r.agent.audits=$null}),
    @('bad_action_with_zero_counter',{param($r) $r.agent.audits[0].failures=@('welding_while_sealant_hot')}),
    @('rejected_call',{param($r) $r.agent.rejected_calls=1}),
    @('held',{param($r) $r.agent.held=$true}),
    @('string_held',{param($r) $r.agent.held='false'}),
    @('no_agent_calls',{param($r) $r.agent.requests=0}),
    @('no_vision_calls',{param($r) $r.model_http_requests=0}),
    @('unfinished',{param($r) $r.agent.outcomes=@($r.agent.outcomes[0])})
)) {
    $Row=$AgentJson | ConvertFrom-Json
    & $Mutation[1] $Row
    Check ('reject_agent_'+$Mutation[0]) { Assert-DarkFactoryProcessReport (Save-Fixture ('agent-'+$Mutation[0]) $Row) agent $Session } $false
}
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
