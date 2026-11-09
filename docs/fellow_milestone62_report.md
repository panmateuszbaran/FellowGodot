# FellowEngine — milestone 62 report (milestone 61's two open topics, without a GPU)

Status: **FINAL** for the cloud's part (2026-11-09): checked on lavapipe, headless; the RTX's confirmation is a soak
when the laptop's GPU may be used. Branch `fellow/geometry`. Decision records: additions to
`docs/decisions/0167-late-pipelines.md` and `0169-kernel-spirv-cache.md`.

## 0. Result

Milestone 61 left two things for later: which pipelines the RTX's slice still compiled after its warm-up (88 in 20
minutes, about 50 of them in the storm and the blizzard), and the particle process materials a baked export still
compiled at its first start. Both done on the cloud's CPU:

* **The pipelines after the warm-up, named and mostly gone** (decision 0167). Slice Lab's soak on the RTX's timeline,
  on lavapipe, with the specialization log: of milestone 61's 20 draws that asked for a pipeline not compiled yet, 15
  were a lightning's flash at night (the frame's only directional light, without an angular size: Godot's soft-shadow
  bit fell), 2 a burnable's ash heap at its first ash, 3 autotune's evaluations. `stable_scene_bits` now keeps the
  soft-shadow bit with directional lights without a size (the same image: their soft path is the hard one), and the
  warmer's shelf copies hidden meshes their nodes mark as ready (the ash heap, the lightning's bolt and glow). After the
  warm-up: **37 → 20 pipelines** with autotune on (the rest its evaluations, prepared ahead), **2** with it off;
  draws that asked **20 → 3**.
* **The rain's particle process materials baked** (decision 0169): `FellowRain3D`'s baking provider gives the shader
  baker the particles shaders of its four process materials (the drops with and without splashes, the splashes, the
  flakes without and with the scene's wind). A baked rain export compiles **0** shaders at its first start (3 before).

## 1. Development

* `servers/rendering/renderer_rd/storage_rd/light_storage.{h,cpp}`: `fellow_directional_sized` (a directional light of
  the frame with an angular size); `forward_clustered/render_forward_clustered.cpp`: the soft-shadow bit kept without
  one (decision 0167, under `frame/stable_scene_bits`).
* `modules/fellow_world/fellow_pipeline_warmer_3d.cpp`, `FellowPipelineWarmer3D.xml`: hidden nodes with the metadata
  `_fellow_shelf` on the shelf; `modules/fellow_fluid/fellow_burnable_3d.cpp` (the ash heap) and
  `fellow_lightning_3d.cpp` (the bolt, with a degenerate triangle in its vertex format until the first strike, and the
  glow) set it.
* `modules/fellow_fluid/fellow_rain_3d.cpp`: `_make_process_materials()`, shared by the node and
  `fellow_bake_shader_codes()`, which adds the process materials' particles shader codes (decision 0169).
* `benchmarks/project/bench/pipeline_states_check.gd`: `--flash=1`, six more states with a lightning's flash by day
  and at night.

## 2. Verification

* `pipeline_states_check.gd --sun_size=0.5 --contact=1 --flash=1` (lavapipe, 320 × 180): the flash alone at night
  compiled 3 pipelines, now 0; 5 after the warm-up in all (the probe's, as before); the 27 states' pictures the same
  with `stable_scene_bits` on and off, bit for bit.
* Slice Lab's 20-minute soaks (below): no engine error.
* A Linux export of the regression suite's rain scene (release template, the shader baker on), started twice on
  lavapipe from an empty user directory: no shader compiled at the first start nor the second.
* The image regression suite (editor build and release template): 24 pictures exact (the lightning scene with its bolt's
  placeholder, the rain with its shared process materials).

## 3. Measurements

Slice Lab's soak on lavapipe on the RTX's timeline (20 minutes; the weather every minute, the fire every two, a
10-minute day; the terrain and its foliage, the warm-up with its shelf; 160 × 90, release template;
`FELLOW_SPECIALIZATION_LOG=1`):

| | pipelines after the warm-up | draws that asked for one not compiled |
|---|---|---|
| milestone 61 | 37 (35 specialized, 2 surface) | 20: 15 the flash at night, 2 the ash heap, 3 autotune |
| the flash's bit, the heap on the shelf | 22 (20, 2) | 5: 4 autotune, the heap in a positional light's shadow |
| the same, autotune off | 5 (3, 2) | 2: the lightning's bolt, the heap's shadow |
| the bolt and glow on the shelf too, autotune off | **2** (2, 0) | **1**: the heap's shadow |
| the bolt and glow on the shelf too, autotune on | **20** (20, 0) | **3**: 2 autotune, the heap's shadow |

None of these made a draw wait (specialized pipelines compile beside the ubershader; on the RTX, `pipelines_draw` was 0
for all of f364's 88). What is left is autotune's: each change of the weather moves the frame time, autotune evaluates
its tunables again and compiles their other variant of the pipelines in view, ahead of the evaluation's frames
(decision 0068).

## 4. Limits

* Measured on lavapipe: its frames are about a second, so a lightning flash lasts a dozen frames there, and autotune's
  evaluations follow its frame times, not a GPU's. The RTX's own count after the warm-up is for its next soak.
* Autotune's evaluations still compile their variants in play (about 2.7 MB a pipeline on the RTX); a game that wants
  none turns autotune off or keeps its choices.
* The shelf's copies are lit by directional lights only: a mesh in an omni or spot light's shadow compiles that pass
  when it first casts one (the ash heap near the campfire's light).
* A burnable's burning material is still not baked into exports (written from its object's material when it burns).

## 5. Next

The RTX's soak with milestone 62's template (the pipelines after the warm-up, against f364's 88); the burning
material in baked exports.

## 6. Addendum: the showcase after the owner's first play

The owner played the showcase on the laptop and reported four things; each answered on lavapipe.

* **The campfire's smoke ended in a hard edge** (the top of its 4 m box). `FellowGasVolume3D.edge_fade` (new, metres,
  0 by default — every existing picture the same): the smoke is drawn faded out within that distance of the box's
  faces, all but a closed floor — in the camera's march and in the light volume that shadows it, not in the
  simulation. The showcase's campfire: a 4 × 6.5 × 4 m box (72 cells up) open at the sides and the top, the smoke
  dissipating at 0.22 /s, faded over 0.7 m: it billows, thins and is gone before any face; no edge shows (pictures
  from 16 m, lavapipe).
* **"At some angles the screen is darker and flickers."** A 360° turn of the camera at 36 °/s (lavapipe, the clock
  stopped, the picture's mean luminance per frame): it changes smoothly with the view — 0.39 towards the sun (the
  houses' shaded sides), 0.52 with the sun behind — at most 0.004 from one frame to the next. Sweeping the camera's
  pitch over the mouse's range at 30 °/s: one-frame dips of 0.016-0.023 (the 10th percentile 0.37 → 0.29) at the same
  pitch both ways, about 15° up — looking up swung the camera down until its spring arm met the ground, among the
  grass, and blades next to the lens filled the picture for a frame and left it. The cameras of both characters now
  keep 0.9 m over the terrain (they slide over it) and their spring arms collide with the buildings only: the same
  sweep, at most 0.004 between frames. F3 turns FellowEngine's effects off one at a time (contact shadows, light
  shafts, bloom, clouds, aerial perspective, SSAO, TAA, autotune), for anything the RTX shows that lavapipe does not.
* **The character was slow.** MotionBricks' network walks at about 1.4 m/s and runs up to about 3.5 m/s (faster, it
  stumbles). The showcase now plays its motion 1.3 times faster than recorded while it moves (the feet stay planted:
  the same motion, quicker): walking about 2 m/s, running 4.3 m/s.
* **A roll and a jump.** The network makes neither; the script draws them over its pose. A jump (Space): the body
  raised on a parabola (4 m/s up, 0.73 m at the top), the legs drawn up and the arms forward on the way up, down again
  for the landing. A roll (C, the gamepad's B): the stride undone, the body tucked into a ball (hips, knees, spine,
  neck), lowered until the ball's lowest point is on the ground and turned once about its centre in 0.95 s while the
  network runs on underneath; every rotation is about the body's right axis, set through the parents' poses.

Also: the campfire burns from the start (`prewarm_steps` 240), and `prewarm_steps` no longer overflows the GPU's queue
of steps (64 until the next frame) when several physics ticks come before a frame — a slow first frame, or a fixed
frame rate below the physics' (Movie Maker at 30 fps: 2 × 33 steps a frame, the oldest dropped with a warning): a
tick's prewarm batch is what the queue still holds since the last frame drawn, and the node asks for frames while it
prewarms.
