# Observer collider bounds

Press **F8** or **Xbox left-stick click** to cycle **off â†’ physics bodies â†’ physics + query bounds â†’ off**. The overlay is independent of observer modality, palette, headlamp, motion pause and viewpoint bookmarks. It starts off. Lines draw through opaque surfaces, exposing otherwise hidden colliders; the scene stays visible.

| Color | Meaning |
| --- | --- |
| Green | Static collision supports, tunnel walls, floors and rails |
| Cyan | Kinematic moving carriage collision boxes |
| Orange | Dynamic physics bodies such as curtain segments, including while paused |
| Purple | Query-only mesh bounds, including the imported robot and body-panel meshes used for spark traces |

The current imported scene has 114 physics collider components when a car is present: 10 carriage boxes, 84 curtain segments and 20 portal supports. Adding query-only meshes gives 186 bounds. During the empty-car interval, the car disables ten boxes and 31 panel/holder queries, leaving 104 physics and 41 query-only bounds. Decorative clamps, hoods, props, smoke and spark-render instances have collision disabled and are not given fictitious colliders. The procedural reference may show no boxes because its visual components have collision disabled.

`UBoxComponent` uses its exact unscaled extent and current transform. Other simple shapes use the aggregate collision envelope. Complex mesh queries use mesh-local enclosing bounds; when simple and complex collision coexist, the all-query view includes both envelopes. These are diagnostic bounding boxes, not a replacement rendering of every convex hull or triangle. Disabled collision is excluded even if the mesh remains visible; hidden active proxies remain included. Dynamic colors describe the body's configured category, while metadata separately records whether it is currently simulating.

The implementation lives in `DarkFactoryColliderOverlay.cpp` and the observer HUD. It caches weak component references once per second while enabled, checks collision state and transforms each frame, handles instances separately, and caps output at 512 boxes / 6,144 edges. Near-plane and viewport clipping prevent behind-camera or offscreen edges from producing invalid screen coordinates. Off mode skips discovery and drawing. HUD lines are rendered after scene postprocessing, using the normal GPU canvas line path; there are no debug scene actors, additional camera captures or image readbacks. Fixed NIR and thermal scene captures never include the HUD.

## Reproduce the check

```powershell
.\unreal\Scripts\build.ps1 -SkipContent
.\unreal\Scripts\run.ps1 -Scene welding -ColliderSmoke -Output artifacts\collider-overlay
python scripts/verify_collider_overlay.py --directory artifacts/collider-overlay --output artifacts/collider-overlay/verification.json
```

The rendered check compares off/physics/all/restored observer views in all four modalities, checks fixed sensor PNG parity, and samples moving/hidden/restored car states and near-camera clipping. Generated screenshots and reports remain private under ignored `artifacts/`. Validation on 2026-09-13: the UE 5.8 Development build, 34 Python unit tests and all 8,003 independent collider checks pass. Twenty rendered views cover all observer/overlay combinations, moving/hidden/restored car state and an inside-car near-plane view. Actual dim, thermal and close-camera screenshots were reviewed. The ten carriage proxies translate and restore correctly; the expected 114/72 and 104/41 physics/query inventories pass. Maximum independent corner-transform error is 1.14e-13 cm. Fixed NIR and raw thermal PNGs remain byte-identical across every overlay toggle, and turning it off restores the observer scene pixels exactly. Private evidence is in artifacts/collider-overlay-v1.
