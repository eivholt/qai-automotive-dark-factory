# Repository agent memory

User direction recorded on 2026-09-11. Read this alongside AGENTS.md when planning or implementing changes. These notes describe the intended demo; they do not imply that every modality or adapter is implemented.

## Demonstration objective

Build an Unreal Engine factory simulation demonstrating sensor cameras feeding NVIDIA Cosmos-Reason2-2B to detect anomalies. The simulation should react to model results. Begin with several sensing modalities and use experiments to refine the concrete manufacturing scenarios.

## Production-line sequence and inspection gates

Use 3D assets to depict a section of a production line. Sensor-camera observations and the model's inspection results should act as a gate for subsequent production steps: the simulation advances only when the inspection outcome permits it, and otherwise holds or follows the defined anomaly response.

The concrete production steps, assets, gate conditions and downstream responses remain to be specified by the user. More details will follow; keep the sequence and inspection gates configurable.

## Initial modalities and anomaly scenarios

| Modality | Intended inspection |
| --- | --- |
| 1. Near-infrared (NIR) | Identify parts that have fallen off a rig, or are about to come loose. |
| 2. Thermal imaging | Identify overheating, heating-element faults and temperature-dependent process defects, including examples involving vehicle windows and glue placement. |
| 3. Temporarily illuminated visible imaging | Temporarily enable local inspection lighting to look for surface defects, such as paint scratches. |

Thermal imaging needs its own temperature/emission representation; the current NIR rendering proxy and a color palette do not establish thermal sensing. Local visible inspection lighting is intentional and compatible with the dark-factory demonstration.

## Adapter experiments

The LoRA adapter strategy is undecided. Compare a single shared adapter across modalities with dedicated adapters per modality. Select the organization based on experiments rather than committing to an architecture in advance. Keep training, evaluation and deployment input contracts consistent for each candidate.

## Observer experience

The observer viewpoint should switch between modalities, inspired by the vision-mode switching in Splinter Cell games. Preserve independent Xbox-controller navigation on PC.

The normal visible observer view should remain navigable under dim light entering from roof windows; the user does not want this view to be completely black. Keep true zero-light scenes as explicit offline calibration conditions. Roof-window light must remain independent of fixed NIR and thermal sensor inputs. Provide native Unreal captures comparing the implemented observer modalities from the same scene and viewpoint.

Observer mode changes are presentation controls and must remain independent of the inspection sensors and their model inputs. The fixed NIR sensor stays in NIR when the observer changes modes. Additional thermal and locally illuminated inspection sensors should retain their own capture settings and explicit lighting/inspection sequences.

## Planning status

The final vehicle-manufacturing scenes and purchased assets remain undecided. Develop replaceable inspection cells and reusable capture, randomization, training and model-response infrastructure while experimenting with these modalities.

## Thermal visual direction

The user requested less uniform thermal examples: warmer central body regions, cooler edges and thin parts. Represent these as spatial surface-temperature patterns while preserving opaque occlusion and independent sensor views. Thin passive features and actively powered heating elements need different heating/cooling behavior.

- Thermal bodies must exchange heat with nearby surfaces: contact conduction and blocked/distance-dependent radiation, with stored warmth persisting after the source moves. Current implementation is a deterministic bulk/six-face network feeding GPU patches; use `docs/thermal_heat_transfer.md` for assumptions, controls and validation. Preserve the separate thermal sensor-v3 contract and frozen capture metadata.
