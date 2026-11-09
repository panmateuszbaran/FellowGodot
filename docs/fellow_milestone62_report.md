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
