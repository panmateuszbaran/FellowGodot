# FellowEngine — milestone 152 report (climbing across, on narrow holds, over eaves and parapets onto the roofs; the RTX's rare failure narrowed)

Status: **FINAL** (2026-11-19). Branch `fellow/geometry`. The climbing ran on the CPU and lavapipe in the cloud; the
RTX ran `f516` and `f517` for decision 0270 until the owner needed the laptop (09:06). `f518` is held in `.tools/wip`.

## 0. Result

* **Climbing from hold to hold goes where Assassin's Creed's does** (`FellowTraversal3D`, all opt-in, in
  `hang_reach_up`'s mode):

  | decision | what | how |
  |---|---|---|
  | 0285 | across a gap to a ledge to the side, or up and across | `hang_reach_side` (0.8 m in the showcase), `TRANSFER_REACH_SIDE` (0.8 s): the leading hand to the near end, the body swinging across under the hands, the feet stepping across the wall |
  | 0286 | narrow holds (a beam's end, a corbel) | `hold_min_width`: the hands closer together, 5 cm in from the hold's ends (`spread`); caught, reached and leapt to like any other |
  | 0287 | onto a roof over its eave | a hold under the eave caught where the hands have room; the mantle's room on a sloping top fixed (it had been wrong over 23°) |
  | 0288 | the next hold beside the hands, holds close over each other, the eave's lip far out | a reach up and across; a second look for a top from over the face found; the up probes from 0.45 m; the lip looked for out to 0.85 m; 0.12 m for the hands over a hold |
  | 0289 | over a parapet onto the terrace behind it | the far face and the floor behind looked for; the mantle over the top, then over the far side and down; the push held through a leap climbs on |

* **The showcase's houses are climbed to their roofs.** The player sprinting at CasaCrema's front and pushing on goes
  from the street to the roof in 5 s: the impost band, a leap to the rustication's top, up and across to the first
  floor's sill, the pediment's base, the second floor's sill and hood, out and up to the eave's lip, over it onto the
  tiles. CasaBianca and CasaRosa: over their parapets onto their terraces (11.2 m) in 6 s. Before: caught at 2.2 m, at
  4.4 m at most. The pediments' bases and the eaves' fascia are in the houses' collision (models and scene rebuilt).
  The mannequin has a reach across (`hang_across_l/r`, 0.8 s). The demo climbs CasaCrema in one take.
* **The RTX's rare failure** (decision 0270), narrowed by decision 0284's diagnostics:

  | job | the hot workers' gate | failures |
  |---|---|---:|
  | `f510`, `f515` | as it is (open in 1-2 % of the frames) | 7 in 100 |
  | `f516` | held open | 0 in 30 |
  | `f517` | held closed (`debug/gate_open_every` -1) | **4 in 33** |
  | `f517` | as it is, interleaved | 0 in 33 |

  The nine earlier failures' reports, read again, had the gate closed in the failing frame; `f517`'s hangs came in runs
  where the gate **never** opened. So nothing the open frames leave behind: the closed frame's own path fails — its
  lists recorded on cold workers (pool tasks), replayed whole into the primary command buffer, submitted by the render
  thread. `f518` splits those when the GPU is free again.

## 1. Checks (cloud)

* **`climb_check.gd`, the editor build:** 36 lanes, seven new (`side_reach`, `narrow_holds`, `eave_roof`, `eave_deep`,
  `beside_reach`, `stacked_holds`, `parapet`); each climbs its lane to the top, no frame of the capsule in the world.
  The 29 lanes of milestone 151: value for value against its results, on GodotPhysics3D and Jolt (Jolt by a temporary
  `override.cfg`). Jolt found a wall's top 1.5 cm under a roof that GodotPhysics3D did not: the hands' room over a hold
  (decision 0288) is checked for it. New options `--only=` and `--times=` render picture strips of one lane.
* **Regression suite:** the scenes `climb`, `ledge_corners` and `traversal` exact after each decision.
* **The class reference:** `hang_reach_side`, `hold_min_width`, the `spread` key and `TRANSFER_REACH_SIDE` documented;
  the order matches `--doctool`; `xmllint` and `make_rst.py` clean.
* **The rooftops demo headless:** the whole demo runs through on the same path as before, with the new shots of
  CasaCrema's climb.
* **Video:** the demo rendered with Movie Maker on lavapipe (`rooftops_climb_m152.mp4`).
* **Decision 0284 on lavapipe:** 1 200 transparent meshes, 36 lists recorded in parallel in 40 frames;
  `gate_open_every` 1: all 36 replayed in secondary command buffers, 4: 9, -1: none, 0: 34 (as it is); the history
  printed as `[o..o..o..]`, `[.........]`, `[ooooooooo]`.

## 2. The owner's notes on the video, for milestone 153

"Much better", and two things left:

1. **The houses further off are still poor and ugly** — the Skyline model's blocks: flat fronts, no eaves or roofs that
   read as tiles, no window depth.
2. **The climbing animations are unrealistic and the transitions between them strange** — seen in the frames: the arms
   held out level during leaps (the hands' IK weight dips mid-leap), the knees drawn up to the chest when braced
   (`hang_brace_height` 0.65 m is too high for the mannequin), the legs splayed in leaps, and plain crossfades between
   clips with no weight shifting through them.

## 3. Next

* Milestone 153: the climbing's body motion toward AAA (a spring on the hips and chest driven by the body's
  acceleration, smoothed limb targets, the hands on an arc through a leap at full weight, a lower brace, better clips
  and blends); the distant houses rebuilt with real roofs, eaves, cornices, window depth, shutters and chimneys.
* The GPU, when the owner allows it: `f518`, then a fix for the closed frame's path.
