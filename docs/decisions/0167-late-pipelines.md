# 0167 — Fewer pipelines after the warm-up: the sun's bits, tile culling's while it is off, a shelf of the scene's meshes

Status: **KEEP** (the bits: exact, under `frame/stable_scene_bits`, on; the shelf: a property of a node, on) · Branch
`fellow/geometry` · Code: `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}`
(`_fellow_stable_sun_masks()`, the soft-shadow and tile-culling bits),
`modules/fellow_world/fellow_pipeline_warmer_3d.{h,cpp}` (`shelf`), `FellowPipelineWarmer3D.xml` · Checks:
`benchmarks/project/bench/pipeline_states_check.gd` (`--sun_size`, `--contact`); Slice Lab's soak with
`FELLOW_SPECIALIZATION_LOG=1` (`--slice_warmup_shelf=0` for the warm-up without the shelf).

## Problem

Point 4 of the plan towards AAA: no pipeline compiled in the middle of a game. Decisions 0161 (bits that do not follow
what a frame holds) and 0162 (the warm-up) left the slice's 20-minute soak on the RTX with 151 pipelines compiled after
its warm-up (f363), most in the first minute with the fire burning, at the first snow and through the day's cycle.
Logged on lavapipe (`FELLOW_SPECIALIZATION_LOG=1`, the specialized pipelines a draw asked for before they were
compiled, with their keys) over a 4-minute soak of the slice whose weather turns every 10 s, its fire is lit again
every 15 s and its day lasts 80 s (job `spec1`, 160 × 90, the terrain and its foliage, the warm-up):

* **The sun setting.** At dusk every material in view asked for new pipelines, and at dawn again: the sun's node is
  hidden below the horizon, and with no directional light Godot's directional soft-shadow bit
  (`use_directional_soft_shadows`) and FellowEngine's contact-shadow bit (`fellow_contact_shadow`, its mask is made
  only for a directional light) turn off.
* **Light tile culling while autotune has it off.** Decision 0161 kept tile culling's bit on in frames without omni or
  spot lights; in frames with them it follows whether tile culling runs — off while autotune measures it or has
  chosen it off. Then the frames with and without lights in view asked for both pipelines of every material.
* **Meshes out of the warm-up's views.** The terrain's grass is scattered in cells around the camera (90 m): from
  the warm-up's place no grass was in view, and its material compiled when the camera first came to a meadow.

After the warm-up had finished (frame 40), 9 specialized pipelines were asked for before they were compiled: the
grass's 3 at its first meadow (depth, shadow and colour passes), tile culling's other variant for the terrain, a town
material and the grass (3), and the grass's again at dawn (3).

## Change

**The sun's bits** (`frame/stable_scene_bits`): in a frame without a directional light, Godot's soft-shadow bit is
kept on once it has been on, and the first directional light's traced and contact shadow masks' bits likewise. The
code they select is in the directional lights' loops, which run no iteration without a directional light; the masks'
binding falls back to a default texture (as it did). The same image.

**Tile culling's bit while it is off**: decision 0161's rule — the bit on in frames without omni and spot lights — now
applies only while tile culling is on (as a frame with such a light would draw); while it is off (autotune's choice),
the frames with and without lights share the pipelines without the masks. The same image.

**The warm-up's shelf** (`FellowPipelineWarmer3D.shelf`, on): after its six directions the warmer collects every
distinct mesh and material set (and kind of instance: MultiMesh format, shadow casting, GI mode, layers, transparency)
of the world's visible MeshInstance3D and MultiMeshInstance3D nodes and of each FellowTerrain3D's foliage kinds, and
draws copies of them, 36 to a page (at most 576), each page for `frames_per_view` frames from a view of its own 4 km
above the camera. The copies have a visibility range of 40 m: no other camera draws them, nor their shadows (the
range applies to the shadow casters too). A page's copies are made when it is shown and follow their sources' meshes
and materials every frame: a node may change them while the warm-up runs and free what it no longer uses (the first
version kept copies of the whole shelf from the start: the campfire's log, a burnable, freed its material's texture
when it began to burn, and the copy of the log drew with it — errors for each frame of the warm-up). Hidden nodes are
left out (a burnable's ash heap is hidden until there is ash: its material's textures are made then), and skinned
meshes (their vertices come from their skeleton: another vertex format). The copies are lit by the scene's directional
lights and none of its positional lights; materials near omni lights still compile those pipelines when first seen.

## Verification

* `pipeline_states_check.gd` on lavapipe (decision 0161's 21 states, the sun set and risen among them), with the sun's
  soft shadows (`--sun_size=0.5`) and contact shadows (`--contact=1`): with `stable_scene_bits` 5 specialized
  pipelines after the warm-up, all at the reflection probe's first capture, none at night or day (without the setting
  28, 3 at night); each state's picture the same with the setting on and off, 21 of 21.
* The image regression suite: every picture exact.
* Slice Lab's soak on lavapipe, as `spec1` (4 minutes, the weather every 10 s, the fire every 15 s, an 80 s day; job
  `spec3` with this decision), no engine error:

| Slice Lab's 4-minute soak, lavapipe, 160 × 90 | before (`spec1`) | 0167 (`spec3`) |
|---|---|---|
| the warm-up: frames; pipelines compiled | 41; 81 | 43; 76 (17 meshes on its shelf, one page) |
| specialized pipelines a draw asked for before they were compiled, after the warm-up | 9 | **1** |
| pipelines compiled after the warm-up, every kind | 23 | **9** |

The one left is the grass's depth pass (the terrain's foliage cells draw it with another vertex format than a
MultiMesh of the same mesh). Of the 23 and the 9, 14 and 8 were compiled in one second late in the run without a draw
waiting for them: autotune preparing an evaluation's pipelines before its frames (decision 0068).

On the RTX laptop (job f364, release template):

* `pipeline_states_check.gd` (1280 × 720, soft and contact shadows): with `stable_scene_bits` 5 specialized pipelines
  after the warm-up, all at the probe's first capture, none at night; without it 28 (3 at night) — lavapipe's counts.
* Slice Lab's 20-minute soak as f363 (1080p, the terrain and its foliage, the warm-up, the weather every minute, the
  fire every two):

| the slice's 20-minute soak, RTX, 1080p | f363 (before) | f364 (0167) |
|---|---|---|
| the warm-up: frames; pipelines | — | 35; 75 (17 meshes on its shelf) |
| pipelines compiled after the warm-up | 151 (149 specialized, 2 surface) | **88** (86, 2) |
| of them while it snowed; while it rained (every such period) | 66; 31 | 8; 14 |
| hitches after the warm-up (twice the median and 10 ms over it) | 4 | 10 |

The pipelines left: 27 in the storm, 20 in the blizzard, 16 overcast, 14 in the rain — the release template's soak
does not log their keys; omni lights (the lightning's), particles and skinned meshes are not on the shelf — and 20 of
them in two minutes late in the run, as autotune's evaluations were on lavapipe. Of f364's 10
hitches, 9 are the GPU's fence (14-20 ms waited in `rd_fence_wait_usec`, as f363's) and one of 89.6 ms at the first
rain: not a pipeline but the rainbow's shader, compiled on the main thread as the frame drew the first bow (77 ms of glslang: the
first session of a new build, whose shaders miss Godot's shader cache — f363's build had been run with rain before);
decision 0169 makes it ahead, on the worker threads.

## Decision

**KEEP.** On lavapipe's slice the draws that waited for a pipeline after the warm-up fall from 9 to 1, at the same
image; on the RTX the slice's soak compiles 88 pipelines after its warm-up instead of 151 (while it snowed 66 → 8); the
warm-up costs 2 frames more for its shelf. Not done: positional lights near shelf copies (materials lit by omni and spot lights
compile those variants when first seen), skinned meshes out of the six views, hidden nodes, particles.
