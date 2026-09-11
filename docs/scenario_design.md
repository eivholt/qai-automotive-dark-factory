# Choosing the manufacturing scenario

The starter is a replaceable inspection cell made from Unreal primitives. It represents four modules seated in a tray. Its purpose is to exercise imaging, data generation and the model-to-simulation loop before asset selection.

## Candidate tasks

| Scenario | Observable evidence from one fixed camera | Example reaction | Asset implications |
| --- | --- | --- | --- |
| Battery module seating | Empty slot, large displacement, wrong orientation, incomplete insertion | Pause the fixture and divert the tray for rework | Tray, modules and a simple loader; good fit for the starter |
| Wheel installation | Missing wheel, visibly incorrect placement, incomplete seating | Hold the station and request another installation step | Vehicle shell, wheels and an installation manipulator |
| Panel fit inspection | Door/hood position, gross gap or skew | Stop transfer and flag rework | Detailed vehicle panels; defects must remain visible at sensor resolution |
| Robotic pick-and-place | Empty gripper, dropped component, incorrect destination | Retry a bounded step or stop the cycle | Articulated robot plus components; motion may require multiple frames |

Select an anomaly whose decisive visual evidence is large enough to survive the 448 x 256 sensor path. An absent part or a gross alignment defect is a stronger first demonstration than weld quality, torque, hidden connector seating or a hairline crack. A single image cannot prove tightening torque or distinguish every transient manufacturing step from a defect.

## Define the inspection point first

1. Define when the part is stationary and ready for inspection. Capture only after the relevant animation and renderer have settled.
2. Describe acceptable geometry, anomalous geometry and insufficient visibility in words before assigning labels.
3. Define G (proceed), A (hold), and R (anomaly/rework) from visible evidence. Simulator state supplies offline labels but is never part of model input.
4. Give every inspection a new identity. A response to the previous part must never control the next part.
5. Choose a reversible demo reaction. Keep an independent conventional interlock if this ever becomes connected to physical machinery.

## Before buying assets

Check scale in centimeters, pivot locations, separate movable parts, collision, usable LODs, UE compatibility and redistribution terms. For a robot, confirm that joints and animation controls are available. For a tray, confirm that individual components can be removed and repositioned without editing a combined mesh.

For an infrared camera, visible RGB textures do not establish NIR reflectance. Plan separate material parameters and eventual reference captures of the chosen materials. Highly polished metal, transparent covers, glare and small occlusions deserve dedicated test scenes.

## What remains scenario-dependent

- A final camera pose, lens, inspection distance and minimal resolvable defect.
- Mechanical timing and the precise pass/hold/rework policy.
- Realistic materials and animations, plus asset licenses.
- Randomization bounds, difficult negatives, and labels near acceptance boundaries.
- The final prompt, newly trained adapter and its post-quantization evaluation.
- A frozen independent test set and acceptable false-pass and latency limits.

Change the contract version when any of these changes alters what the model is expected to see or decide.
