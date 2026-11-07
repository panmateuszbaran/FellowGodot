# FellowEngine changelog

FellowEngine is a fork of Godot 4.7.2-stable. Its work is on the Forward+ renderer (and, from milestone 15, on
skeletal animation, physics and new nodes for smoke, fire, burning, water, rain, wind and far objects), measured on an
RTX 5070 Ti Laptop GPU at 1920x1080 against stock Godot 4.7.2. Every change sits behind a project setting under
`rendering/fellow/`, or in a node that does nothing unless a scene contains it; the settings are documented in
[docs/FELLOWENGINE_OPTIONS.md](docs/FELLOWENGINE_OPTIONS.md) and each decision in [docs/decisions/](docs/decisions/).
"Exact" below means the same pixels (or physics state) as without the change: bit for bit on lavapipe (a CPU Vulkan
driver), bit for bit or at the run-to-run floor on the RTX. Percentages against stock are frame time unless noted.
Godot's own changelog (the upstream 4.7.2 changes) is [CHANGELOG.md](CHANGELOG.md).

## Milestone 60 — the main thread's CPU (2026-11-06)

- `rendering/fellow/animation/shared_no_override_pose`, `animation/lazy_simulator_poses` (on, exact), decision 0163:
  a skeleton without overrides copies each bone's global pose into the deprecated one without overrides instead of
  multiplying again; the compatibility PhysicalBoneSimulator3D of a skeleton without physical bones copies the bones'
  poses only when one is bound or read. Crowd Lab headless (128 characters, release template): the main thread's
  instructions −10.4 % (AnimationTrees blending −6.4 %), its CPU time a frame −7.3 % (−4.4 %), the same bone poses bit
  for bit. Rejected: the keys' part of each rotation track's slerp kept per thread — fewer instructions, but more CPU
  time (its table's cache misses cost more than the `acosf()` it saved).
- The main loop's frame step split in FellowEngine's stats (`main_frames`, `main_process_usec`, `main_draw_usec`):
  `Performance.TIME_PROCESS`, which the bench reported as the main thread's process time, is the largest of each
  second's steps with the RenderingServer's sync and draw — the frame's rendering and its waits for the GPU with the
  default thread model. `run_benchmarks.py` reports `scene_process_ms` and `main_draw_ms`.
- Crowds on worker threads: Crowd Lab `--crowd_groups=N` (Slice Lab `--slice_crowd_groups`) puts the characters in N
  sub-thread process groups (Godot's `Node.process_thread_group`), each posing its characters and updating their
  skeletons on a worker thread. The same bone poses bit for bit as on the main thread (AnimationPlayers and blending
  AnimationTrees, every FellowEngine animation shortcut on; `animation_check.gd --groups=N`), the same pictures on
  lavapipe, no error in a debug build.
- With the separate render thread (decision 0165): `FellowWater3D`, `FellowRain3D` and `FellowShallowWater3D` free their
  RenderingDevice resources on the render thread (before: 80 errors and leaked textures, uniform sets and shaders at
  the slice's exit, and the same for any of them freed during play). And a deadlock in Godot's shader compilation
  fixed: a thread waiting for a version's compile group while holding the version's mutex, with every worker blocked on
  that mutex or busy (the cloud's two-thread pool, one of them the render thread: two runs of four hung); the waiter now
  compiles the group's variants no task has started itself. The same shaders; the image regression suite exact with
  its shader cache warm and cold.
- The pipeline cache's size queries and saves off the RenderingDevice's lock (decision 0166): a save copies and
  checksums the whole Vulkan pipeline cache (hundreds of MB in FellowBench) and held every thread's device calls while
  pipelines compiled in the background. The slice's 20-minute soak on the RTX: the largest frame 397 → 20 ms, hitches
  16 → 4, none over 30 ms, with more pipelines compiled than before.

## Milestone 59 — pipelines compiled ahead (2026-11-06)

- `rendering/fellow/frame/stable_scene_bits` (on, exact), decision 0161: the colour passes' specialization bits that
  follow what a frame holds (absent decals, probes and lights, the positional lights' loop variants and tile culling,
  the weather's wetness, the clouds' shadow) keep the feature's code once the feature exists or has been in a frame.
  lavapipe: a scene's 21 changes (rain, lights, decal, probe, night) compile 5 specialized pipelines instead of 28,
  every state's picture bit-exact; the image regression suite exact. `FELLOW_SPECIALIZATION_LOG` lists the pipelines
  a draw asked for before they were compiled.
- `FellowPipelineWarmer3D`, decision 0162: while a scene loads, its world drawn from the camera's place in six
  directions into a small viewport set up as the game's, until nothing more compiles. lavapipe: a camera turning to
  24 unseen objects compiles 0 pipelines instead of 7 (MSAA: 0 instead of 13, 6 of them compiled while the frame
  waited). The FellowEngine menu's "Add Pipeline Warm-up"; Slice Lab `--slice_warmup=node`.
- `bench/pipeline_states_check.gd`, `bench/pipeline_warmup_check.gd`. The RTX's measurements (the general pipelines'
  cost, the slice's soak) wait for the GPU.

## Milestone 58 — sculpting and painting the terrain (2026-11-06)

- `FellowTerrain3D.sculpt()` (raise, lower, smooth, flatten) and `paint()` (a layer's weight, the weights still adding
  up to 1; the splat maps made when there are none), decision 0160: only the region under the brush is updated — the
  queries at once, the textures and collision shapes once at the frame's end, the foliage over it scattered again —
  and the result is the terrain a rebuild from the edited images makes, every texture and mip bit for bit (lavapipe,
  read back). `blit_heightmap_region()`, `blit_splat_region()` write rectangles back (undo, stamps). A dab costs 0.02-0.08 ms of the main thread on the cloud CPU whatever the
  map's size (a rebuild, what an edit cost before: 28-455 ms for 1-4 km); the RTX's measurement is still to run.
- In the editor: a selected terrain's toolbar in the 3D view (Raise, Lower, Smooth, Flatten, Paint, the layer, the
  brush's size and strength), the brush drawn on the ground, one undoable action a stroke; the FellowEngine menu's
  "Add Terrain (256 x 256 m, to sculpt)".
- Fixes found with it: a data rebuild of a drawn terrain freed the patches' mesh under them (errors, nothing drawn);
  the foliage cells' tasks now read their own copies of the heights and weights, so edits never race them and a
  rebuild no longer waits for them (a first paint stroke over grown foliage: 3.6 s → 9 ms on lavapipe).
- `bench/terrain_edit_check.gd`; the editor check's terrain brush and setup.

## Milestone 57 — terrain and foliage (2026-11-06)

- `FellowTerrain3D` (decision 0158, module `fellow_world`): a height field (an `Image` of metres) drawn with
  continuous levels of detail — patches whose vertices morph onto the next coarser grid, reading a coarser mip as they
  do: no cracks, no popping — as one MultiMesh the GPU culls patch by patch, written into a second hidden one when the
  selection changes (TAA and FSR see a static ground). Up to 9 `FellowTerrainLayer` materials blended by two splat
  maps and their heights, triplanar per layer; collision (height map shapes, the finest level's triangles); height,
  normal and weight queries; a level-of-detail debug view; the editor's camera in the editor. Slice Lab's 1 km valley at
  1 m (16 times its 4 m chunks' vertices near the camera) on the RTX at 1080p: GPU 3.36 → 3.59 ms (+0.23), the render
  thread 0.16 ms shorter, the terrain's own main-thread time 17 µs a frame; no crack in any view of a 1 km or a 4 km
  map, physics rays within 0.08 mm of `get_height()`.
- `FellowTerrainFoliage` (decision 0159): plants and rocks scattered by the terrain over a layer, its slopes and
  heights, in cells around the camera made on worker threads, each its own MultiMesh culled per instance by distance:
  Slice Lab's trees and grass scattered by the terrain cost the same GPU time as placed by its script, the render thread
  0.12 ms less.
- Exact fixes found with the terrain: `FellowGasVolume3D` no longer copies the data of height map and concave shapes in
  its box every step; the directional shadow cache ignores changes of instances that cast no shadows.
- Slice Lab `--slice_terrain=node`, `--slice_foliage=node`; `bench/terrain_check.gd`; the image regression suite's
  `terrain` scene.
- After milestone 55 (decision 0156): the first snow's pipelines and particles shader and a relit burnable's mesh
  readbacks moved to the nodes' setup — on the RTX the first snow's worst frame 51 → 10.1 ms, a relighting's 17-22 →
  6-9 ms, 2 hitches in 5 minutes (both the GPU's fence).

## Milestone 56 — motion matching on uneven ground; mirrored and tagged clips (2026-11-06)

- `FellowMotionMatcher3D.ground_adaptation` (decision 0157, off by default): rays under the root and each foot's heel
  and toe; the root follows the ground, the feet are placed on it by the legs' IK, the hips lowered for the lower foot,
  a foot that is down turns to the slope. Against the root alone following the ground, a walking character's stance
  feet: 8° slope 28 -> 3 mm off (in the ground in 49 % -> 0 % of the frames), stairs 56 -> 13 mm, uneven ground
  27 -> 10 mm.
- `FellowMotionDatabase.build()` options `mirror` (each clip mirrored, left and right swapped: Matching Lab's foot
  sliding -18 %, trajectory error -6 %, a search twice as long) and `tags`; the matcher's `tags_required`,
  `tags_excluded`.
- Checks: `bench/ground_check.gd`, `bench/matching_tags_check.gd`.

## Milestone 55 — a vertical slice played for an hour (2026-11-06)

- Slice Lab (`--scene=slice`, decision 0156): a 1 km valley with a town of 711 buildings under HLOD and its street
  lights, a campfire with a burning log, a crowd, a lake, 9 000 trees and grass in the wind, weather turning through
  rain, storm and snow, a day turning to night. The bench's `--mode=soak` plays a scene in real time for minutes or
  hours: per second the frame times, the GPU's and render thread's time, memory and counts, pipelines compiled; the
  hitches with what compiled in them; growth per hour. `benchmarks/tools/soak/run_soak.py` samples the process's and
  the GPU's memory from outside, `soak_report.py` writes the CSV, a chart and the summary.
- The first hour on the RTX at 1080p: 251 fps, Godot's video memory 394 -> 395 MB, objects +2 — and 61 hitches, an
  error every four minutes and a 4.8 s freeze at the first start, all from the burning log. Fixed in
  `FellowBurnable3D`: lit again, it freed invalid texture IDs (new texture objects now) and compiled its shaders anew
  (from `FellowSharedShaders` now); its ash heap made its material at the first ash (now when it is configured,
  hidden). The hour again: 252 fps, 25 hitches (2 compiling pipelines, 31 before), no error, no frame over 21 ms in
  the first 25 s.

## Milestone 54 — an image regression suite (2026-11-06)

- `tools/fellow_regression/regression.py` (decision 0155): FellowEngine's features in twelve scenes (lights, shadows,
  materials, water, rain, snow, lightning, fire, HLOD and MultiMesh geometry, sky and clouds, ray tracing) rendered on
  lavapipe and checked against goldens, the release template against the editor build, and FellowEngine off against
  on; every picture bit-exact between runs and between the two builds. `--goldens` keeps another GPU's own.
- Found and fixed: a FellowGasVolume3D's gizmo and handles showed its box centred on the node (its floor is there);
  the FellowEngine menu's Fire, Smoke and Burning Box put their emitters below the box, emitting nothing. An emitter
  that does not reach into its volume's box now has a configuration warning. The clouds traced a quarter of the
  pixels per frame showed a checker of noise (each pixel's history lost): fixed, 45.3 -> 51.5 dB against the full
  trace (a pan 44.4 -> 47.5 dB).
- fellow_agent seeds the global random sequence (`--seed`): two captures of a scene are the same picture.

## Milestone 53 — the editor: scene opening, ready-made setups, grouped inspectors (2026-11-05)

- FellowEngine's nodes make the shaders, compute kernels, meshes and textures they all share once per session
  (`FellowSharedShaders`, decision 0154): a scene with water opened again in the editor 492 -> 4.9 ms, a second water
  body 270 -> 0.1 ms; rain, lightning, shallow water likewise. Exact.
- A FellowEngine menu on the 3D editor's toolbar: Weather, Fire, Smoke, Burning Box, Ocean, Lake, Flowing Pool, added
  wired up (no configuration warning), undoable.
- Inspector groups for shallow water, lightning, the world streamer, the motion matcher and the motion character.
- Measured: FellowEngine adds nothing to the editor's start (15.1 s on, 15.3 s off, lavapipe).

## Milestone 52 — the editor: idle, warnings, handles, tooltips (2026-11-05)

- An untouched editor draws nothing for FellowEngine's nodes unless they move (decision 0153): values written to the
  RenderingServer only when they change (lightning no longer redraws between strikes, rain no longer rewrites its
  drops' positions), passes not dispatched again with the same inputs, simulations asking for a frame per step
  instead of piling up work the editor never drew. The 3D toolbar's **Animate** toggle (editor setting
  `editors/3d/fellow/animate_nodes`) stills water, rain, lightning, wind, gas and shallow water in the editor.
- Configuration warnings for FellowEngine's nodes; `FellowEditorTools.get_configuration_warnings()`; fellow_agent's
  `check` reports every node's warnings.
- Handles (undoable) for emitters, water extent, shallow-water domain, streaming cell bounds, snow trackers and
  lightning; gizmos redraw when their properties change.
- Tooltips for all 296 `rendering/fellow/*` project settings (`doc/tools/fellow_settings_docs.py`).
- Fixed: the rain's shelter heightfield bound in the pass rendering it (debug builds skipped those draws: no shelter in
  the editor); shallow water's timestamps overflowing between frames; Update Continuously set in Editor Settings
  applying only after a restart (stock); a still capture hanging in a project with low processor mode.

## Milestone 51 — tools for AI agents (2026-11-05)

- `tools/fellow_agent/fellow_agent.py` (decision 0152): `capture` (a scene to PNGs from its camera or framed views,
  lit where it has no light, with image and rendering statistics and the share of the picture the scene covers),
  `describe` (the node tree as data), `check` (every scene and script loaded, instantiated and run), `run` (frame time
  percentiles), `compare` (two PNGs), `import`, `api` (the class reference), `init` (an `AGENTS.md` for a game
  project). JSON out, exit codes 0/1/2, errors with file, line and the script's backtrace (a `Logger`).
- `AGENTS.md` (and `CLAUDE.md`) at the root: where things are, how to build and check, the fork's rules and scope.
- A self-test with planted faults: 26 of 26 checks on the cloud's CPU, 22 of 22 on the RTX.
- Fixed: debug builds — the editor's viewport, debug exports — drew no omni lights, spot lights, decals or reflection
  probes since milestone 10 (the element-parallel cluster pass bound with the stock variant's uniform set; release
  builds unaffected).
- A Linux editor build in the cloud.

## Milestone 50 — tools (2026-11-05)

- An editor module (`fellow_editor`, editor builds only; decision 0151): gizmos for FellowEngine's nodes — a gas
  volume's box with resize handles, emitters' shapes, water extents, shallow-water domains, streaming cells' bounds,
  the wind's arrow, lightning's distance band, snow trackers — and their icons as billboards; `.bvh` motion capture
  imported as `Animation` resources (scale, the T-pose frame; a CMU preset); Project > Tools > **Split into Streaming
  Cells** for the selected `FellowWorldStreamer3D` (`split_into_cells()`: the level's nodes grouped on a grid, saved as
  cell scenes, replaced by `FellowStreamingCell3D`s).
- `FellowWorldStreamer3D.editor_preview` (on): the cells show their content in the editor, not saved with the level.
- Icons for the 17 FellowEngine classes; Performance monitors for texture streaming, world streaming and motion
  matching (the debugger's Monitors tab); the class reference complete (184 entries of the nodes were missing or
  empty; three classes outside the module's documented list).
- Checks: an editor check (`benchmarks/tools/editor_check/`: an editor plugin exercises all of it, then the split
  level is streamed back in a game; on the RTX with the editor's window and headless: every check passes), and the
  web template in headless Chromium (`benchmarks/tools/platforms/web/`).
- Android's (NDK r29, arm64) and the web's (Emscripten 4.0.10, threads) templates built; the web one runs with its
  cells streamed on worker threads and desktop OpenGL's image.
- Fixed: `FellowWeather3D.weather` missing in the editor (a two-argument setter, refused by debug builds); the pipeline
  cache's limit read before its setting existed (64 MiB, not 1024: caches over it discarded at start since milestone
  9); the water invisible with the Compatibility renderer (OpenGL's depth read as Vulkan's, an unbound wave array
  lifting it), and the shallow water's refraction there; a streaming cell's double free at exit; the web image's
  missing `libatomic`; the platform builds' copies of the host's generated sources.

## Milestone 49 — platforms without their hardware (2026-11-05)

- `FELLOW_VULKAN_PROFILE` (a diagnostic, decision 0150): the Vulkan driver presents its device as another — Android's
  2022 and 2025 baselines, Intel UHD 620 and Arc A770, GTX 1060, RTX 4070, RX 580, RX 7800 XT, Apple M1 through
  MoltenVK, or with ray tracing, half floats or subgroup operations removed — from the Khronos Vulkan-Profiles data;
  every shader checked against it and what the device could not run reported with the pipeline.
- Checks (`benchmarks/tools/platforms/`): every lab with Godot's three renderers (lavapipe, llvmpipe; `renderer_matrix`
  in bash and Python), eight labs on the nine devices, the Windows template built with MinGW and run under Wine, the
  Linux arm64 template cross-built and run under qemu, a clang build. No crash; Linux's images everywhere.
- Fixed: a crash on GPUs without buffer device addresses (index buffers' uninitialized creation flags); the Mobile
  renderer losing positional lights (Lights Lab 9.3 → 36.6, stock's); stock's DirectInput hook calling
  `VirtualProtect()` with a null pointer (a crash under Wine); fragment-to-light varyings counted as interpolants (stock)
  and the water's two in one; gas volumes dense where the sparse kernels' 11 storage images do not fit, the trace pass
  on textures and one sampler (27 samplers before); the glow's 16 × 16 tiles only on devices with 256 invocations a
  workgroup; MultiMesh culling's buffer leaked at exit; MinGW's and clang's build errors and warnings; warnings instead
  of errors from FellowEngine's nodes with the Compatibility renderer.
- On Android's 2025 baseline and every desktop profile FellowEngine needs nothing the device lacks; ray tracing falls
  back to the same image without it.
- On the RTX: the Mobile and Compatibility renderers give stock's images in every lab; milestones 45-49 cost nothing
  in frame time (the 9 standard scenes against milestone 44). Milestones 45-48 measured there: FSR 3.1 at FSR 2.2's
  cost and **on by default** (`upscaling/fsr3`) after a crash at its first frame was fixed (an ODR violation between the
  two FidelityFX SDKs' `FfxResourceDescription`); texture streaming's per-view memory as on lavapipe (video memory
  377-457 instead of 667 MiB, the first frame 838 → 103 ms); motion matching 0.12-0.89 ms a step for 16-256 characters
  on the laptop's CPU.

## Milestone 48 — motion matching (2026-11-04)

- `FellowMotionDatabase` and `FellowMotionMatcher3D` (new module `fellow_animation`, decision 0149): clips sampled into
  a database of poses, foot contacts and 27 features a frame (feet, hips, the trajectory 1 s ahead), searched every
  0.1 s with bounding boxes; inertialized transitions, the root's motion pulled towards a spring simulation, foot
  locking with two-bone IK, LOD by camera distance, `advance_group()` on the worker threads; BVH import; the database
  saved as a 16-bit resource.
- Matching Lab on 27 minutes of the CMU motion capture database: 16 characters follow their paths within 3.9 cm, feet
  sliding 0.11 m/s in contact (0.32 without locking and IK), popping's p99 36 mm (174 with hard cuts), 15 µs a
  character a frame; 256 characters 4.5 ms, 3.0 ms with LOD.
- FellowBench: Matching Lab (`--scene matching`), `bench/matching_eval.gd`, `scripts/get_cmu_locomotion.sh`.

## Milestone 47 — FSR 3.1 upscaler (2026-11-04)

- `rendering/fellow/upscaling/fsr3` (opt-in, decision 0148): a viewport in the FSR 2 scaling mode runs AMD's FSR 3.1
  upscaler instead of 2.2 — the FidelityFX SDK 1.1.4 (MIT) built from source, with a RenderingDevice backend
  (`effects/fsr3.{h,cpp}`) and the SDK's Vulkan GLSL through Godot's shader builder; Godot's FSR 2.2 options kept
  (motion vectors derived from the depth where there are none, the reactive mask clamp); half precision where the GPU
  has it; the SDK's tunables and debug view as settings.
- PBR Lab on lavapipe at 960 × 540 against an accumulated ground truth: at 2× upscaling less error in motion (−9 to
  −24 %) and flicker (−10 to −14 %) than FSR 2.2, +0.6 to +1.0 dB; in quality mode +0.7 to +0.9 dB, within 0.4-1.1 dB
  of native TAA; as native AA +1.3 dB over TAA in three of four sequences. Still views at 2× softer (−1.3 dB in two of
  three). 8.5 MiB less memory than FSR 2.2 at that size; no validation message of its own.
- FellowBench: `tools/lavapipe/upscaler_check.sh` (TAA, FSR 2.2 and 3.1 at three scales against a ground truth).

## Milestone 46 — world streaming (2026-11-04)

- `FellowWorldStreamer3D` and `FellowStreamingCell3D` (module `fellow_world`, decision 0147): a level in cells, each a
  PackedScene loaded on the ResourceLoader's threads when the camera or a target comes within its distance, its nodes
  built on a worker thread, a few cells a frame entering or leaving the tree (the nearest in first, the farthest out
  first), with hysteresis, `prefetch()`, `load_now()`, `is_settled()` and statistics; the cell's own children are its
  proxy while it is unloaded.
- Texture streaming: a texture not used yet keeps its small mips (a cell's, loaded before the cell enters the tree);
  2D, the decal and area light atlases and materials whose UVs it cannot estimate pin a texture at full resolution.
- A `.ctex` loads where a `Texture2D` is expected (stock: no loader).
- World Lab (lavapipe): at 1080p 289-612 MiB of video memory a view instead of 740 MiB with world and texture
  streaming, the start 4.09 → 2.55 s, the same images (one pixel by one level at 1080p); over a flythrough the peak
  708 → 321 MiB.
- FellowBench: World Lab (`--scene world`, `--world_streaming=1`, generated cells), the streamer's state in results.

## Milestone 45 — texture streaming (2026-11-04)

- `rendering/fellow/streaming/textures` (opt-in, decision 0146): VRAM-compressed `CompressedTexture2D` with a full mip
  chain load only their mips at or below 128 px; the RenderingServer estimates the finest level each texture can need
  (the cameras, the instances' boxes and scales, the surfaces' texel densities, BaseMaterial3D's `uv1_scale`, the
  samplers' bias), loads it on worker threads, swaps it in under the same RID and releases it when no longer needed,
  within `texture_budget_mb`. `RenderingServer.fellow_get_texture_streaming_info()` reports it.
- Streaming Lab at 1080p (lavapipe): 3-77 MiB of its 384 MiB of textures resident in a view (all texture memory 221-295
  MiB instead of 602), the textures loaded in 7-9 ms instead of 460 ms, the same images bit for bit once loaded.
- FellowBench: Streaming Lab (`--scene streaming`; textures from `benchmarks/scripts/make_streaming_textures.py`),
  waits for streaming before captures (`--streaming_fixed_wait`, `--streaming_prepass`), the streamer's state in every
  result; `tools/lavapipe/streaming_check.sh`.

## Milestone 44 — neural motion's CPU side and faster tensor-core products; readiness for AAA (2026-11-04)

- `fellow_motion` off the main thread (decision 0144): the service encodes the requests and decodes and
  endpoint-corrects the results on the WorkerThreadPool, one task per request; a character interpolates, crossfades
  and poses only the 26 joints the model drives, with a ring history and no per-frame allocation. The characters'
  main-thread work a step: 64 characters 0.888 → 0.212 ms on the laptop's CPU (16 on the cloud's: 0.611 → 0.204 ms);
  the same motion within 8.3e-6 m.
- The tensor-core products (decision 0145): the activations loaded four at a time (`vec4`), the epilogue's addresses
  and mask a row at a time — the same results bit for bit, 13-22 % faster from 2 requests on (RTX: 16 at once
  15.23 → 12.60 ms, 64 at once 51.55 → 40.95 ms; a request 4.03 ms). Six other designs measured and not kept.
  `FELLOW_MOTION_GEMM=scalar` (diagnostic) takes the scalar loads.
- Motion Lab on the RTX (milestone 43's build alternating with this one): 64 characters 8.52 → 7.33 ms a frame in
  flush mode, 4.30 → 3.56 ms asynchronous (the 99th percentile 11.6 → 6.1 ms); 16 characters 3.51 → 3.43 ms.
- `docs/fellow_aaa_readiness.md`: an assessment of FellowEngine's readiness for AAA production — a scorecard of 14
  areas, what an AAA project would hit first, what it is ready for now, a path in seven steps. Verdict: not AAA-ready as
  an engine (streaming, platforms and GPU breadth, upscaling, animation, tools); a renderer and simulation close to
  AAA-class on Windows/Vulkan/NVIDIA, ready for PC indie and AA games with a high-end look.

## Milestone 43 — neural motion: MotionBricks on the GPU (2026-11-03)

- New module `fellow_motion` (decision 0143): `FellowMotionModel` runs NVIDIA's MotionBricks SOMA model (a root and a
  pose transformer and a VQ-VAE decoder, 188 M weights) on a RenderingDevice of its own — a plan of 251 GPU ops that
  `tools/fmb_convert.py` writes from the released checkpoints into an `.fmb` file, checked against NVIDIA's PyTorch
  model (`tools/fmb_plan.py` executes the same plan in NumPy). The weights are NVIDIA's (NVIDIA Open Model Agreement)
  and are not in this repository.
- Tensor cores: `VK_KHR_cooperative_matrix` enabled in the Vulkan driver when the device has it
  (`RD::SUPPORTS_FELLOW_COOPERATIVE_MATRIX_F16`); the products run on it with fp16 weights (`cooperative_matrix`, on),
  fp32 elsewhere. RTX 5070 Ti Laptop (in use): a request 4.3 ms, 16 at once 18 ms, 64 at once 62 ms (the first
  version: 32, 68 and 171 ms).
- `FellowMotionCharacter3D`: a desired velocity in, a walking, jogging, running, turning, stopping and standing
  character out, with no animation clip (root-only targets from a relaxed stance; MotionBricks' runtime glue ported);
  one batched service per model (async, flush or immediate); a skinned SOMA mannequin (`tools/fmb_usd.py`). Falls
  (the training data has lying and crawling): the hips' target at a standing height, `get_up` (a standing target on
  the ground), `max_speed` 3.5 m/s — 32 characters for 40 s: 8 falls, all back up within 0.4 s.
- FellowBench: Motion Lab (`--scene=motion`, `--motion_model`, `--motion_body`, `--motion_crowd`, `--motion_mode`,
  `--motion_rewind`): 64 characters 9.4 ms a frame waiting for their requests, 5.0 ms with a worker thread running them.

## Milestone 42 — the weather's own rain in thin streaks (2026-11-03)

- A rain node a FellowWeather3D creates (`create_missing`) draws its drops as thin streaks (`streak_width` 0.004 m,
  decision 0142); a scene's own rain node is left as it is.
- The final check of milestones 36-42 (`m41_f317_final`, stock against fellow229 and fellow236, the nine scenes, 3 runs):
  fellow236 faster than stock in every scene (Stress Lab within its spread, its frame bound by the render thread), and
  against fellow229 each scene within its runs' spread. The README's table is from it.

## Milestone 41 — thin rain streaks (2026-11-03)

- `FellowRain3D.streak_width` (decision 0141, 0: off): each drop drawn at its own width or a pixel at its distance,
  whichever is wider, its opacity scaled by their ratio — near drops thin, far ones faint, instead of `drop_size`'s
  bars (heavy by day against the sun). FellowBench: `--weather_streaks`.
- Milestone 36's frozen puddles: white only at their edges (the rims had turned whole puddles white in Bistro).

## Milestone 40 — slush (2026-11-03)

- `FellowRain3D.slush` (decision 0140): melting snow soaks with its meltwater — greyer, glossier, without glints, its
  thin parts first. A FellowWeather3D raises it while snow lies above 0 °C (fully at 3 °C or in the rain) and lowers it
  below freezing and under fresh snow. `--weather_ground` takes a fifth value, the slush.

## Milestone 39 — wind-driven rain (2026-11-03)

- `FellowRain3D.wind_driven` (decision 0139, 1): in a wind the shelter follows the drops' slant — the heightfield
  looked up back along their drift as well — so walls facing the wind get wet under their eaves (and as wet as the
  ground), the rain reaches in under an awning on the windward side and the lee stays dry beyond it. Nothing changes
  below a drift of 0.2 m per metre of fall (a breeze of 1.8 m/s); not for snow. FellowBench: `--weather_wind_driven`.

## Milestone 38 — wetting and drying in patches (2026-11-03)

- `FellowRain3D.drying_patches` (decision 0138, 1): between dry and wet the surfaces are wet in patches — the puddles'
  noise taken as the ground's lows and highs, a broad and a fine noise with it: the highs dry first and wet last — and
  the puddles shrink to their deepest instead of fading. At a wetness of 0 or 1 the image is the same bit for bit.
  FellowBench: `--weather_patches`.

## Milestone 37 — showers (2026-11-03)

- `FellowRain3D.showers` (decision 0137, 0 by default): with the volumetric clouds on, it rains at the camera only as
  the clouds above let it — under their dense parts, as the rain shafts hang — so showers pass over with the clouds'
  wind: the drops, the ripples, the wetting and the settling snow follow; the shafts and the rainbow keep the rain set.
  A one-thread probe of the clouds' layer at the camera, read back asynchronously (0.010 ms on the RTX, `m37_f310`).
  FellowBench: `--weather_showers`. Showcase: a shower passing over Bistro.

## Milestone 36 — ice and hoarfrost (2026-11-03)

- `FellowRain3D.freeze` and `.frost` (decision 0136): the water on the ground frozen — no ripples, the puddles dark
  clear ice with white rims, the wet film a glaze — and hoarfrost on what faces the sky where the rain would reach, fine
  grey-white grains growing into a veil the surface shows through, glinting in the sun; under the snow cover.
- A FellowWeather3D's ground model below 0 °C freezes the water and, when nothing falls, grows the frost (slower under
  clouds), in the rain node's `freezing_time` (600 s) at −5 °C; above 0 °C both thaw. It creates its rain node in the
  cold; ready below 0 °C it settles the ground frozen and frosted under a clear sky.
- FellowBench: `--weather_temperature`, `--weather_ground=<wet,snow,freeze,frost>`, `--weather_drops` (0: none drawn).

## Milestone 35 — rain shafts (2026-11-03)

- `FellowRain3D.shafts` (decision 0135): while it rains under the volumetric clouds, grey curtains of rain hang below
  their dense parts (local coverage 0.7 to 0.95), streaked along the fall and drifting with the clouds, beyond 2 km of
  the camera and fading out from 25 to 40 km; snow's are brighter. Without rain the clouds' trace is the same bit for
  bit (lavapipe).
- Cost on the RTX (Water Lab in showers, `m35_f307`): the clouds' pass +0.05 ms; the frame within noise.

## Milestone 34 — rainbows (2026-11-03)

- `FellowRain3D.rainbow` (decision 0134): while it rains or lately rained (fading over 90 s once it stops, none once it
  snows) and the sun shines on the rain, the primary and secondary bows around the point opposite the sun — from each
  wavelength's refraction in water through the CIE matching functions, the sky brighter inside the primary and darker
  between the two — over the sky and over what stands behind the rain, dimmed by the clouds' shadow at the camera.
- Off is the previous image (lavapipe). Cost on the RTX (Bistro raining, `m34_f305`): the pass 0.045 ms, the frame
  +0.07 ms. FellowBench: `--weather_rainbow`. Showcase: a double rainbow over Bistro as the rain clears.

## Milestone 33 — the clouds' shadows and the sky's light under them (2026-11-03)

- `post/clouds/shadows` (decision 0133, on with the clouds): a map of the sun's transmittance through the cloud layer,
  512² over 16 km around the camera (the clouds' own density, eroded edges included), multiplied into the sun's shadow
  in the colour passes — moving shadows on a cloudy day, sunny gaps under an overcast; a FellowWeather3D's `sunlight`
  factor steps aside while they are on.
- `post/clouds/sky_light`: where the clouds cover the sky, its ambient light and reflections turn grey (no clear-sky
  blue on a wet street under rain clouds).
- Fixed before release: with ray-traced GI on, reading the map written earlier in the same frame lost the device on the
  RTX; the map is double-buffered (read a frame later).
- Off is the previous image (lavapipe: Rain Lab without drops, Water Lab, Rain Lab under clouds with both off, bit for
  bit). Cost on the RTX (Bistro under clouds, `m33_f305`): the map 0.12 ms, the frame +0.17 ms.
- `Light3D::get_light_rid()` serves the sun's identification; `light_storage.cpp` skips the weather's sun factor
  under the clouds' shadows.

## Milestone 32 — a weather controller (2026-11-03)

- Added `FellowWeather3D` (decision 0132): the weather from one node — presets (clear, cloudy, overcast, rain, storm,
  snow, blizzard) or a custom state of wind, rain or snow, lightning, cloud coverage and density, sunlight, sky light,
  fog and temperature — changed smoothly with `set_weather(weather, seconds)` (rain and snow hand over at the
  transition's midpoint). It drives the scene's `FellowWind3D`, `FellowRain3D` and `FellowLightning3D` (creating the
  missing ones), the volumetric clouds through a render-side override, the sun on the scene's surfaces only (the sky
  and the clouds keep the full sun), the environment's fog and background energy, and the ground: wet in the rain,
  drying after it, covered by falling snow below freezing, melting above it. Nothing in the editor.
- `Light3D::get_light_rid()` (C++): a light's RenderingServer RID (`VisualInstance3D::get_base()` is not set for lights).
- Off is the previous image (lavapipe: Rain Lab without drops, Water Lab bit for bit). Cost on the RTX (Bistro
  exterior, `m32_f297`): the controller not measurable, its weathers the cost of their parts (rain +0.5 ms).
- FellowBench: `--weather`, `--weather_cycle`, `--weather_times`. Showcase: Bistro under each weather, a time-lapse.
- The decision index lists records 0127-0132.

## Milestone 31 — lightning lights the clouds (2026-11-02)

- `FellowLightning3D.cloud_glow` (decision 0131): while a flash lasts, the volumetric clouds glow around it — by how
  near the view passes to the flash and how opaque they are — added in the clouds' composite at full resolution every
  frame (their traces, accumulated over frames, would smear a flash of a few frames). Without a flash the clouds are
  bit for bit milestone 30's (lavapipe). Showcase: the thunderstorm in Rain Lab under a cloud deck.
- Snow tracks: a new `track_window` starts the tracks afresh; a one-step parallax for them was tried and reverted.
- The defaults are not slower (`m30_f294_final`, stock / fellow212 / fellow220, 3 runs): fellow220 faster than stock in
  every scene (Sponza −54.2 %, Bistro exterior −33.4 %, City Lab −25.8 %, Crowd Lab −24.0 %, Bistro interior −23.6 %,
  Lights Lab −23.4 %, Foliage Lab −17.8 %, PBR Lab −16.4 %; Stress Lab −2.7 %, within its spread this time). Against
  milestone 26's build within the spread (`m30_f296_order`, the order reversed).

## Milestone 30 — snow tracks (2026-11-02)

- Added `FellowSnowTracker3D` and `FellowRain3D.stamp()` (decision 0130): feet, wheels and runners press tracks into
  the snow while they touch the ground (the shelter heightfield, eroded); the cover shows their walls, packed snow at
  their bottom, a rim beside them and the ground where they cut through; the falling snow fills them back. The
  tracks live in a world-anchored, wrapping R16F window around the camera (48 m, 2048², `tracks`, `track_window`,
  `track_resolution`, `snow_depth`, `track_refill_time`), updated only where something changed.
- Cost on the RTX (Rain Lab snowing, a cart's wheels pressing every frame, `m30_f292`): not measurable. Rain Lab
  without drops and Water Lab bit for bit on lavapipe.
- FellowBench: `--weather_snow_footprints`, `--weather_snow_ruts`, `--weather_snow_depth`; Rain Lab `--rain_tracks=1`.

## Milestone 29 — lightning (2026-11-02)

- Added `FellowLightning3D` (decision 0129): strikes at random around the camera (`strikes_per_minute`,
  `distance_range`) or on `strike()`; two to four return strokes, each a flash decaying over about 40 ms — a
  directional light from the bolt's direction with shadows (no sun disc, not taken for the sun by `FellowWater3D`), a
  branching bolt of a constant width on screen, a lit cloud. The same clock gives the same storm; `struck(position,
  distance)` for the thunder.
- Cost on the RTX (Rain Lab at dusk, `m29_f290`): +0.04 ms of GPU time on average with a strike every 2 s; nothing
  between strikes.
- FellowBench: `--weather_lightning`, `--weather_lightning_flash` (captures), `--weather_lightning_frames`
  (sequences). Showcase: a thunderstorm in Rain Lab.

## Milestone 28 — snow (2026-11-02)

- `FellowRain3D.precipitation` = Snow (decision 0128): flakes drift down in a box around the camera, take the
  scene's wind where they are (gusts included, the particles' Wind group), swing about their paths, stop on the
  shelter heightfield and receive shadows; drawn in the opaque pass with hashed alpha so that TAA reprojects them
  (blended, TAA wiped them out).
- `FellowRain3D.snow`: a snow cover on what faces up where the snow falls — patchy with ragged edges while thin,
  everything facing up once deep, nothing on slopes over 60°, bare under roofs and canopies (the shelter eroded by
  35 cm, so wires and lamps overhead leave no bare spots); smooth with gentle undulations, glinting
  (`snow_sparkle`); `snow_follows_precipitation` grows it in `snowing_time` and melts it in `melting_time`.
- The rain unchanged: Rain Lab without drops and Water Lab bit for bit on lavapipe. Cost on the RTX (Rain Lab,
  `m28_f289`): snowing on a covered street +0.37 ms of GPU time (raining +0.34).
- FellowBench: `--weather_snow` in any scene, Rain Lab `--rain=snow`. Fire Lab's embers are round.
- Class reference for the new members and for milestone 27's water and particle wind members.

## Milestone 27 — the scene's wind for water, gas and particles (2026-11-01/02)

- `FellowWater3D.wind_from_scene` (decision 0127): the spectrum follows the active `FellowWind3D`'s speed and
  direction, and its gusts roughen the shortest waves — darker, rougher water in a gust, a mirror in a lull, the
  "cat's paws" running over a lake (`gust_ripples` scales them).
- `FellowGasVolume3D.wind_from_scene`: the gas's wind each step is the scene's wind at the box's centre, gusts
  included; flames and smoke lean and waver with the gusts.
- `ParticleProcessMaterial` gets a Wind group (`wind_enabled`, `wind_influence`): the particles' horizontal velocity
  relaxes towards the wind at their position, gusts included. Particle shaders read it as `WIND_VELOCITY` (zero
  without a wind and on the Compatibility renderer).
- Off is the previous image: Water Lab and Fire Lab without a wind bit for bit on lavapipe. Cost on the RTX
  (`m27_f287`, `m28_f288`): the water's gust ripples and the gas's wind not measurable, Particles Lab's update +0.04
  to +0.15 ms, its frame +0.09 ms.
- Fixed: Water Lab with contact shadows crashed — the depth pre-pass bound per-render masks that did not exist yet
  (`84f3a8e`).
- FellowBench: `--weather_wind=<m/s>` in any scene (Water, Fire and Particles Lab hand it on), `--wind_leaves=<n>`
  falling leaves in Wind Lab.

## Milestone 26 — wind in the forest (2026-10-29/30)

- Added `FellowWind3D` (module `fellow_fluid`, decision 0125): the scene's wind with gusts, a noise field travelling
  with it that rolls over a meadow in waves. Each plant bends with the wind's force towards its top, sways at a
  frequency of its size, its branches at their own phases, its leaves flutter; normals turn with the bend and bent
  grass brightens (a sheen). `BaseMaterial3D` gets a Wind group (`wind_height`, `wind_bend`, `wind_flutter`,
  `wind_sheen`); a `ShaderMaterial` uses the render mode `fellow_wind` and the vertex built-ins `WIND_HEIGHT`,
  `WIND_BEND`, `WIND_FLUTTER`, `WIND_SHEEN`. Shadows and motion vectors follow (TAA and motion blur see it);
  `FellowRain3D`'s drops follow the same wind.
- New bench scene Wind Lab (`--scene=wind`: about 40 k grass clumps, 700 broadleaf trees, 500 bushes). Without a
  `FellowWind3D` and without wind materials, City Lab, Water Lab, Rain Lab `--rain=off` and Foliage Lab with its
  TIME-based sway render bit for bit the images of `d8f2909`.
- The wind's cost is in the shadows. MultiMesh culling now uses the wind's bound on the displacement, and the far
  cascades (from the third, `shadows/wind_cached_from_cascade` = 2) are cached with the plants' pose of their last
  build: lavapipe Wind Lab −41 to −63 % GPU time against the first build, 32-58 % less than the TIME-based sway
  projects write. RTX (`m26_f275_wind`): still 3.566 ms of GPU time, `FellowWind3D` 4.122 ms (+0.56: the near shadow
  cascades +0.33, the opaque pass +0.23, the pre-pass +0.06), the TIME-based sway 5.372 ms (+1.81); the wind with
  gusts is 1.25 ms (23 %) cheaper than what projects write.
- Optimizations of the features of milestones 24-26 (2026-10-29, lavapipe):
  - rain: the ripples' compute works out the 75 cells' drops once per group in shared memory instead of 27 times per
    texel (exact): 14.1 → 9.4 ms on lavapipe (−33 %; on the RTX it is 0.04 ms) (0124);
  - water: reflections traced over a Hi-Z pyramid of the depth texture (`postfx/depth_copy_nearest_mipmaps`, on,
    0126): on the RTX the reflections 0.99 → 0.61 ms (−38 %), Water Lab's GPU time 3.128 → 2.762 ms (−12 %; shore
    −29 %, pier −24 %, glint −36 % of the water's pass, aerial +26 %); the far shore reflected at its true parallax,
    without jitter;
  - tried and not kept: a Fresnel-scaled step count for the reflection march (+2-9 %, the spread; reverted).
- On the RTX, the night of 2026-10-29/30 (rows 5-14 of the report's table):
  - row 5, rain, the ripples' cells shared: the ripples' compute −0.025 ms; Rain Lab's frame unchanged (the basin's
    depth pyramid +0.047 ms); kept;
  - row 6, water, the rays towards the camera bounded by `reflection_distance`: aerial −0.01 ms, the rest within the
    spread (`m26_f276`); not kept;
  - row 7, water, the depth pyramid asked for only while reflections are traced (`560d36c`): without reflections the
    depth copy 0.079 → 0.026 ms (`m26_f278`); exact; kept;
  - row 8, wind, the shadows cached from the second cascade: −0.11 ms more than from the third, but only the nearest
    cascade's shadows sway; the third stays;
  - row 9, depth texture, the single-level copy skipped where the pyramid replaces it (`e99e5db`): the depth copy
    −0.023 ms in Water Lab and Rain Lab (`m26_f280`); exact (lavapipe: Water Lab bit for bit); kept;
  - row 10, water, where the shading without reflections goes (`m26_f281`): of the 0.46 ms, the cascades' lookups
    0.21 ms (their anisotropic filtering 0.10), the foam's noise 0.013, the caustics 0.02, the refraction 0.01; the
    rest the lighting;
  - row 11, the labs the final run does not cover, before / after milestone 26 (`m26_f283`, fellow207 / fellow211):
    frame time Fire Lab 2.914 / 2.950 ms, Burn Lab 3.669 / 3.628, Fluid Lab 2.315 / 2.311, Particles Lab
    2.946 / 2.921, Blend Lab 2.008 / 2.033, within the runs' spread;
  - row 12, burning (milestone 22), where its drawing goes (`m26_f284`): +0.84 ms of GPU time against the original
    meshes (the opaque pass +0.43, the depth pre-pass +0.28, the shadows +0.16); the crack cells 0.19 ms of it, the
    vertices' march into the voxels 0.37 ms (it ran in every pass drawing the mesh);
  - row 13, burning, the crack cells skip the neighbours that cannot be nearer (exact): no gain (the pre-pass
    0.358 ms with and without, the opaque pass +0.02 ms, `m26_f285`); reverted;
  - row 14, burning, the vertices march into the voxels once per frame with steps, in a compute pass, the materials
    reading them by `VERTEX_ID` (`fluid/burn_vertices_cached`, on, 0120): Burn Lab's GPU time 2.634 → 2.139 ms
    (−19 %; the frame −13 %): the shadows −0.14 ms (−65 %), the opaque pass −0.27, the pre-pass −0.13, the compute
    pass +0.03; at 48 voxels per object −24 % (`m26_f285`); as shipped (fellow213) 2.638 → 2.134 ms, Fire Lab
    unchanged (`m26_f286`); the images alike (at most 151 pixels per 1080p view differ, PSNR ≥ 81 dB).
- Final check against stock (nine scenes, three runs each after a pass filling the shader caches; frame time):
  `m26_f279_final` (fellow210) faster in every scene and every pair of runs — Sponza −52.8 %, Bistro exterior
  −29.6 %, City Lab −27.4 %, Bistro interior −24.1 %, Crowd Lab −23.0 %, Lights Lab −22.9 %, Foliage Lab −18.0 %,
  PBR Lab −14.7 %, Stress Lab −10.3 %. Again at the end of the night with fellow212 (`m26_f285_final`: the copy
  skipped, the burning's vertices marched once per frame), faster in every scene and every pair of runs — Sponza
  −54.6 %, Bistro exterior −30.8 %, City Lab −25.1 %, Bistro interior −23.5 %, Lights Lab −22.2 %, Crowd Lab
  −21.8 %, Foliage Lab −16.7 %, PBR Lab −16.0 %, Stress Lab −11.5 %.

Details: [report](docs/fellow_milestone26_report.md)

## Milestone 25 — rain and wet surfaces (2026-10-29)

- Added `FellowRain3D` (module `fellow_fluid`, decision 0124). Drops around the camera: stock GPU particles drawn by a
  shader of their own as lit streaks along their velocity (leaning with the wind), colliding with the scene seen from
  above and splashing where they land. Wet materials: every opaque Forward+ material wetted after its own code where
  the rain reaches it — porous surfaces darker, all with a glossy film, puddles on flat ground with the rain's ripples
  (a tiling texture computed on the GPU every frame) — in the depth pre-pass too, so screen-space reflections show the
  street in them. Shelter: a heightfield of the scene from above, following the camera, keeps the ground under roofs
  and trees dry and the drops off it. A `FellowWater3D` shows the ripples too.
- The renderer keeps a weather state on the render thread (`RendererRD::FellowWeather`) and a specialization bit
  (`fellow_wetness`), so nothing changes without rain: City Lab and Rain Lab `--rain=off` bit for bit the images of
  `5e26af0` (with the final build Rain Lab `--rain=off` has 10 pixels one level off in the basin: float rounding in
  the water's march).
- New bench scene Rain Lab (`--scene=rain`, `--rain=on|dry|off`, `--rain_light=day|dusk`): a town street, a cobbled
  square with a fountain basin, an arcade whose floor must stay dry, a bus shelter, a parked car, street lamps.
- Cost on the RTX (Rain Lab flythrough, 3 runs each): 0.36 ms of GPU time, after three rounds of optimization from
  0.49 ms (frame: no rain 1.889 ms, the first version 2.398 ms, the last 2.243 ms); a `FellowRain3D` without rain
  0.04 ms (the spread). The wetting's five `textureGrad` lookups became lookups at explicit mip levels (the wetting
  0.175 → 0.116 ms). The rest of the 0.36 ms: SSR on the wet surfaces 0.13 ms, the particles 0.04 ms, the ripples'
  compute 0.04 ms, the drops' drawing 0.03 ms.
- Water Lab: a reflection march with the clip position linear along the ray made no measurable difference; 16
  reflection steps instead of 32 save 0.36 ms (frame 2.974 ms against 3.310).

Details: [report](docs/fellow_milestone25_report.md)

## Milestone 24 — water: lakes and rivers (2026-10-28)

- Added `FellowWater3D` (module `fellow_fluid`, decision 0123), a body of water to the horizon: wind waves simulated
  on the GPU after Tessendorf (JONSWAP for the wind and fetch, FFT in three cascades of 160 / 27 / 5 m), matching
  NumPy to half-float precision (worst relative error 9 x 10⁻⁴); the river's simulation (`FellowShallowWater3D`) drawn
  as part of the lake, its waves carried by the flow; reflections traced in screen space with the sky where nothing is
  hit; refraction, absorption and in-scattering, foam at shores and objects, caustics from the waves' own slopes, the
  sun's glitter path.
- Exact: the screen texture's copy builds its mip chain only when a material drawn this frame samples its mips
  (`postfx/screen_copy_mipmaps_on_demand`): the copy 0.031 ms (0.064 with its mips always); Water Lab and Fluid Lab
  bit for bit with the option on and off.
- New bench scene Water Lab (`--scene=water`; `--water=basic` is what a Godot project would use). Off, Fluid Lab
  renders bit for bit the images of `6f091cd`.
- Cost on the RTX (Water Lab flythrough, frame / the water's pass): no water 1.615 ms; Godot's water
  1.999 / 0.166 ms; `FellowWater3D` 3.482 / 1.591 ms (`m24_f269`); with the cheaper reflection march
  3.264 / 1.389 ms; without reflections 2.372 / 0.463 ms (`m24_f270`). The waves' compute 0.10 ms, the simulation
  0.14 ms, the caustics 0.03 ms. The reflections are the cost: 1.13 ms, and 0.93 ms after the Fresnel cutoff, steps by
  screen length and jitter (−18 %, 45-57 dB against the full march). In all, 1.6 ms of the frame (Godot's water
  0.38 ms), more than half of it the reflections (milestone 26 made them cheaper).

Details: [report](docs/fellow_milestone24_report.md)

## Milestone 23 — far objects: contribution culling, automatic HLOD (2026-10-28)

- Contribution culling (decision 0121, off by default): the camera skips a mesh or MultiMesh node — and, with
  MultiMesh culling, each MultiMesh instance on the GPU — whose bounding sphere would cover fewer than N pixels of the
  viewport's width (`geometry/contribution_cull_pixels`); a directional cascade skips casters spanning fewer than N of
  its texels (`shadows/contribution_cull_texels`). At about 4 px of 1080p: 300-1 400 nodes per view not drawn, 7-13 %
  fewer primitives, the render thread 1-7 % shorter; 62.9-78.4 dB against the full image. Small MultiMesh instances
  are thinned, not culled (`geometry/contribution_cull_instances`): culled, Foliage Lab's meadow turned 3.7 levels
  lighter; thinned, its mean colour stays within 0.05 levels.
- Added `FellowHLOD3D` (module `fellow_world`, decision 0122): the static meshes below it are replaced beyond a
  distance by proxies. Repeated meshes become instanced proxies (one MultiMesh per kind, culled instance by instance
  on the GPU, drawn from the distance on): the same image bit for bit on lavapipe, the render thread 23-42 % shorter,
  the GPU unchanged; City Lab's 18 800 nodes in 14 proxies, built in 44 ms. Unique meshes are merged per cell and
  material into meshes with generated LODs. Shadows are decided per cascade, camera-independent, so the directional
  shadow cache keeps them.
- City Lab flythrough on the RTX (`m23_f267`): stock 2.705 ms frame (render thread 2.066 ms, GPU 1.789 ms);
  fellow200 2.053 ms; with contribution culling at 4 px 2.020 ms (−1.6 %); with `FellowHLOD3D` at 100 m 1.813 ms
  (−11.7 %), render thread 1.172 ms (−26.4 %), GPU 1.746 ms. Foliage Lab with contribution culling, after the
  compaction grows only the instances the cull kept (`m23_f268`): aerial −10.5 %, forest −1.2 %, meadow −2.4 %.
- Impostors (`FellowHLOD3D.impostor_distance`, an option): far instances of heavy meshes drawn as octahedral
  impostors baked at run time (8 x 8 views of 128 px). They do not pay for Foliage Lab's 420-triangle trees
  (`m23_f268_foliage`): flythrough 4.805 → 4.671 ms (−2.8 %), but aerial +5.1 % and forest +6.2 %; images 27.4 / 49.0
  / 32.0 dB. `impostor_min_triangles` defaults to 2 000, so light meshes stay meshes.
- Tried and not kept: HLOD merging every mesh per cell (18 814 meshes into 196 proxies: a third of the triangles, but
  1 500 draws instead of 200 and the render thread no faster — merging repeated meshes undoes Godot's instancing).

Details: [report](docs/fellow_milestone23_report.md)

## Milestone 22 — burning objects: char, embers, ash (2026-10-26/28)

- Added `FellowBurnable3D` (module `fellow_fluid`, decision 0120): a `MeshInstance3D` burns like wood in a gas
  volume's fire. Per object a GPU-simulated box of voxels (temperature, wood, char, ash): conduction, the heat of the
  gas's flame or the surface's own flame, pyrolysis (wood → char; the volatiles are the gas's fuel, so the flames come
  from the object), char oxidation (embers → ash), the remains falling to the ground and sliding into heaps. Coupled
  both ways with `FellowGasVolume3D`.
- Drawn on the object's own material (BaseMaterial3D or ShaderMaterial, extended): the mesh subdivided, its vertices
  receding with the wood and char, char and ash colours, embers glowing in cracks; burned-through parts not drawn; the
  ash and crumbled charcoal drawn as a heap of its own (`FellowAshHeap`). Below `collapse_fraction` of wood and char
  the physics body stops colliding (signal `collapsed`).
- Verification: the GPU matches a NumPy reference (`burn_reference.py`): `--case=solo` 1.1 × 10⁻⁵, `--case=ash`
  2.2 × 10⁻⁵, `--case=gas` 9.1 × 10⁻⁷ (relative L2); Fire Lab and the gas checks unchanged bit for bit.
- New bench scene Burn Lab (`--scene=burn`): two logs and a branch over a kindling fire, a crate lit at a corner. The
  branch burns away by 15 s, the logs by 35-40 s, the crate's planks by about a minute; the logs end as 0 % wood,
  13-25 % char, 9-11 % ash of their volume at 70 s. The look was reworked on 2026-10-27 (standing, licking flames;
  alligator char; beds of grey ash with pieces of charcoal).
- Cost on the RTX (`m22_f267_burn_cost`, fellow200): frame 3.417 ms burning drawn and simulated, 2.594 ms simulated
  with the original meshes drawn, 2.387 ms with neither. The simulation of four objects costs about 0.2 ms of the frame
  (0.03 ms of GPU time); drawing them burning 0.8 ms (milestone 26 cut that with `fluid/burn_vertices_cached`).

Details: [report](docs/fellow_milestone22_report.md)

## Milestone 21 — gas: where the time goes, sparse bricks (2026-10-26)

- Sparse bricks (decision 0119, on by default: `FellowGasVolume3D.sparse`, `sparse_margin`; project setting
  `fluid/gas_sparse`): only the 4³ bricks within two bricks of smoke, heat, fuel or an emitter are simulated. Fire
  Lab's simulation per step on the RTX: `--gas_res=64` 1.77 → 1.58 ms (−11 %), frame 3.38 → 3.10 ms (−8.2 %);
  `--gas_res=128` 6.43 → 3.16 ms (−51 %), frame 8.52 → 5.04 ms (−41 %).
- Verification: the GPU matches the NumPy reference, which models the bricks, to ≤ 3.1 × 10⁻⁶ after 30-60 steps; two
  runs bit for bit; dense volumes are milestone 20's bit for bit; City Lab 0 differing pixels.
- Profile (`fluid/gas_profile_passes`): the pressure solve is 65-77 % of a step. At 128 cells, dense → sparse:
  emission group 0.16 → 0.34 ms, advection 0.84 → 0.25, forces 0.54 → 0.17, pressure 4.49 → 2.62, projection and
  bricks 0.72 → 0.29.
- From the RTX profiles: after a world-space box's shift the bricks outside the simulated ones are cleared one
  invocation per texel, not per brick (the emission group 0.58 → 0.34 ms at 128 cells).
- Tried and not kept: the multigrid's small levels in one workgroup (`fluid/gas_mg_tail_cells`): the same state bit
  for bit, slower at every threshold but 2048 cells at 64 (−2.0 %).

Details: [report](docs/fellow_milestone21_report.md)

## Milestone 20 — smoke and fire (2026-10-26)

- Added `FellowGasVolume3D` (module `fellow_fluid`, decision 0117): a box of air simulated on the GPU, one step per
  physics tick — a staggered grid, MacCormack advection, combustion, buoyancy, vorticity confinement, turbulence, wind,
  a multigrid pressure projection; the physics bodies inside it are moving obstacles. Its `FellowGasEmitter3D`
  children emit smoke, fuel and heat.
- Drawn by Forward+ (decision 0118): a half-resolution ray march with the smoke's self-shadow, the sky's light, the
  fire's blackbody emission (its colour from the temperature × 1.4, `fire_temperature_scale`), the smoke's shadow on
  the scene; an omni light at the fire placed and coloured from the simulated emission; the velocity as a texture for
  GPU particles (embers).
- Verification: the GPU's state equals a NumPy port of the solver to float rounding (≤ 2.5 × 10⁻⁶ after 30-60 steps,
  except the campfire's fuel after 60 steps, 9.6 × 10⁻⁵); two runs of Fire Lab render the same frames bit for bit;
  without a gas volume every frame is milestone 19's bit for bit.
- New bench scene Fire Lab (`--scene=fire`: a campfire, a smoking chimney, a sphere through the smoke, a carried
  torch). Cost on the RTX (`m20_f261`): at `--gas_res=64` (0.47 M cells) the simulation 1.69 ms of GPU per step, the
  drawing 0.31 ms, the frame 3.25 ms (1.76 without the simulation); at 128 (3.24 M cells) 5.62 and 0.45 ms, the frame
  7.52 ms.
- Final measurement, stock against fellow193 (`m20_f260_final`, nine scenes, three runs each): faster in every scene
  and every pair of runs — Sponza 13.82 → 6.40 ms (−53.7 %), Bistro exterior 6.33 → 4.44 (−29.8 %), City Lab
  2.88 → 2.10 (−27.0 %), Bistro interior 4.31 → 3.33 (−22.7 %), Lights Lab 3.05 → 2.36 (−22.7 %), Crowd Lab
  5.63 → 4.49 (−20.2 %), Foliage Lab 3.41 → 2.86 (−16.2 %), PBR Lab 2.47 → 2.09 (−15.5 %), Stress Lab 4.82 → 4.43
  (−8.2 %).

Details: [report](docs/fellow_milestone20_report.md)

## Milestone 19 — more of the look, and its cost (2026-10-25)

- Four visual features after Unreal's, off by default (off, the image is the previous build's bit for bit):
  `post/bloom/enabled`, an energy-conserving bloom of the whole image (0112); `post/bloom/lens_flare`, ghosts and a
  halo of the bright sources (0113); `post/white_balance/temperature` and `tint`, a camera's white balance (Bradford
  adaptation to D65) (0114); `post/light_shafts/intensity`, rays from the sun broken by roofs and clouds (0116).
- Three optimizations of milestone 18's features (`m19_f258`): clouds traced a quarter of the pixels per frame
  (`post/clouds/interleaved`, 0115), City Lab's clouds 0.505 → 0.325 ms; the atmosphere's sky no longer redraws
  Godot's radiance every frame, "Setup Sky" 0.25 → 0.002 ms in every scene (0108); contact shadows in 8 steps, City
  Lab 0.159 → 0.104 ms, Sponza 0.282 → 0.179 ms (0107). With every feature on, about 0.5 ms saved per frame in City
  Lab and 0.37 ms in Sponza.
- GPU cost per feature (`m19_f258`, City Lab with an 8° sun / Sponza; none: 2.03 / 6.44 ms): bloom +0.14 (City Lab);
  bloom and lens flare +0.15 / +0.13; light shafts +0.06 (City Lab, where the sun is on the screen); white balance
  −0.002 (n.s.); interleaved clouds +0.39 / +0.15; contact shadows in 8 steps +0.12 / +0.17.

Details: [report](docs/fellow_milestone19_report.md)

## Milestone 18 — the look (2026-10-25)

- Seven visual features after Unreal Engine 5's, each under `rendering/fellow/post/*`, off by default, and off the
  image is the previous build's bit for bit: `post/exposure/histogram` (histogram eye adaptation, 0105),
  `post/local_exposure/enabled` (0106), `post/contact_shadows/enabled` (0107), `post/atmosphere/enabled` (Hillaire's
  sky atmosphere and aerial perspective, 0108), `post/clouds/enabled` (volumetric cumulus, 0109),
  `post/sharpen/strength` (RCAS after TAA, 0110), `post/bloom/lens_dirt` (0111).
- GPU cost, each against none (`m18_f255`/`m18_f256`, City Lab / Sponza / Bistro exterior; none 2.18 / 6.40 /
  4.32 ms): exposure (histogram and local) +0.01 (n.s.) / +0.03 (n.s.) / +0.03; contact shadows +0.22 / +0.34 /
  +0.29; atmosphere +0.11 / +0.10 / +0.07; clouds +0.63 / +0.62 / +0.52; all seven +0.88 (+40 %) / +1.01 (+16 %) /
  +0.86 (+20 %). The histogram's exposure pass is cheaper than stock's reduction (0.054 against 0.062 ms in City Lab).
- Second round (`m18_f257`): clouds traced only where the sky is seen (Sponza +0.19 ms, the pass 0.107 ms, was 0.609;
  Bistro exterior +0.22, the pass 0.151 ms, was 0.496); contact shadows marched in clip space (0-10 % cheaper); the
  sky's Rayleigh coefficients integrated over the sRGB channels (`srgb_rayleigh`, the default), matching a spectral
  sky within 1-2 % where the three-wavelength coefficients leave the red 15-20 % low.

Details: [report](docs/fellow_milestone18_report.md)

## Milestone 17 — physics and collisions (2026-10-24/25)

- Both of Godot's 3D physics engines are faster, and the exact fork's simulation is stock's tick by tick: in every
  case of the new Physics Lab, with both engines, the physics state hashed every 30 ticks equals stock's (also with 1
  to 24 workers). New tools: Physics Lab (five cases), the bench's `--mode=physics`, `benchmarks/physics_bench.py`.
- GodotPhysics3D, exact, on: motion queries skip the trimesh faces a separating-axis test proves out of reach
  (`physics/distance_face_cull`, 0085; with the shape itself and the faces' edge axes, 0096; culled once per query,
  0097; not tested again within a sweep, 0100); a convex polygon's vertices transformed once per SAT test (0086); the
  broadphase's culls on worker threads (0087); a large island solved on every thread in stock's per-body order (0089:
  `pile`'s solve 9.9 → 3.9 ms); islands from contiguous constraint lists (0091); a repeated motion test answered from
  the first within one `move_and_slide()` (0093); rays skip tree nodes beyond the closest hit (0098); vectorised and
  inline segment-box tests (0094, 0099); `Vector3::is_finite()` inline (0103). `physics/verify` checked 50-66 million
  (0085) and 36 million (0096, 0100) skipped faces against GJK: none touching.
- Jolt, exact, on: Area3D contacts recorded per thread instead of under one mutex (0088); the jobs per phase by the
  work, one per 128 active bodies and at least 4 (`physics/jolt_max_jobs`, 0090).
- Laptop, headless, 600 ticks, ms per tick, stock → exact fork (`m17_f254`): GodotPhysics3D `pile` 28.11 → 12.36
  (× 2.27), `stack` 1.37 → 1.13 (−17 %), `chars` 27.39 → 19.95 (−27 %), `rays` 74.08 → 8.65 (× 8.6), `areas`
  4.00 → 2.61 (−35 %); Jolt `pile` 3.52 → 3.50 (−1 %), `stack` 0.77 → 0.65 (−16 %), `chars` 5.77 → 5.78 (=), `rays`
  10.52 → 10.12 (−4 %), `areas` 3.78 → 2.20 (−42 %).
- Fast mode (off by default, `physics/fast/*`; deterministic, not stock's results bit for bit):
  `physics/fast/jolt_shape_cast` (0095) and `physics/fast/godot_toi` (0102). GodotPhysics3D `chars` 12.69 ms (× 2.2),
  `rays` 8.02 ms (× 9.2); Jolt `chars` 3.87 ms (−33 %), `rays` 2.38 ms (× 4.4); Jolt's shape casts within 2.5 mm on
  25 m of stock's.
- Correction kept visible: the first results (`m17_f252`) for `chars` and `rays` were measured on a Physics Lab
  terrain wound facing down (fixed in `2e1cd05`); the numbers above replace them.
- Tried and not kept: Jolt's jobs through a lock-free queue and pump tasks (0092, no consistent gain); a capsule's SAT
  without the faces proven out of reach (0101, exact, no gain); `test_body_motion()`'s broadphase culls from one
  collected set (0104, exact, −1.5 %).

Details: [report](docs/fellow_milestone17_report.md)

## Milestone 16 — a better image: post-processing and ray tracing (2026-10-24)

- Everything new is off by default (with the defaults the image is the previous milestone's bit for bit). Motion blur
  after TAA, a reconstruction filter over the velocity buffer (`post/motion_blur/enabled`, 0078): City
  Lab GPU +0.35 ms, Sponza +0.39 ms. Lens effects folded into the tone mapping pass (`post/lens/chromatic_aberration`,
  `post/lens/vignette`, `post/lens/film_grain`, 0079): within noise.
- Ray tracing (`raytracing/enabled`, 0080): ray queries enabled in the Vulkan driver (stock never turned them on), a
  bottom-level structure per mesh surface, the frame's top-level structure from the mesh and MultiMesh instances
  within 200 m of the camera. Building the top-level structure for City Lab's 7 600 instances took 5.7 ms of the
  render thread with stock's driver code; built in local memory and copied whole, 0.6 ms.
- Four effects on it: ray-traced ambient occlusion in place of SSAO (`raytracing/ambient_occlusion`, 0081), ray-traced
  sun shadows (`raytracing/sun_shadows`, 0082), DDGI from two cascades of ray-traced probes (`raytracing/gi`, 0083;
  probe relocation, `gi_sky_leak` 0.1), ray-traced reflections in place of SSR (`raytracing/reflections`, 0084); a
  path-traced reference (`debug/raytracing_gi_reference`).
- GPU cost against ray tracing on with no effect (`m16_f246_rt_ab`, City Lab / Sponza / Bistro exterior): RTAO
  −4 % / +5 % / +17 %; sun shadows +4 % / +7 % / +14 %; DDGI +57 % / +22 % / +44 %; reflections +8 % / +13 % / +5 %;
  all four +66 % (2.15 → 3.55 ms) / +53 % (6.33 → 9.72 ms) / +59 % (4.07 → 6.45 ms). The render thread pays about
  1 ms for the scene in City Lab and Bistro exterior.
- Tried and not kept: reflections traced by a thread per 2 × 2 block (Sponza 2.04 ms against 1.57 ms).

Details: [report](docs/fellow_milestone16_report.md)

## Milestone 15 — frame pacing, skeletal animation (2026-10-22)

- Closed without its final measurement: the final run (`m15_f229`) was stopped after its first scenes when the job
  queue was cleared at the user's request on 2026-10-22; the numbers below are the milestone's own measurements.
- Six worker tasks instead of ten (`cpu/worker_tasks`, automatic: half the logical processors less two, 1-6; decision
  0073): City Lab's presentation stalls gone, 1 % low 117-281 → 246-289 fps; no scene's mean frame moves.
- New bench scene Crowd Lab (256 skinned characters with 52 bones, an AnimationPlayer and a Skeleton3D each): its
  frame is the main thread animating them, in stock's code.
- Skeletal animation without work whose result is known (0076; `animation/elide_modifier_backup`,
  `animation/full_weight_slerp`, `animation/track_shortcuts`, `animation/key_hints`): the same bone poses bit for bit;
  Crowd Lab between processes stock 5.25 → 4.60 ms (−12.3 %), in one process −19.6 %, and the key hints −4.9 % more.
- A skin's bone transforms handed to the RenderingServer in one call (`animation/batched_skin_transforms`, 0077): with
  the separate render thread Crowd Lab −25.2 % (4.63 → 3.47 ms), now faster than the default thread model (4.34 ms).
- Rejected, exact but without gain: SSR's resolve with its samples shared (`postfx/ssr_resolve_shared`, 0074); TAA's
  tile loaded by every invocation (`postfx/taa_strided_load`, 0075).

Details: [report](docs/fellow_milestone15_report.md)

## Milestone 14 — the same frames on every run (2026-10-22)

- With the defaults and autotune off, two runs at a fixed frame rate render the same frames bit for bit in every
  scene the run-to-run floors are measured in (Bistro interior, PBR Lab, Sponza, Lights Lab), where stock's two runs
  differ at 69-98 dB; also with the separate render thread.
- Textures a pass can write, created without data, cleared before their first use (`frame/clear_new_textures`,
  0070): no measurable cost. The positional shadow atlas's reallocation tolerance counted in simulated time at a fixed
  frame rate (`shadows/atlas_simulated_time`, 0071). Movie Maker mode: a 3D draw waits for its specialized pipeline
  (`frame/movie_wait_pipelines`, 0072): 0 draws on the ubershader (34 without).
- Final (`m14_f213_final`, stock → fellow162): Sponza 13.97 → 6.66 ms (−52.3 %), Bistro exterior 7.06 → 4.52
  (−36.0 %), City Lab 3.35 → 2.24 (−33.3 %), Lights Lab 3.28 → 2.48 (−24.3 %), Bistro interior 4.46 → 3.49
  (−21.8 %), PBR Lab 2.80 → 2.29 (−18.0 %), Foliage Lab 3.90 → 3.20 (−17.9 %), Stress Lab 5.29 → 5.00 (−5.4 %);
  Foliage Lab with one MultiMesh per type 11.31 → 5.09 ms (−55.1 %). Load to the first frame −21 % (Bistro exterior)
  to −60 % (Lights Lab).
- Godot's separate render thread measured with FellowEngine (`m14_f204`): Stress Lab −6.7 %, City Lab −4.5 % (n.s.),
  Bistro exterior −1.6 %; recorded in the options doc as a recommendation for projects whose main thread works.

Details: [report](docs/fellow_milestone14_report.md)

## Milestone 13 — frame pacing, LOD of large surfaces, run-to-run floors (2026-10-20)

- Hot workers linger 30 µs instead of 300 µs (`cpu/hot_workers_usec`, 0065): City Lab's frames of 16-52 ms are gone,
  1 % low 340-382 fps against milestone 12's 44-46 and stock's 244-260; its mean frame −10.4 % against milestone 12.
- Two reads in stock's SSAO/SSIL code fixed (`postfx/ss_interleave_exact_bounds`, `postfx/ss_depth_mips_exact`,
  0067): a last column never written at widths such as 1920, and racing stores into the depth mipmaps. Two runs of
  FellowEngine now give the same 60-frame sequences bit for bit in Bistro interior, PBR Lab and Sponza; two runs of
  stock differ at 69-82 dB.
- Fewer draws on the ubershader during autotune's evaluations (`autotune/prepare_pipelines`, 0068;
  `frame/depth_pipeline_keys`, 0069): Stress Lab 118-289 per measurement → 1, Lights Lab 15-19 → 0, PBR Lab
  57-174 → 17-49.
- Large surfaces at the LOD of their parts (`geometry/cluster_lod`, 0063, an option, off: it changes the image as
  LOD does): Sponza GPU −2.8 % (6.649 → 6.461 ms), render thread +0.101 ms; 63.6 dB on a moving sequence.
- Final (`m13_f199_final`, stock → fellow158): Sponza 13.61 → 6.39 ms (−53.1 %), Bistro exterior 6.55 → 4.45
  (−32.0 %), City Lab 2.84 → 1.94 (−31.8 %), Lights Lab 3.09 → 2.34 (−24.0 %), Bistro interior 4.32 → 3.36
  (−22.3 %), PBR Lab 2.65 → 2.17 (−18.1 %), Foliage Lab 3.66 → 3.03 (−17.2 %), Stress Lab 5.07 → 4.50 (−11.1 %).
  Load to the first frame −11 % (Sponza) to −67 % (Lights Lab).
- Verification: every default against its absence on lavapipe, 151 comparisons, 148 bit for bit and the three
  documented differences; Vulkan synchronization validation 0 hazards. Tried and removed: compiling autotune's other
  choices ahead (`frame/pipeline_prefetch_tunables`, 0066: its tasks hung the engine's exit on lavapipe).

Details: [report](docs/fellow_milestone13_report.md)

## Milestone 12 — the queues, loading, synchronization (2026-10-19)

- Loading (`frame/parallel_shader_create`, `frame/lazy_shader_variants`, 0061): shaders read from the shader cache
  are created in parallel and only when first asked for; a scene's build to its first frame −12 to −62 % against
  stock (Sponza 3.78 → 3.31 s, Bistro exterior 3.78 → 3.08 s, Lights Lab 0.28 → 0.11 s).
- SSIL on the main queue beside the async SSAO and SSR (`frame/async_ss_split`, 0060): in one process with autotune
  on, Bistro interior −2.7 % GPU, Bistro exterior −0.8 %; exact.
- Two synchronization fixes found by Vulkan validation: driver callbacks' barrier stages (`fellow147`), and a barrier
  for a read from a stage the last write was not made visible to (`debug/visibility_barriers`, 0062; no measurable
  cost).
- Autotune: an undecided evaluation of an option that starts off is continued next, and frames that create pipelines
  are not counted (`autotune/continue_undecided`, `autotune/skip_creation_frames`, 0064): Lights Lab's tile culling
  on for 79-100 % of a run (39-100 % before). The final's Lights Lab +3.5 % against milestone 11 was this coin toss,
  not a change of code.
- Final (`m12_f178_final`, stock → fellow151): Sponza 13.54 → 6.24 ms (−53.9 %), Bistro exterior 6.99 → 4.26
  (−39.1 %), City Lab 3.12 → 2.14 (−31.4 %), Bistro interior 4.24 → 3.35 (−21.1 %), Lights Lab 3.09 → 2.44
  (−21.1 %), Foliage Lab 3.66 → 2.96 (−19.0 %), PBR Lab 2.60 → 2.19 (−15.9 %), Stress Lab 4.99 → 4.92 (−1.4 %,
  n.s.).
- Rejected: large surfaces' clusters drawn front to back (`geometry/cluster_front_to_back`, 0058: 2 % off the depth
  pre-pass, nothing off the frame); a compiled sibling instead of the ubershader while a specialization compiles
  (`frame/pipeline_fallback`, 0059: doubles the compilations where materials first come into view).

Details: [report](docs/fellow_milestone12_report.md)

## Milestone 11 — specialized passes, synchronization between command buffers (2026-10-18)

- The colour passes without the code the frame cannot run (`shading/absent_features_specialized`, 0057): six
  specialization bits for decals, reflection probes, directional lights, omni and spot loops and positional shadows;
  the main variants drop to 72-80 registers. GPU in one process: Sponza −9.5 %, Bistro exterior −6.2 %, Bistro
  interior −6.0 %, Foliage Lab −4.7 %, City Lab −4.2 %, PBR Lab −3.4 %, Lights Lab −0.8 %. Exact after four versions
  (v4 on the RTX: bit for bit in City Lab and Bistro exterior, the floors elsewhere).
- Tone mapping compiled for the frame's paths only (`postfx/tonemap_specialized`, 0055): the pass −12 to −14 %, GPU
  −0.3 to −1.1 %. Volumetric fog's density pass without the loops the frame does not run (`postfx/fog_specialized`,
  0056): the pass −21 to −27 % (Lights Lab 0.200 → 0.159 ms).
- Light tile culling split around the async compute part (0049 v6): Bistro interior −1.9 % GPU. Barriers between the
  command buffers of a frame for resources used again with the same usage (`debug/segment_barriers`, 0054): a real gap
  in the render graph closed, no measurable cost.
- Final (`m11_f168_final`, stock → fellow142): Sponza 13.32 → 5.89 ms (−55.8 %), Bistro exterior 5.90 → 4.09
  (−30.8 %), City Lab 2.82 → 2.04 (−27.6 %), Bistro interior 4.06 → 3.05 (−25.0 %), Lights Lab 2.88 → 2.21
  (−23.3 %), Foliage Lab 3.44 → 2.75 (−20.0 %), PBR Lab 2.48 → 2.17 (−12.2 %), Stress Lab 4.82 → 4.48 (−7.2 %).
  Against milestone 10: Sponza −9.9 %, Bistro interior −6.6 %, Foliage Lab −5.3 %, Bistro exterior −5.1 %.
- Rejected: volumetric fog's light loops walked by the subgroup (`postfx/fog_light_loop`, 0053: the pass −5 % in
  Lights Lab, the frame slower in Sponza and Bistro exterior).

Details: [report](docs/fellow_milestone11_report.md)

## Milestone 10 — stock's inefficiencies: the cluster build, glow's blur (2026-10-14)

- The light cluster build without its 21.5 MB clear per frame, with instanced proxy draws and a pack pass per element
  (`lights/cluster_pack_elements`, `lights/cluster_instanced_draws`, 0051; exact): `debug/cluster_verify` 0
  mismatches over 10.6-21.1 billion words per scene; GPU in one process −2.3 % Lights Lab, −1.1 % PBR Lab, −0.9 %
  City Lab.
- Glow's blur without shared-memory bank conflicts, in 16 × 16 tiles (`postfx/glow_blur`, 0052; exact): glow's region
  0.136-0.199 → 0.053-0.076 ms; GPU City Lab −4.5 %, Lights Lab −3.8 %, PBR Lab −3.8 %, Bistro interior −2.0 %,
  Bistro exterior −1.7 %, Sponza −1.1 %.
- Final (`m10_f152_final`, stock → fellow131): Sponza 13.715 → 6.766 ms (−50.7 %), Bistro exterior 6.754 → 4.451
  (−34.1 %), City Lab 2.954 → 2.070 (−29.9 %), Lights Lab 3.044 → 2.352 (−22.7 %), Foliage Lab 3.652 → 3.015
  (−17.5 %), Bistro interior 4.183 → 3.458 (−17.3 %), PBR Lab 2.558 → 2.293 (−10.4 %), Stress Lab 4.928 → 4.776
  (−3.1 %, p 0.07); Foliage Lab with one MultiMesh per type 10.750 → 5.018 ms (−53.3 %).
- Rejected: the same padding in SSR's roughness filter (`postfx/ssr_filter_padded`: exact, no gain).

Details: [report](docs/fellow_milestone10_report.md)

## Milestone 9 — the positional lights (2026-10-12/14)

- The omni and spot lights' fields read together, before their tests (`lights/fields_early`, 0045; exact): Bistro
  interior −3.1 %, Lights Lab −3.0 % in one process.
- Light tile culling (`lights/tile_cull`, 0049; exact, under autotune): per 8 × 8 tile, the lights whose sphere can
  reach the tile's geometry; the light loop visits half the lights (lavapipe, Lights Lab: 9.8-16.1 → 4.6-9.2 per
  pixel); Lights Lab −5.6 % between processes. It slows other scenes, so autotune switches it on per scene only where
  it measures faster.
- Stock's pipeline cache crashed on save at 2 GiB: a file over `frame/pipeline_cache_max_mb` (1024 MiB) is now
  replaced, and a cache that would overflow the checksum is not saved (0050).
- Final (`m9_f145_final`, stock → fellow127): Sponza 13.591 → 7.123 ms (−47.6 %), Bistro exterior 6.771 → 4.551
  (−32.8 %), City Lab 2.932 → 2.119 (−27.7 %), Lights Lab 3.026 → 2.472 (−18.3 %), Bistro interior 4.199 → 3.536
  (−15.8 %), Foliage Lab 3.602 → 3.128 (−13.2 %), PBR Lab 2.558 → 2.367 (−7.5 %), Stress Lab 4.940 → 4.831 (−2.2 %,
  n.s.). Against milestone 8: Lights Lab −8.0 %, PBR Lab +2.6 % (milestone 10 found build-to-build variation of about
  ±2 % there, with no step at any change).
- Rejected: the next light read while the current one is processed (0046, 2-11 % slower), the loop's fields from an
  80-byte copy (0047), 0048's experiments: the colour pass's main variants sit at 96 registers, a budget to fit in.

Details: [report](docs/fellow_milestone9_report.md)

## Milestone 8 — the opaque pass: what its time is spent on (2026-10-12/14)

- Soft shadow filter taps fetched four at a time, with stock's arithmetic (`shadows/batched_taps`, 0040; exact): in
  one process Bistro exterior −5.0 %, PBR Lab −5.0 %, Sponza −4.6 %, Foliage Lab −3.8 %, Bistro interior −3.6 %,
  Lights Lab −2.1 %, City Lab −0.9 % (n.s.).
- TAA velocities in shared memory (`postfx/taa_shared_velocity`) and SSIL's last frame without its unread full-size
  level (`postfx/ss_last_frame_direct`) (0044; exact): the TAA pass −0.006 to −0.033 ms; the frame −0.5 to −1.0 %
  where SSIL is on.
- Final (`m8_f125_final`, stock → fellow110): Sponza 13.67 → 6.92 ms (−49.3 %), Bistro exterior 6.15 → 4.93
  (−19.8 %), City Lab 2.45 → 2.03 (−17.1 %), Bistro interior 4.12 → 3.61 (−12.5 %), Lights Lab 2.98 → 2.68
  (−10.3 %), Foliage Lab in chunks 3.53 → 3.17 (−10.2 %), PBR Lab 2.55 → 2.42 (−4.9 %), Stress Lab 5.11 → 4.84
  (−5.3 %, within noise); Foliage Lab with one MultiMesh per type 10.24 → 4.97 ms (−51.5 %). Milestone 7's build
  against this one in one session: −3.1 % (City Lab) to −6.8 % (PBR Lab).
- Rejected (seven ideas, six records): register experiments (0037), light cluster tiles under 32 pixels (0038), a
  PCSS early-out after the blocker search (0039), batched SSAO/SSIL taps (0041), two cascades through one PCSS and a
  blocker search shared by pixel quads (0042), dropping the lights that cannot reach a pixel first (0043).

Details: [report](docs/fellow_milestone8_report.md)

## Milestone 7 — vegetation: MultiMesh instances culled one by one (2026-10-11/12)

- MultiMesh culling (`geometry/multimesh_culling`, 0034; exact, on from fellow93): every instance tested against
  every pass's view volume on the GPU (test, scan, compact; order preserved; indirect draws). Foliage Lab with one
  MultiMesh per type −52.9 % against stock (−21 % with milestone 6's defaults), in chunks −9.5 %. Pass groups that
  would draw fewer than 300 000 indices are drawn the stock way (`geometry/multimesh_cull_min_indices`); autotune
  keeps the faster way.
- Per-instance mesh LOD for culled MultiMeshes (`geometry/multimesh_instance_lod`, 0035, an option, off: it changes
  the image; Foliage Lab from above 33 dB on lavapipe, 39.9 dB on the RTX): −2.2 % with the bench's light meshes.
- Swaying vegetation (`geometry/multimesh_cull_vertex_motion`, 0036, an option, off): the node's extra cull margin
  taken as the bound of the vertex motion; Foliage Lab with wind, one MultiMesh per type, stock 10.68 ms → 6.31 ms
  (−40.9 %), against −3.2 % with the defaults.
- Final (`m7_f105_final`, stock → fellow95): Sponza 13.67 → 7.48 ms (−45.3 %), Bistro exterior 6.59 → 4.90
  (−25.6 %), City Lab 2.82 → 2.20 (−22.1 %), Bistro interior 4.19 → 3.91 (−6.6 %, p 0.07), Lights Lab 3.04 → 2.79
  (−8.2 %), PBR Lab 2.63 → 2.53 (−3.8 %), Foliage Lab in chunks 3.61 → 3.27 (−9.5 %), Foliage Lab with one MultiMesh
  per type 10.89 → 5.13 (−52.9 %), Stress Lab 5.00 → 4.94 (−1.3 %, within noise).

Details: [report](docs/fellow_milestone7_report.md)

## Milestone 6 — visually lossless options; dGPU only (2026-10-08/09)

- A second kind of option: changes to the image that measurements show to be invisible (≥ 50-55 dB against stock and
  almost no pixel above 2 levels), off by default. In one process: adaptive shading rate (`shading/adaptive_vrs`,
  0027, under autotune) Lights Lab −21.1 % (opaque pass 1.356 → 0.649 ms), PBR Lab −5.2 %, Bistro interior −2.8 %;
  temporal SSAO (`postfx/ssao_temporal`, 0028) −1.5 to −3.1 %; temporal SSIL (`postfx/ssil_temporal`, 0030) −1.0 to
  −1.5 % where SSIL is used; volumetric fog at half rate (`postfx/fog_half_rate`, 0031) Lights Lab −1.7 %.
- Together, against the defaults between processes: Lights Lab −24.9 %, Bistro interior −9.0 %, PBR Lab −7.0 %, the
  other three scenes −3.2 to −3.4 %.
- TAA direct output (`postfx/taa_direct_output`, 0033; exact, on from fellow91): the post-processing chain reads the
  TAA history texture instead of a copy; the TAA region −0.034 to −0.041 ms, the frame −0.5 to −1.2 %.
- Final (`m6_f94_final`, stock → fellow87, now with the NVIDIA GPU only): Sponza 13.48 → 7.37 ms (−45.3 %), Bistro
  exterior 6.54 → 4.87 (−25.6 %), City Lab 2.84 → 2.16 (−24.0 %, p 0.07), Bistro interior 4.17 → 3.86 (−7.5 %),
  Lights Lab 3.02 → 2.75 (−8.9 %), PBR Lab 2.56 → 2.52 (−1.5 %, within noise).
- Rejected: occlusion culling of the opaque pass (0026: exact, frame +0.1 to +5.6 %); the depth pre-pass with the
  simple elements first (0029); soft shadows with half their taps under TAA (0032, not merged: PBR Lab's penumbrae
  47 dB).

Details: [report](docs/fellow_milestone6_report.md)

## Milestone 5 — scene-dependent choices, persistent instance data (2026-10-07)

- Autotune (`autotune/enabled`, 0020): the render thread runs each scene-dependent exact optimization on and off in
  alternating blocks of frames and keeps the faster choice per scene; all evaluations together take at most 20 % of
  the frames (`autotune/max_duty_percent`).
- Persistent instance data (`cpu/persistent_instances`, 0022): per-surface entries kept between frames, rewritten
  only when they change; City Lab's render thread −13.5 %.
- Async compute on by default on NVIDIA GPUs (`frame/async_compute`, 0015), chosen per scene by autotune. Auto
  exposure's luminance reduction with two barriers per workgroup (`postfx/fast_luminance_reduce`, 0025): the pass
  0.106 → 0.060 ms, 0 mismatches in 44 298 789 texels.
- New defaults against milestone 4's (`m5_f73_defaults`): City Lab 2.396 → 2.299 ms (−4.1 %), draw-heavy City Lab
  −4.6 %, PBR Lab −4.0 %, Lights Lab −1.5 %, Bistro exterior −0.7 % (p 0.17), Bistro interior +0.2 % and Sponza
  +0.2 % (n.s.); with the luminance reduction, Bistro exterior −0.7 %, Bistro interior −0.8 %, Sponza −0.4 % more.
- Rejected: instance data buffers in system memory (0021: GPU +2.5 to +3.4 %), the TAA resolve in 16 × 16 groups
  (0023), SSAO/SSIL gathers by sample rotation (0024).

Details: [report](docs/fellow_milestone5_report.md)

## Milestone 4 — render-thread CPU (2026-10-06/07)

- New bench scene City Lab: about 19 000 MeshInstance3D nodes, 300 carts moving every frame, a sun with 4 cascades;
  CPU-bound in stock. In-process A/B tool (`--ab`, `tools/ab_inproc.py`).
- Render lists built by the render thread and worker tasks with stock's lists, keys, order and instance data
  (`cpu/parallel_*`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, hot workers; 0016); draw lists
  recorded on several threads (`cpu/parallel_recording`, 0018); the end of the frame on other threads: parallel replay,
  present thread and submit thread (`frame/parallel_replay`, `frame/present_thread`, `frame/submit_thread`, 0017).
  `cpu/verify` 0 mismatches in every scene.
- Final (`m4_f61_*_perf`, stock → fellow59): City Lab draw-heavy 3.832 → 2.531 ms (−34.0 %, render thread −42.9 %),
  City Lab 2.777 → 2.321 (−16.4 %), Bistro exterior with the interior 6.335 → 5.051 (−20.3 %), Sponza 13.545 → 7.076
  (−47.8 %), Lights Lab 3.252 → 2.969 (−8.7 %), Bistro interior 4.295 → 4.070 (−5.2 %), PBR Lab 2.800 → 2.784
  (−0.6 %, noise). No scene slower than stock.
- Kept as options, off: the frame plan (`frame/replay_frame_plan`, 0019: −2.7 % in one process, −0.2 % between
  processes); async compute (costs about 3 % in City Lab); idle workers in UMWAIT (`cpu/hot_workers_wait`).

Details: [report](docs/fellow_milestone4_report.md)

## Milestone 3 — the light loop and the shadow filters (2026-10-01/02)

- Light clusters with logarithmic depth slices (`lights/cluster_depth_slices`, 0008): lights visited per pixel in
  Lights Lab 33.9 → 19.1; Lights Lab −0.27 ms (−7.9 %). The zero-attenuation skip (`lights/zero_attenuation_skip`,
  0008): Lights Lab opaque pass −0.38 ms; with both, 1.709 → 1.151 ms.
- Shadow min/max window pyramid v7, directional and positional (`shadows/directional_max_mip`,
  `shadows/positional_max_mip`, 0005): filters skip their taps where one fetch proves the result; Sponza −14 %, Bistro
  interior −3 %, Bistro exterior −1 %; its build 0.06 ms for a fully rewritten 4096² atlas.
- After the measured binary: blended fragments with alpha 0 skip their lighting (`shading/blended_alpha_zero_skip`,
  0010): Blend Lab transparent pass 0.417 → 0.275 ms (−34 %), frame 2.738 → 2.519 ms (−8.0 %); every FellowEngine
  scene-shader feature behind specialization constants (0012): the null test's Lights Lab opaque pass 1.506 ms against
  stock's 1.515 (was 1.847); the cluster pack pass with one workgroup per tile (`lights/cluster_pack_parallel`, 0014):
  Lights Lab −2.3 %, Bistro interior −1.0 %; async compute as an option, off (`frame/async_compute`, 0015: −0.9 to
  −2.1 %, render thread +0.15-0.76 ms).
- Final (`m3_final_perf`, fellow22): Sponza 13.547 → 7.201 ms (−46.8 %), Bistro exterior 5.625 → 4.973 (−11.6 %),
  Bistro interior 4.315 → 4.080 (−5.4 %), Lights Lab 3.238 → 3.018 (−6.8 %), PBR Lab 2.825 → 2.876 (+1.8 %). With
  fellow33 (`m3_f33_perf`): Sponza −47.4 %, Bistro exterior −11.5 %, Blend Lab −8.6 %, Lights Lab −5.7 %, Bistro
  interior −5.5 %, PBR Lab −0.5 % (within noise): no scene slower than stock.
- Bistro scene v2, the benchmark default from 2026-10-04: the exterior also loads the interior (stock 6.641 ms,
  fellow33 5.110 ms, −23.1 %). Fixed: headless `--import` crashed on exit (also in stock).
- Rejected: early submit and presenting from a second queue (0009); SSAO with shared-memory tiles (0011); TAA motion
  vectors of static geometry from the depth buffer (`postfx/taa_camera_motion_vectors`, 0013: not bit-identical,
  frame within noise).

Details: [report](docs/fellow_milestone3_report.md)

## Milestone 2 — shadow maps, shadow lookups, mesh clusters, the TAA regression (2026-10-01)

- Directional shadow static cache (`shadows/directional_cache`, 0004): Sponza shadow pass 3.40 → 0.28 ms, Bistro
  exterior 0.52 → 0.26 ms, render-thread CPU −42 %; VRAM +52 MB where a sun casts shadows.
- Shadow sampling skips for N·L = 0 pixels and an unblended cascade (`shadows/skip_*`, 0005): opaque pass −0.07 to
  −0.29 ms.
- Mesh clusters, frustum culling inside large surfaces (`geometry/mesh_clusters`, 0007): Sponza depth pre-pass −43 %,
  opaque pass −22 % (−1.2 ms GPU). The cluster build was moved off the loading path in fellow17 (Sponza 3.70 s
  against stock's 3.33 s).
- TAA v2 motion soften (0001 follow-up): milestone 1's foliage regression fixed; closer to the ground truth in 8/8
  motion sequences by PSNR and SSIM (+0.1 to +2.3 dB) and on fixed cameras in 4/4 scenes (LPIPS −19 to −24 %).
- Frame time at 1080p (`fellow-m2_perf`): Sponza 13.93 → 8.63 ms (−38.1 %), Bistro exterior 5.75 → 4.96 (−13.7 %),
  Bistro interior 4.56 → 4.41 (−3.3 %), PBR Lab 3.07 → 2.96 (−3.4 %); at 2560x1440 −30.3 %, −11.8 %, −5.3 % and
  −2.3 % (PBR Lab not significant). The null test (`rendering/fellow/enabled=false`) stays within noise.
- Optional look changes, off: the PCSS blend band and a parallel-light penumbra (opaque −0.18 to −0.40 ms with the
  band). Rejected: the max-only shadow pyramid v1 (no net gain); temporal SSAO (0006, parked on branch `fellow/ssao`).

Details: [report](docs/fellow_milestone2_report.md)

## Milestone 1 — temporal anti-aliasing and a GPU shallow-water prototype (2026-09-30)

- TAA resolve v2 (`postfx/taa_resolve_v2`, 0001): reverse-Z closest-depth dilation, dilated-velocity reprojection, a
  YCoCg variance clip that does not collapse under motion. Closer to an accumulated ground truth on fixed cameras in
  4/4 scenes (LPIPS −19 to −25 %) and in 7 of 8 motion sequences (PSNR +0.3 to +1.8 dB).
- TAA history ping-pong (`postfx/taa_history_pingpong`, 0002): two fewer full-screen copies; the TAA stage 14-22 %
  faster (0.04-0.12 ms) at identical output. With v2, the TAA stage is 3-15 % cheaper than stock in all 8
  scene/resolution configurations; the whole frame is unchanged within noise.
- `FellowShallowWater3D` (module `fellow_fluid`, 0003, opt-in): a GPU shallow-water prototype, a 512² grid in 0.21 ms
  (1024² in 0.75-0.79 ms), mass conserving against a NumPy reference, no readback.
- Benchmark suite, renderer audit (`docs/fellow_renderer_audit.md`), ground truth from 2×2 SSAA × 8× MSAA × jitter.
- One measured regression: more shimmer on alpha-tested grass and fence during a lateral strafe (motion error +6 %,
  flicker +5 %, PSNR −0.4 dB); fixed in milestone 2.

Details: [report](docs/fellow_milestone1_report.md)
