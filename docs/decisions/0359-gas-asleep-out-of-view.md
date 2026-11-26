# 0359 — Gas volumes asleep out of view; the rooftops' frame pass by pass

Status: **KEEP** · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_gas_volume_3d.{h,cpp}`
(`sleep_offscreen`, `_out_of_view()`), `doc_classes/FellowGasVolume3D.xml`; `showcase/scripts/rooftops_fire.gd` (the
chimneys asleep out of view); `showcase/tools/climb_look.gd` (`--passes=1`: the GPU's time pass by pass; `--perf=1`
drawing every frame again).

## Problem

The owner: "work through the night on AAA across the whole engine." Where does the rooftops' frame go? `climb_look.gd
--passes=1` (new) reads the engine's own pass markers (`RenderingDevice.get_captured_timestamp_*` with
`rendering/fellow/profiling/pass_timestamps`) on the RTX at 1920x1080 (job f611, 240 frames a view):

| pass (ms of GPU, fenced) | the square | in the snow | a chimney | at night | in the osteria | the demo |
|---|---|---|---|---|---|---|
| opaque | 3.15 | 3.52 | 2.04 | 2.70 | 2.75 | 2.54 |
| **gas simulation** | **3.04** | **3.13** | **3.06** | **3.07** | **3.15** | **3.09** |
| depth pre-pass (+ GI) | 1.59 | 1.77 | 1.18 | 1.48 | 1.40 | 1.27 |
| directional shadows (+ GI) | 1.40 | 1.45 | 0.69 | 1.50 | 0.80 | 1.23 |
| clouds | 0.94 | 1.38 | 1.14 | 0.94 | 0.11 | 0.76 |
| omni lights' shadows | 0.86 | 0.87 | — | 0.86 | 1.89 | — |
| screen gather | 0.32 | 0.32 | 0.28 | 0.32 | 0.35 | 0.41 |
| all the markers | 13.4 | 14.6 | 10.2 | 13.0 | 12.6 | 11.3 |

The gas simulation — the brazier's, three chimneys', the osteria's fireplace — took the same 3 ms whatever was in view;
its stages (`gas_profile_passes`): the pressure solve 4.1 ms of 5 fenced, its multigrid's levels one after another.
Fenced, each pass waits for the one before: unfenced, the volumes' dispatches overlap one another and the frame's other
work — the frame with its brazier and chimneys 9.77 / 9.94 ms of GPU, without them 9.01 / 9.11 (`--perf=1`, 480
frames, twice): 0.8 ms of GPU, and 1.0 ms of the render thread's CPU (2.61 / 2.70 against 1.64 / 1.56 ms: some 250
dispatches recorded a frame). A chimney's plume behind the camera cost as much as one in front of it.

## Change

* **`FellowGasVolume3D.sleep_offscreen`** (seconds; 0, never, by default): once the volume's box — grown by a tenth of
  its size and a metre, so that a plume coming into view is already moving — has been wholly outside the viewport's
  camera's frustum for that long, the gas is not stepped (as beyond `sleep_distance`, milestone 68): its state kept and
  still drawn (its shadow on the scene), its sound on, its burning objects waiting; it goes on as soon as the box comes
  into view. Not in the editor. For smoke whose light does not matter out of view: a fire's light comes from its
  simulated flames and would stop flickering on what is in view.
* **The rooftops' chimneys** sleep a second after they leave the view (the brazier and the osteria's fireplace light
  the square and the room: not).

## Measurements

The RTX, 1920x1080, `--perf=1` (480 frames after 3 s), each view with the chimneys asleep out of view and without
(`--gas_set_chimney=sleep_offscreen:0`), twice, alternating (job f613):

| view (chimneys asleep) | GPU ms, asleep | GPU ms, awake | render thread's CPU ms, asleep | awake |
|---|---|---|---|---|
| the square (1 of 3: CasaOcraBassa, behind) | 9.90, 10.17 | 9.90, 10.13 | 2.50, 2.76 | 2.72, 2.79 |
| the brazier (3 of 3) | 9.69, 9.78 | 9.54, 9.59 | 2.27, 2.40 | 2.33, 2.68 |
| in the osteria (1 of 3: two seen through the arches' frustum) | 9.14, 9.21 | 9.12, 9.15 | 2.66, 3.06 | 2.93, 3.33 |
| a chimney (2 of 3) | 6.85, 6.63 | 6.88, 6.52 | 1.40, 1.68 | 1.63, 1.85 |

On this GPU no frame time is saved: the plumes' steps overlap the frame's other work (the brazier's view, all three
asleep, within its runs' spread). The render thread records less: 0.1-0.3 ms (its runs' spread is as large). Kept for
that and for scenes whose GPU has no room to overlap them, off by default; the frame is the same until a volume sleeps
(the regression suite's fire, rain and weather_gi identical). The demo's path unchanged.

## Decision

KEEP.

## Rejected / not done

* The gas's steps in fewer, wider dispatches (the volumes' passes interleaved, the multigrid's coarse levels of all of
  them at once): unfenced the GPU already overlaps them; the render thread's 1 ms of recording is the larger share.
* A chimney's plume stepped every other tick out of view (half the cost, never frozen): the plume behind the camera
  is not seen at all.
