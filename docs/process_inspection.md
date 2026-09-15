# Camera-gated inner trunk panel assembly

Explicit `-Process` selects the `automotive-panel-process-v3` experiment in the locally imported cell. It replaces the former door-fault/return task. Host Cosmos-Reason2-2B inspects camera images; the earlier parcel adapter, procedural mock and EVK deployment remain separate.

| Step | Physical behavior | Model gate or transition |
| --- | --- | --- |
| 1. Arrival | Body and rail holders move into the station | Arrive and settle |
| 2. Panel presence | R4 scans the inner trunk installation area with NIR | Two matching fresh G: preassembled branch; two R: assembly branch; A holds |
| 3. Sealant application | Arm A extrudes the two continuous perimeter sealant runs | Authored application finishes |
| 4. Pickup and placement | Arm B picks up a panel, positions it and stays engaged | Placement finishes; arm B continues holding |
| 5. Sealant readiness | R4 scans the complete two sealant runs from three physical positions | Two fresh G: temperature-ready; A/R waits |
| 6. Welding | Arm C spot welds the held panel | Authorized welding finishes |
| 7. Panel seating | R4 checks the installed panel with NIR | Two fresh G: seating accepted; R is an exception hold; A waits |
| 8. Weld cooldown | R4 checks the six weld contact regions in three paired views thermally | Two fresh G: release-ready; A/R waits |
| 9. Holder retraction | Arm B releases the panel and withdraws | Retraction finishes |
| 10. Departure | Body and rail holders leave the station | Reset identities and begin the next body |

One in five generated bodies is preassembled. The model, not the generator flag, selects the branch. A preassembled body runs presence, adhesive-readiness and weld-cooldown inspection, skipping adhesive application, pickup/placement, welding, seating inspection and holder retraction. Missing panels normally take the full assembly sequence. A wrong presence R could attempt assembly over an installed panel, so both branch directions require evaluation; zero false G alone is insufficient.

The source role mapping is arm A/R1 for adhesive, arm B/R3 for pickup/holding, arm C/R2 for welding, and R4 for inspection. R4 never welds. This is authored demonstration choreography, not calibrated industrial robot control. The 45 C adhesive and 55 C weld thresholds demonstrate apparent-temperature readiness, not chemical cure or structural weld quality.

## Model and startup status

The six-weld scene has new capture and model-development evidence. Consult the [current model guide](model_classification.md#selected-host-runtime) for the selected runtime and validation status. Earlier three-contact and door results retain their original scope.

After preparing the local assets and building, explicit exploration without inference is:

```powershell
.\unreal\Scripts\build.ps1 -SkipContent
.\unreal\Scripts\run.ps1 -Scene welding -Process -NoInference
```

Reuse the selected host when its `/health` reports ready with the matching v3 identity. When absent, start it with the [detached WSL service helper](model_classification.md#selected-host-runtime), wait for GPU loading and `/health`, then launch the model-gated demo:

```powershell
.\unreal\Scripts\run.ps1 -Scene welding -Process -ProcessObserverMode 3 `
    -ProcessEndpoint http://127.0.0.1:18191/v1/inspect
```

The normal process endpoint remains loopback 18191. `-ProcessView cabinet` or `inspection` chooses an observer starting point, and `-ProcessObserverMode 3` enables the bright observer view. Observer movement, Xbox controls, light modes and palettes never modify model evidence. Space / Xbox View pauses physical state, cancels incomplete inspections and starts a fresh batch on resume. There is no timer or ground-truth fallback that overrides a model hold.

If a gate reports that the model is unavailable, check `http://127.0.0.1:18191/health` and `wsl.exe systemctl --user status dark-factory-panel-model.service`. The saved adapter is independent of the running server; an unavailable endpoint does not require retraining. Inspect logs with `wsl.exe journalctl --user -u dark-factory-panel-model.service -n 80 --no-pager`. The detached host survives closing Codex or the launch terminal during the current WSL session, but is not installed for startup after WSL shutdown or a Windows reboot. Stop it explicitly when finished using `wsl.exe systemctl --user stop dark-factory-panel-model.service`. See [host lifecycle](model_classification.md#host-service-lifecycle) for restart behavior and the foreground alternative. All transport failures continue to hold the model gate until fresh compatible evidence is available.

## Capture and timing

All four gates use three physical R4 views in `spot1`, `spot2`, `spot3` order. NIR uses the panel framing; thermal uses contact closeups. The rigid lens moves with six robot joints and is never independently aimed. Capture waits for physical settlement and three rendering settle frames. Deployment takes 1.0 second, each between-spot movement 0.8 seconds, and withdrawal 0.8 seconds. Actual images are GPU renders with bounded asynchronous readback and worker PNG encoding. HUD previews sample the GPU targets directly.

The model sees lossless 448 x 256 grayscale PNGs and the fixed stage prompt. Model thermal stays 20-120 C apparent. The observer is independently fixed at 0-120 C; F10/right-stick welding-range switching has been removed, and colors above 120 C saturate without changing the physical hot-surface temperatures or sensor encoding. Cabinet text, status lamps and HUD are hidden from model cameras. Identity, version, prompt hash, camera order, modality, scoring method and policy must agree. The whole batch/reply age limit is eight seconds; stale or mismatched evidence holds.

Panel choreography constants currently specify 13.5 seconds of sealant application, 6.0 seconds of pickup/placement with an initial 1.5-second pickup interval, 11.5 seconds of pinch welding, and 2.5 seconds of holder retraction. These are authored action durations, not full-cycle throughput: inspection travel, model consensus and temperature-dependent waits add time. Shared source constants in `DarkFactoryWeldingCell.h` and the scene's phase-duration function are authoritative. The legacy 21-second visual cycle remains a separate fixture.

Offline capture snaps the same physical inspection poses at a frozen process/heat clock. Every episode contains eight batches: presence, three adhesive G/A/R states, seating, and three weld G/A/R states. Its views and time steps stay together in one split. Current recipes are `rigid-arm-panel-perimeter-scan-v5` and `panel-perimeter-episodes-v8`; the panel contact surface-heat recipe is `panel-perimeter-contact-core-halo-v6`. Adhesive views map to the two physical beads as `[0,1,0]`; the isolated rear-middle patch is absent; each continuous run now extends along a side and half the rear contact, while weld views cover pairs [0,1], [2,3], [4,5]. Never reinterpret old door data as this panel dataset.

```powershell
# No model calls: controller policy and physical camera evidence.
$RunStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
.\unreal\Scripts\run.ps1 -ProcessPolicySmoke -Output "artifacts\panel-policy-v3-$RunStamp"
.\unreal\Scripts\run.ps1 -ProcessCapture -ProcessEpisodes 64 -ProcessDatasetSeed 4307 `
    -Output "artifacts\panel-perimeter-episodes-v8-$RunStamp"

# With the selected compatible model running: full camera-to-motion integration.
.\unreal\Scripts\run.ps1 -ProcessSmoke `
    -ProcessEndpoint http://127.0.0.1:18191/v1/inspect `
    -Output "artifacts\panel-model-live-v3-$RunStamp"
```

Use a new output directory for every capture/run. Seed 4307 reproduces the current episode recipe; a recapture of those seeds is not a new independent test cohort. The completed 64-episode dataset has 40 training, 12 validation and 12 test episodes: 320/96/96 batches and 960/288/288 PNGs, totalling 512 batches/1,536 PNGs. Labels and source metadata remain private offline data and are stripped by the evaluator. Review rendered panel/contact coverage and grayscale radiometry before measuring classification.

## Native verification

The `ProcessSmoke` fixture requires three forward departures: bodies 1 and 3 must perform assembly and welding, while body 2 must take the preassembled branch and skip them. It audits six thermal acceptances against 45/55 C actual apparent limits after the model actions. A wrong branch, premature acceptance or missing stage fails the report; the audit cannot change the model result or control the scene.

A four-minute wall-clock limit includes pauses and model holds. The launcher checks a new session ID and the explicit JSON verdict in addition to the Windows exit status. Missing/stale/failed reports cannot pass. Screenshots are saved under the run's session directory and require visual review. Policy checks make no model calls and are not accuracy evidence.

Current rendering, policy and model results are recorded in the [pinch/sealant
implementation](pinch_welder_and_sealant.md) and [model guide](model_classification.md).
Earlier three-contact model results and their integration runs remain in
[two-bead history](model_classification_two_bead_history.md); the previous six-weld
model remains in [six-weld history](model_classification_six_weld_history.md).
Those scores do not validate the revised perimeter views and prompt.


Purchased assets, runtime packages, rendered datasets and weights stay private. Only tutorial and source examples are published.
