# Automotive camera-gated process

The `-Process` mode adds an experimental Cosmos-Reason2-2B inspection sequence to the locally imported welding cell. It uses the host GPU first. The earlier parcel model, battery-tray mock and EVK deployment remain separate.

| Stage | Physical behavior | Camera evidence | Gate response |
| --- | --- | --- | --- |
| Arrival and doors | Body and holders arrive; four authored door inserts may be seated, loose, fallen or missing | Two opposite-side fixed NIR views | Two fresh G results continue; R returns the body through entry; A holds |
| Adhesive readiness | Three strips cool at independently randomized rates; R4 carries a thermal camera and never welds | Three views aimed from the physical R4 lens | G continues when visually ready; R or A waits and reinspects |
| Welding | Three robots approach, weld and withdraw | No decision is required during the approved welding movement | Wait for the authored weld sequence to finish |
| Weld cooldown | The inspection arm observes the three weld regions | Three fixed-window thermal views | Two fresh G results release the body; R or A waits |
| Departure or return | Body and holders leave through the appropriate portal; detached doors are cleared | Next body has fresh identities and empty gate history | Repeat |

The live arrival generator uses an aggregate one-in-three door-fault probability, with randomized subtype and affected door. Offline data deliberately balances examples differently. Source assets have no individual door meshes, so the four fitted windowed door inserts are additional authored geometry. Adhesive heat, weld heat, emission, robot motion and collision proxies are illustrative values.

The adhesive gate demonstrates **temperature readiness**, not proof of chemical cure. A later scenario can add a minimum cure-time interlock alongside visual inspection. Weld cooldown likewise does not establish weld quality; keep cosmetic/weld-quality inspection as a separate task when expanding the tutorial.

## Start the experiment

Prepare/import the local assets and build as described in the README. Start the [selected host runtime](model_classification.md#selected-host-runtime), which routes NIR doors and thermal inspections to two evaluated adapter checkpoints resident on the host GPU. These checkpoints are private local outputs; the linked instructions also describe how to reproduce the fitting experiment. The unadapted baseline has poor gate accuracy. Backend health confirms availability, not classification accuracy. Once its `/health` endpoint reports ready:

```powershell
.\unreal\Scripts\build.ps1 -SkipContent
.\unreal\Scripts\run.ps1 -Scene welding -Process
```

The endpoint defaults to `http://127.0.0.1:18191/v1/inspect`; `-ProcessEndpoint` overrides it. The selected Torch runtime serves this endpoint directly; the optional llama.cpp comparison uses a separate backend port. Model weights, exports and new adapters stay in ignored local directories. No automotive model is deployed to the IQ9 EVK by this change.

Use `-ProcessView cabinet` or `-ProcessView inspection` to start near the physical indicators or camera arm; `-ProcessObserverMode 3` starts under bright roof lights. These settings change only the observer. `-Process -NoInference` explicitly holds without sending model requests.

An unavailable or uncertain model leaves the body at the gate. There is no ground-truth fallback or timer overriding a hold. HUD, cabinet LCD/button pairs and stack lights show controller state. Space / Xbox View pauses motion and invalidates outstanding inspections. Observer modes, thermal palettes, headlamp, flight and bookmarks remain independent of model images.

## Capture and validation

The model sees only fresh 448 x 256 lossless grayscale PNGs and the frozen stage prompt. Thermal images use a linear apparent-temperature window of 20 to 120 C. Cabinet display, gate lamps and HUD are excluded. The host returns G/A/R, identity, prompt hash and decision policy; the controller rejects stale/mismatched replies and requires two consecutive fresh G batches. Images render on the GPU, asynchronously read back once per request and encode on a worker thread. UI previews sample GPU textures directly.

The frozen v1 thresholds are 45 C for adhesive and 55 C for weld release, with a +/-3 C ambiguity band labeled A. Native initialization rejects unsupported contract edits. Read [model classification](model_classification.md) before changing prompts, preprocessing or policy.

```powershell
# Scripted native controller/physical-state checks; no model requests.
.\unreal\Scripts\run.ps1 -ProcessPolicySmoke -Output artifacts\process-policy

# Forty development batches for camera review; no inference.
.\unreal\Scripts\run.ps1 -ProcessCapture -Output artifacts\process-captures

# 64 whole-body episodes: 40 train, 12 validation, 12 test.
# Each body has a door batch and hot/borderline/cool batches for each thermal gate.
.\unreal\Scripts\run.ps1 -ProcessCapture -ProcessEpisodes 64 -ProcessDatasetSeed 701 -Output artifacts\process-episodes

# Actual host inference: requires healthy bodies 1/3 to depart and faulty body 2 to return.
.\unreal\Scripts\run.ps1 -ProcessSmoke -Output artifacts\process-model-live
```

Use a new capture directory: existing manifests are never overwritten. Episode capture varies camera calibration and door appearance only through an explicit offline API; live cameras stay fixed. All seven batches from one body share its episode and split. Private offline truth stays in the manifest; the evaluator never forwards it, filenames or randomization metadata.

`ProcessSmoke` selects healthy/fault/healthy bodies as an explicit test fixture. It succeeds only when bodies 1 and 3 depart and body 2 returns; exchanging a healthy rejection for a faulty departure fails even if aggregate counts match. A separate QA audit checks that actual apparent temperatures were below 45/55 C when thermal gates accepted; a premature acceptance fails the test without overriding the model-controlled action. This audit is action-time readiness, not per-frame classification accuracy or the prompt's conservative ambiguity-band evaluation. Its four-minute wall-clock limit also applies while paused. A timeout is a failed check, including when a model abstains. The final JSON report must save successfully. The launcher checks its unique session ID and explicit verdict instead of trusting only the Unreal process exit code; a stale earlier report cannot make a new failed run pass. HUD screenshot requests at phase milestones are best effort; each run stores them under `screenshots/<session_id>/` so earlier PNGs are preserved. Inspect the saved files before claiming visual validation. Test expectations affect only the report and exit code, never the model inputs or gate decisions. Report policy checks, camera review, development results and withheld-test results separately.

See [scene implementation](process_scene.md) and [HUD/cabinet indicators](process_ui.md). Images, weights, runtime packages and purchased assets remain private; only tutorial and source examples belong in Git.

## Implementation verification in this checkout

The UE 5.8 build and 58 native policy checks pass. The launcher passes 28 reusable PowerShell regression checks. A separate private process check also rejects the failed live report despite Unreal returning exit code zero. The latest native policy report passes the production launcher validator with its actual launch session. The policy run submits zero HTTP requests and therefore establishes controller behavior, not model accuracy. It exercises physical cooling during holds, two fresh G replies, fault return and cleanup, pause/stale-result handling, disabled inference, exact smoke-body outcomes and the separate thermal acceptance audit.

The current 64-episode capture contains 448 batches and 1,280 PNGs. An independent audit of all 1,152 thermal PNGs found the relevant strip/spot peaks visible and all hot/borderline/cool labels consistent with the fixed sensor window; maximum peak disagreement was 0.21 C. Captured NIR side views are distinct, and healthy/missing-door examples were visually reviewed. Across all four observer modes, the two NIR and three thermal sensor PNGs are byte-for-byte identical in a frozen paired check. These checks assess this authored renderer, not real-camera calibration or generalization.

The previous visual-cycle regression also passes all nine existing suites (12,261 checks), the separate RealismSmoke passes 681 checks with reviewed mechanism/HUD screenshots, and cabinet/readback checks retain their existing criteria. Private reports live under `artifacts/process-policy-v6`, `artifacts/process-observer-parity.json`, `artifacts/process-thermal-pixel-audit.json`, `artifacts/process-legacy-v2`, `artifacts/process-realism-v1`, `artifacts/process-cabinet-v2` and `artifacts/process-readback-v1`. Model experiments and their separate acceptance status are recorded in [model classification](model_classification.md).

## Selected model and complete process run

The frozen host configuration routes NIR to the 128-update adapter and thermal to its 128-update lower-learning-rate continuation. Both were trained on the same shared corpus; modality-only fitting has not yet been compared. On the untouched 12-episode test, 82 of 84 actions were correct with zero observed false G: one healthy door batch was falsely rejected, and one cool adhesive batch held. Median host response time was 54.2 ms. This is a small synthetic test, and the remaining errors are documented in the model guide.

A subsequent native `ProcessSmoke` passed with actual model inference: bodies 1 and 3 departed after welding, while faulty body 2 returned through entry. The production launcher validator confirmed the native verdict and matching launch session. The run lasted 98.28 simulation seconds (110.16 seconds including startup/shutdown); 45 recorded decision replies had median native HTTP latency 68.01 ms while Unreal rendered. These HTTP timings exclude capture scheduling and the required second G observation. Both adhesive approvals were below 45 C (40.29 and 36.98 C), and both weld approvals were below 55 C (35.75 and 30.49 C). These temperatures were audited only after the model actions and did not control them.

The run saved 14 screenshots at 1600 x 900; actual adhesive, door rejection and weld-release views were reviewed. Private evidence is under `artifacts/process-selected-live-v1`, including `process_runtime.json`, `launcher_verification.json`, `verified_summary.json` and per-session screenshots. The final Python suite passes 81 tests. Successful native sequencing does not erase the held-out classification errors or establish EVK or real-factory performance.
