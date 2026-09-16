# Carriage drives and cabinet surface heat

Process mode adds illustrative exterior heat to four carriage drive motors and
the control cabinets. These are visual process cues, not measured motor loads,
cabinet telemetry or visibility through an enclosure.

`scripts/assets/prepare_equipment_heat.py` reads the private source scan and
island ownership report. It fits two local motor capsules to each moving holder,
converting source coordinates into that holder's pivot frame. Cabinet fields
follow the upper vents, controller enclosure and LCD. The private descriptor is
`assets/derived/welding_cell/equipment_heat.json`; regenerate it after changing
the source grouping. Full welding preparation calls the generator automatically.

The process clock and actual carriage displacement drive a bounded first-order
motor surface model. At 500 cm/s or faster, the target core is 62 C with a 2.8 s
heating time constant. Stationary drives cool toward 20 C with a 24 s time
constant. A warm 26 C startup represents prior operation. Equal-clock pose
changes and offscreen turnover do not add heat; rewinding resets the illustration.
Pause freezes both travel and heat. Heat persists between bodies.

Powered cabinet surfaces start at 34 C and approach 38 C, rising toward 44 C
under full conveyor duty with an 18 s response. LCD warmth is lower. Only fitted
regions warm: unrelated objects sharing the source materials remain ambient.

Five existing surface profiles use the existing GPU capsule evaluator, with no
new meshes, stencil IDs, shader variants or LUT rows. The holder fields move with
the actual meshes. The thermal camera and observer see the same surface heat;
visible/NIR emission is unchanged. Legacy visual-cycle fixtures retain their
previous temperatures. A missing optional private descriptor logs a preparation
warning and leaves these additions disabled.

Roof banks now emit 1,200 lumens each in the imported cell (previously 1,800),
with proportional tube emission. Bright observer mode with Lumen uses -1.2 EV
instead of -0.7 EV. Dim lighting and all infrared exposures are unchanged.

On a prepared checkout, render the focused fixture with:

```powershell
.\unreal\Scripts\verify_enhancements.ps1 -Mode EquipmentHeat -Output artifacts/equipment-heat-review
```

It checks heating, pause, cooling and fixed NIR isolation, and saves motor and
cabinet closeups plus a bright-floor overview. Also run the graphics isolation,
legacy welding/lighting and actual model process fixtures for these changes.
