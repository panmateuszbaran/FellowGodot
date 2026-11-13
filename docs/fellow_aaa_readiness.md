# FellowEngine — readiness for AAA production (assessment, 2026-11-04)

Status: an assessment after milestone 44 (its rows updated through milestone 110), from the milestones' measurements (the RTX 5070 Ti Laptop and lavapipe) and
what they did not cover. It is a judgement, not a measurement: where it says "not measured", nothing in the 44
milestones measured it.

## 0. Verdict

**Not AAA-ready as an engine. Close to AAA-class in parts of its renderer and simulation, on one platform.**

On a Windows PC with an NVIDIA GPU and Vulkan, FellowEngine renders the benchmark scenes 15-54 % faster than stock
Godot 4.7.2 (Sponza 13.5 → 6.2 ms, Bistro exterior 6.6 → 4.2 ms at 1080p on a laptop GPU), the same frames on every
run, and it has features of a current AAA renderer: ray-traced GI, shadows, reflections and AO, an atmosphere with
volumetric clouds and their shadows, FFT water, a weather system that drives rain, snow, ice, wind and lightning, GPU
smoke, fire and burning objects, automatic HLOD, and characters moved by a neural network. Together that is a strong
base for a **PC-first indie or AA game** with a high-end look.

AAA production needs more than a fast renderer, and here the 44 milestones left most of it as stock Godot gives it:
streaming of large worlds and of textures, platforms beyond Windows/Vulkan/NVIDIA (consoles above all), an animation
stack (motion matching, IK, faces), content pipeline and editor tooling for large teams, upscalers and frame
generation, audio and online middleware, and validation on more than one machine. Those, not the renderer's speed, are
what stands between FellowEngine and an AAA project.

## 1. The bar

"AAA" here means what a current AAA title ships on: a world of tens to hundreds of millions of triangles and gigabytes
of textures streamed as the player moves; 60 fps on the current consoles and a wide range of PC GPUs (NVIDIA, AMD,
Intel) at 1440p-4K with upscaling; hundreds of animated characters with motion matching, IK and facial animation; a
team of dozens to hundreds working in an editor with a content pipeline, profilers and build automation; certification
on each platform.

## 2. Scorecard

Ready: usable for AAA-class content as it is (within the platform limits of section 3). Partial: present and
measured, but short of AAA scale or breadth. Gap: not addressed by FellowEngine (stock Godot's, or missing).

| area | status | what the milestones showed | what is missing for AAA |
|---|---|---|---|
| Frame time (render thread and GPU) | **Ready** (on its platform) | −15 to −54 % against stock in 8 of 9 scenes (`m41_f317_final`); parallel culling and recording, persistent instance data, shadow caches, GPU culling of MultiMesh instances, specialized colour passes, async compute; autotune per scene; bit-for-bit runs | measured on one laptop GPU at 1080p (1440p in milestone 1); Stress Lab (render-thread bound) −1.8 %, within the spread; no 4K, no AMD/Intel numbers |
| Lighting and GI | **Ready / Partial** | ray-traced AO, sun shadows, DDGI (two cascades of probes), reflections (`raytracing/*`, milestone 16): all four +53-66 % GPU (Bistro exterior 4.07 → 6.45 ms); sky atmosphere, volumetric clouds with shadows | RT only through `VK_KHR_ray_query`; a device loss with DDGI and the cloud shadow map was worked around (double-buffered), its cause not found (milestone 33); no RT fallback chain tuned per GPU tier |
| Environment and VFX | **Ready** (a strength) | FFT water with SSR over Hi-Z, shallow-water simulation, rain (puddles, ripples, wet and drying ground, shafts, showers, wind-driven, thin streaks), snow with tracks, ice and hoarfrost, slush, rainbows, wind for plants, water, gas and particles, lightning, a weather controller; GPU smoke and fire, burning objects; each measured (e.g. Rain Lab 0.36 ms, the wind +0.56 ms); in the editor (milestones 52-53) the nodes animate, ask only for the frames they need, have handles, say what stops them working, and come as ready-made setups | not combined at AAA scale (all systems in one large level; Slice Lab's 1 km valley, milestone 55, is the largest); terrain (`FellowTerrain3D`, milestone 57: continuous LOD, 9 blended layers, collision, scattered foliage; milestone 58: sculpted and painted in the editor with undo, or by a game; milestone 61: an edit uploads only the texels it changed; milestone 64: holes — not drawn, no collision, no foliage — cut with a brush, foliage shrinking out over its range's last metres, tree impostors baked into files and drawn from a distance per instance on the GPU; milestone 65: split into regions streamed with the world's cells, without seams — a 4 km map keeps 17.6 MB of terrain textures instead of 213 MB; milestone 70: the material from a virtual texture, pages baked once into an atlas every terrain shares; milestone 71: on the RTX the GPU's frame 5 % shorter at 4K) (the far terrain beyond the streamed cells as baked proxies since milestone 66); the impostors' hand-over a switch, the foliage's range not applied in the shadow passes |
| Geometry scale | **Partial** (M103: double precision) | cluster culling of large surfaces, LOD of their parts (option), contribution culling, `FellowHLOD3D` (City Lab's 18 800 nodes in 14 proxies, render thread −26 %), impostors (option); Sponza's 12.8 M triangles in 6.2 ms | no virtualized geometry (Nanite-class); meshes streamed in two states only (milestone 66: a stand-in, the full mesh near); the largest scene Frontier Lab's 4 km world (milestone 67: about 64 M triangles and 13 000 nodes in its cells and terrain, 5.7 M drawn a frame at 198 fps at 1080p on the RTX); single-precision coordinates by default; milestone 103: double-precision builds checked 103.68 km out (terrain identical, characters and cars within 0.1-0.2 % of the pixels; +0.8-13 % CPU), GPU particles and ray tracing's structures still floats there |
| Streaming and memory | **Partial** (milestones 45-46) | loading to the first frame 12-62 % faster (parallel, lazy shader creation; milestone 12); texture streaming (`streaming/textures`, opt-in): VRAM-compressed textures keep only the mips the cameras can see, loaded on worker threads within a budget — Streaming Lab at 1080p 3-77 MiB of 384 MiB resident; world streaming (`FellowWorldStreamer3D`): cells loaded on worker threads and freed around the camera — World Lab 289-612 MiB of video memory a view instead of 740 MiB, the same images (lavapipe); on the RTX (milestone 49) the same per-view residency, video memory 377-457 instead of 667 MiB, the first frame 838 → 103 ms, no frame-time cost; milestone 65: terrains split into regions streamed with the cells (a 4 km map: 213 → 17.6 MB of terrain textures), a cell's terrain data built on the streaming worker thread; milestone 66: proxies baked for unloaded cells (merged, simplified, vertex-coloured: the horizon kept, 7.8 → 2.0 % of a picture off) and large meshes streamed by distance (a flight's mesh memory 42 → 25 MB); milestone 69: a terrain region enters in 0.8 ms at the median, 1.5 ms at p99 (2.6 and 4.4 before: its neighbours found once, only the facing border refreshed) | a proxy a draw (no grouping of far cells), proxies without textures; mesh streaming in two states (a stand-in and the full mesh), no page-level geometry streaming; DDS and uncompressed textures load whole; the streamers' reports are the only residency tooling; without a budget a flythrough's peak video memory is higher with texture streaming (its swaps' transient copies); Sponza not yet streamed on the RTX |
| Shader compilation and stutter | **Partial** | pipelines created lazily and in parallel, the ubershader while a specialization compiles, a pipeline cache that no longer crashes at 2 GiB, frame pacing (worker counts, hot workers, presentation stalls); the nodes' shaders and kernels made once per session (milestone 53: a second water body 270 → 0.1 ms in the editor); hitches measured over an hour's play (milestone 55: a burning object compiling its pipelines when lit again or at its first ash — fixed; the slice now starts without a hitch, caches warm or off); milestone 59: FellowEngine's scene-dependent specialization bits stable (a scene's 21 changes compile 5 pipelines instead of 28, the same image) and `FellowPipelineWarmer3D` compiling the pipelines around the camera while a scene loads (a camera turning to unseen materials: 0 instead of 7, with MSAA 0 instead of 13; on the RTX the slice's soak 6 pipelines a minute after the warm-up instead of 10-11); milestone 60: the pipeline cache's saves off the device's lock — the slice's 20-minute soak's largest frame 397 → 20 ms, none over 30 ms; milestone 61: the sun's soft and contact shadow bits kept at night, tile culling's bit stable while autotune has it off, and the warm-up's shelf of the scene's meshes and foliage drawn out of sight (lavapipe's slice: 1 pipeline a draw waited for after the warm-up instead of 9; the RTX's 20-minute soak 88 pipelines after it instead of 151), the nodes' kernels cached as SPIR-V, FellowEngine's effects and the materials its nodes make from code baked into exports with Godot's shader baker (a baked fire scene compiles nothing at its first start), the rainbow made ahead (its shader compiled at the first rain of a new build was the soak's one long frame, 89.6 ms); milestone 62: the soft-shadow bit kept under a lightning's flash at night and the meshes hidden until play (an ash heap, a lightning bolt) on the warm-up's shelf (lavapipe's slice on the RTX's timeline: 37 → 20 pipelines after the warm-up, 2 with autotune off), the rain's particle process materials baked; milestone 63: a play-through's pipelines recorded to a file and compiled at load (lavapipe's slice soak: 18 pipelines compiled in play → 0, ubershader draws 268 → 7; the states check 28 → 0 after the warm-up; the RTX's 20-minute soak: load 1251 → 947 ms, the warm-up's 82 pipelines → 0, 67 → 33 after it, the first frame's ubershader draws 22 → 0), the burning material baked into exports; milestone 68: the variants a shader-cache load leaves for later made on a worker thread after it (created outside the version's lock), the snow tracks' kernel when the rain is set up — Frontier Lab's 20-minute soak on the RTX: no shader or kernel made in play (10 variants and a kernel before), the largest frame 92 → 21.5 ms | the record covers the Forward+ scene shaders only (not Mobile's, canvas, sky or particles); what a play-through did not visit compiles in play; a format that depends on the GPU (another VRS method) is not replayed there; what a recording did not meet (autotune's other choices) still compiles in play |
| Characters and animation | **Partial** (M48, M56, M72, M75, M76, M81, M83, M84, M86, M89, M93-M95, M100, M104) | motion matching on clips with inertialization, foot locking and IK, LOD and worker threads (`FellowMotionMatcher3D`: 15 µs a character, 256 in 3 ms with LOD); Crowd Lab: 256 skinned characters, −19 % (elided work, batched skin transforms); `FellowMotionCharacter3D`: clip-free locomotion from NVIDIA's MotionBricks on the GPU (a request 4.0 ms, 16 at once 12.6 ms, 64 at once 41 ms on the RTX; Motion Lab's 64 characters 7.3 ms a frame waiting for them, 3.6 ms with the service on a worker thread), its CPU work off the main thread, falls caught; milestone 56: ground adaptation (the feet placed on slopes, steps and uneven ground, the hips lowered: on an 8° slope 3 mm from where a foot belongs against 28 mm with the root alone), mirrored clips (−18 % foot sliding), tags; milestone 72: mantling and vaulting (`FellowTraversal3D`: ledges found by physics queries, the body moved along a path clear of the world, moving platforms and grabs in the air; `FellowTraversalPose3D`: hands on the edge, hips hanging, feet on the wall by IK over the animation, or a clip warped onto the ledge; 200 characters 0.32 ms of the CPU a frame with Jolt); milestone 75: hanging from ledges with no room on top and moving along them (round walls followed, ends and gaps stopped at, a climb from the hang), ladders (`FellowLadder3D`) climbed up and down, hands and feet on the rungs; milestone 81: round outside and inside corners and jumps between ledges (across a gap, up to a ledge over the edge); milestone 84: walking over an edge into a hang; milestone 86: down from a hang to a ledge below; milestone 93: diagonal jumps; milestone 95: jumps back to the wall behind; milestone 83: faces that speak and blink (`FellowFaceAnimator`: the mouth's blend shapes from the voice's spectrum as it plays, ARKit's names); milestone 89: their eyes looking at a target, with saccades (milestone 94: converging on a near one); milestone 100: expressions (six after FACS, the game's own, faded and layered) and brows raised on the voice's stressed syllables | facial animation is the mouth from the voice's sound, blinks, the eyes' gaze, expressions the game chooses and brows on stressed syllables (no phonemes, no emotion read from the voice; milestone 104: the head nods on stressed syllables and sways while it speaks, not turning with the eyes), no hair; cloth (milestone 73) hangs from nodes or (milestone 76) a skeleton's bones, but is its own mesh (not a skinned garment's vertices), without self-collision; traversal is mantles, vaults, hanging along ledges (round corners, jumps along and up: milestone 81; from the floor over an edge: milestone 84; down to a ledge below: milestone 86; diagonally: milestone 93) and ladders (no free climbing on any surface, no jumps round a corner in the air), and no clip set made for it; the ground's slope not in the search; neural motion is locomotion only, tensor-core-dependent for speed (fp32 products elsewhere: 1.4-2.1× slower on the same GPU), its weights under NVIDIA's Open Model Agreement (shipping needs its terms checked) |
| Physics | **Partial** (M73, M74, M77, M79, M80, M88, M101, M105, M107-M110) | GodotPhysics3D and Jolt faster and exactly stock's (pile × 2.27, rays × 8.6 on GodotPhysics; Jolt areas −42 %), deterministic fast modes; milestone 73: cloth (`FellowCloth3D`: position-based dynamics in small substeps on the worker threads, the scene's wind, collisions with the physics world's simple shapes and a character's body, motion vectors; 64 flags in the wind 1.4-1.6 ms of the CPU's frame); milestone 74: destruction (`FellowDestructible3D`: Voronoi fracture on a worker thread when struck or damaged, closed pieces with cut faces from the mesh's own cross-sections, convex parts, prefractured patterns shared, budgets of pieces a frame and of debris; milestone 80: a structural mode — broken where struck, standing while supported, unsupported parts falling whole); milestone 77: vehicles (`FellowVehicle3D`: wheels on rays with springs, dampers, anti-roll bars and bump stops, combined-slip tyres on the ground's friction, the spin solved implicitly, a hold at a standstill, an engine, an automatic gearbox, differentials, traction control and anti-lock brakes; 0-100 km/h 6.3 s, 100-0 in 40 m, 0.96 g on both engines; 64 cars 0.65-0.80 ms a frame); milestone 79: their skid marks, tyre smoke and synthesised engine, tyre and road sounds (`FellowVehicleEffects3D`); milestone 88: dust and ruts on loose ground, marks that fade | vehicles' wheels on rays or (milestone 101) swept spheres and cylinders (no two-wheelers or tracks; milestone 105: bodies dented where they hit, not soft bodies; milestone 107: the dents bend wheels out of line and take the engine's power (milestone 110: it smokes; a wheel's suspension travel lost; a car that can be damaged 0.4 µs a frame more), no camber; milestone 108: parts come off as bodies of their own (milestone 109: hanging from one end first; milestone 110: doors and bonnets on hinges, with limits), the collision shapes unchanged; loose ground by a group, no thrown gravel or deformed ground); destruction's support is connectivity to the ground (no stress, loads or sagging), static objects only; cloth without self-collision or tearing; milestone 110: thousands of bodies in one pile — GodotPhysics3D 12 ms at 2 000, 144 ms at 10 000 (× 2.2-2.75 stock's), Jolt 3.1 ms at 2 000, 24 ms at 10 000 (stock's), Jolt's default limits overflowing at 10 000 (contacts ignored, not repeatable: raised in the project) |
| CPU and threading | **Partial** | the render thread parallel; worker counts tuned; animation and physics multi-threaded where stock is; milestone 60: a crowd's skeletons −7 % CPU time (exact), its characters posed on worker threads in Godot's process groups (the same poses bit for bit), the separate render thread usable (FellowEngine's GPU resources freed on it; a deadlock in Godot's shader compilation fixed), the main loop's step split in the stats (scene process, sync and draw) | gameplay on GDScript's main thread; process groups are the scene's choice, not automatic; no data-oriented job system for game code; on the laptop's 24 cores Crowd Lab's 256 characters in 8 groups cost the main thread a third (the frame −33 %) |
| Upscaling, frame generation, HDR | **Partial** (M47, M49) | TAA improved, contrast-adaptive sharpening, motion blur, FSR 3.1 upscaler (on by default in the FSR 2 mode from M49): better than FSR 2.2 in motion, the same cost on the RTX (Bistro 2.72 ms at 50 % against TAA's 4.13 ms at 1080p) | no DLSS or XeSS, no frame generation; 1440p and 4K output not measured; HDR output not addressed by FellowEngine |
| Platforms and GPUs | **Gap** (the largest; checked without the hardware in M49) | Windows, Vulkan, Forward+, one NVIDIA laptop GPU measured; milestone 49 without other hardware: the labs on Godot's three renderers (Mobile's lighting stock's again, FellowEngine's RenderingDevice nodes stepping aside in Compatibility), on nine simulated GPUs (Android's 2022 and 2025 baselines, Intel UHD 620 and Arc A770, GTX 1060, RTX 4070, RX 580, RX 7800 XT, Apple M1 through MoltenVK) with no crash, ray tracing falling back cleanly, what each device could not run reported and fixed where FellowEngine's; Windows (MinGW) and Linux arm64 templates built in containers and run under Wine and qemu with Linux's images | no consoles (Godot's console ports are third-party; FellowEngine's RenderingDevice and driver changes would need porting to each); Android's and the web's templates built in milestone 50 (the web one run in a headless browser, the Android one not run), macOS and iOS not (Apple's SDK); no other GPU's performance or driver bugs (a simulated device hides features, it does not reproduce a driver); Direct3D 12 and Metal untouched by FellowEngine's work (ray queries and cooperative matrices are Vulkan-only here); the Mobile renderer draws no gas volumes |
| Editor, tools, content pipeline | **Partial** (milestones 50-54) | the editor builds and opens projects; a headless import crash fixed; the bench (A/B with heat soak, telemetry, per-pass timestamps), autotune; milestone 50: gizmos (a gas volume's box with handles, emitters, water extents, shallow-water domains, streaming cells, wind, lightning, snow trackers) and icons for FellowEngine's nodes, `.bvh` motion capture imported as animations, Project > Tools > Split into Streaming Cells with the cells' content shown in the editor, Performance monitors for texture and world streaming and motion matching, the class reference complete for every node, and an editor check (found the weather property missing from the editor and the pipeline cache's limit read before its setting existed); milestone 51: a command-line toolkit for AI agents (render, describe, check, measure a scene; JSON out); 52: an idle editor costs nothing for FellowEngine's nodes, an Animate toggle, configuration warnings, handles for every shaped node, tooltips for all 296 settings; 53: scenes reopen without recompiling (water 492 → 4.9 ms), a menu of ready-made setups, inspector groups; 58: terrain sculpting and painting tools with undo; 66: streaming proxies baked from Project > Tools | HLOD's impostors baked at run time (the terrain's foliage impostors baked into files, milestone 64), no DCC round-trip, no profiler UI for its passes beyond Godot's visual profiler, no asset validation or cooking pipeline for a large team |
| Audio, networking, UI, localization | **Gap** (audio: M78, M82, M85, M87, M90-M92, M96, M99) | stock Godot, and (milestone 78) sound through the scene's geometry: `FellowAcoustics3D` occludes every 3D sound by the physics world between it and the listener (its level and highs, paths past an edge partly) and sends it to a reverb set by the listener's room (measured by rays: enclosure, size, the nearest wall); 256 sounds 0.18-0.25 ms a frame; milestone 82: sound events (`FellowSoundEvent`: variations, level and pitch varied, instance limits, cooldowns, distances), a budget of voices by priority and audibility (`FellowAudioDirector`), mix snapshots and ducking (`FellowMixSnapshot`); milestone 85: virtual voices (resumed where they would be); milestone 87: low-passes in snapshots; milestone 96: voice limits by bus; milestone 99: openings (`FellowAcousticPortal3D`: a blocked source heard through a doorway, from its direction at the path's length) | no Wwise/FMOD-class authoring (banks, a mixer window, live editing of a running game); occlusion by material (its level and its highs apart), added along a path (milestones 90-92) but not by bands; paths through one opening (milestone 99), no diffraction round other edges; one room's reverb (the listener's); no large-scale multiplayer stack, no platform online services (milestone 106: Windows' TCP clients see a refused connection at once, not after 30 s) |
| Quality and validation | **Partial** (M97, M98, M102: crash and hang reports, sent) | a strict method: every option against its absence bit for bit on lavapipe, A/B in alternating order with heat soak, Vulkan synchronization validation; the labs on simulated GPUs, Godot's three renderers, Wine and qemu (milestone 49); an image regression suite (milestone 54: each group of features against goldens, the release template against the editor, FellowEngine off against on; bit-exact on lavapipe, and on the RTX against its own goldens; it found the gas volume's editor side wrong since milestone 50 and a checker of noise in the interleaved clouds); a vertical slice played for an hour (milestone 55: every system in one valley, 251 fps on the RTX, nothing growing; its hitches traced to a burning object's shaders and fixed); an open world at AAA scale (milestone 67: Frontier Lab, 4 km, 1 024 streamed cells and 256 regions, 2 123 lights, the slice's systems: 198 fps at 1080p, 162 at 1440p, 94 with the 3D frame at 4K on the RTX; 20 minutes of play at 248 fps, 0.4 frames over 15 ms a minute, nothing growing; milestone 68: what it found — a gas volume asleep when far, shaders made ahead, a false error of the MultiMesh pool — 234-259 fps at 1080p, 20 minutes at 272 fps with 0.25 frames over 15 ms a minute, none from compiling; milestone 69: counters for the queue, the draw's phases, frees and uploads attribute every rare frame — an hour of play at 254 fps, p99.9 6.5 ms, 0.15 frames over 15 ms a minute, the rest the GPU, its driver or a lost scheduling quantum, nothing growing); milestone 97: crash reports from shipped games (`FellowCrashReporter`: release templates included — a JSON report of the reason, the frames, the log's last lines, the game's breadcrumbs and values, the machine and GPU, written without the heap; a minidump on Windows; found by the next run); milestone 98: hang reports (a watchdog: the stalled main thread's frames and script, the stall's length); milestone 102: the reports sent to a collector (`upload_reports()`: Sentry's, BugSplat's and Backtrace's minidump form, HTTPS, kept until accepted) | one test machine; no farm across GPUs, drivers and OS versions; hours, not days; the collector is a service's or the studio's own (FellowEngine sends; it ships no server or dashboard) |

## 3. What an AAA project would hit first

1. **Memory and streaming.** Textures stream (milestone 45: only the mips the cameras can see) and a level split into
   cells streams (milestone 46); the authoring side is there — a level split into cells (milestone 50), terrains into
   regions (65), each cell's proxy baked and its large meshes streamed (66) — what is missing are measurements on a
   GPU and a real drive at a large world's scale.
2. **Platforms.** A console version needs a RenderingDevice backend per console (third-party ports exist for stock
   Godot, none carries FellowEngine's changes), and every Vulkan-only feature (ray queries, cooperative matrices,
   async compute choices) needs its counterpart or a fallback. Milestone 49 checked what can be checked without the
   hardware (simulated GPUs, the three renderers, Windows and arm64 builds under Wine and qemu) and fixed what it
   found (a crash without buffer device addresses, Mobile's lights, limits of Android's and Apple's devices);
   milestone 50 built Android's and the web's templates and ran the web one in a headless browser (Android's has no
   emulator here); Apple's platforms need a Mac.
3. **GPU breadth.** Autotune and every default were chosen on one NVIDIA GPU; AMD's and Intel's drivers differ in
   exactly the areas FellowEngine tuned (barriers, async compute, subgroup sizes, ray tracing).
4. **4K and upscaling.** Without DLSS/XeSS and frame generation, the ray-traced feature set's +53-66 % GPU cost is
   hard to afford at 4K; FSR 3.1 (milestone 47) upscales better in motion than FSR 2.2 at FSR 2.2's cost on the RTX
   (milestone 49: −34 to −40 % frame time at half the pixels, 1080p).
5. **Animation.** Motion matching with foot IK (milestone 48) covers locomotion on clips, on uneven ground with
   mirrored and tagged clips since milestone 56; AAA characters also need facial animation, cloth and hair.
6. **Team scale.** The content pipeline is stock Godot's, which is built for small teams; milestone 50 gave
   FellowEngine's nodes their editor side (gizmos, icons, BVH import, the cell splitter, monitors, the class
   reference), not the pipeline a large team needs.

## 4. What it is ready for now

* PC games (Windows, Vulkan, NVIDIA-class GPUs) of indie to AA scope that want a high-end look: weather, water, fire,
  ray-traced lighting, at frame times well under stock's.
* Levels of the benchmark scenes' size (Sponza, Bistro, City Lab: up to 13 M triangles, 19 k nodes) at 60 fps with
  headroom on a laptop GPU.
* Simulation-heavy scenes (rain, snow, gas, water) where its nodes cost a fraction of a millisecond each.
* Research and prototypes: neural character motion in the engine, deterministic frames for testing.

## 5. A path towards AAA, in order of what blocks most

1. **Hardware and platform breadth** (milestone 49: checked in simulation, containers and emulators): measure on AMD
   and Intel GPUs and on the Steam Deck/Linux; build and run Android's and the web's templates; decide Vulkan-only
   (PC/Linux) or bring the features to Direct3D 12; a console strategy (a partner port that carries the
   RenderingDevice changes).
2. **Streaming**: texture mip streaming (milestone 45: done, CPU-estimated, within a budget), cell-based world
   streaming (milestone 46: done; the cell-splitting tool in milestone 50; proxies and streamed meshes in milestone 66),
   memory budgets with tooling.
3. **Upscaling and frame generation**: FSR 3.1's upscaler (milestone 47: done, measured on lavapipe); DLSS and XeSS
   behind the same scaling mode (vendor libraries, their hardware), FSR 3 frame generation (a present path for
   generated frames).
4. **Editor and pipeline**: gizmos and icons for every FellowEngine node (milestone 50: done, with BVH import, the cell
   splitter, monitors and the class reference; milestones 52-53: the nodes live in the editor, with warnings, handles
   and ready-made setups); offline HLOD/impostor baking, a cooking step, a GPU profiler view of its passes,
   shader/PSO pre-warming for shipping builds (milestone 59: scene bits stable and a warm-up node; milestone 63: a play-through's pipelines
   recorded and replayed at load).
5. **Animation**: motion matching on clips with IK and animation LOD (milestone 48: done for locomotion; ground
   adaptation, tags and mirroring in milestone 56: done); neural motion kept as a second path (its next steps: products nearer the
   GPU's limits, target poses beyond locomotion, uneven ground).
6. **Scale and soak tests**: AAA-sized test levels (50-100 M triangles, 10 k+ objects, 100+ lights, all systems
   together), hour-long play sessions, memory and hitch tracking, an automated perf farm (milestone 54: an image
   regression suite on lavapipe, the first standing check; milestone 55: Slice Lab, every system in one valley,
   played for an hour with frame times, hitches and memory recorded; milestone 67: Frontier Lab, a 4 km open world of
   about 64 M triangles, 13 000 nodes and 2 123 lights streamed with proxies, measured on the RTX; milestone 69: played
   for an hour on the RTX, every frame over 15 ms attributed).
7. **Open-world ground**: a terrain with continuous levels of detail, layered materials, collision and foliage
   scattered over its layers (milestone 57: `FellowTerrain3D`, `FellowTerrainFoliage`), sculpted and painted in the
   editor (milestone 58), with holes, foliage fading out and tree impostors baked into files (milestone 64), split
   into regions streamed with the world's cells (milestone 65), the far terrain beyond them as baked proxies
   (milestone 66), its material from a virtual texture (milestone 70: pages baked once into a shared atlas, the far
   ground from each terrain's overview; milestone 71 on the RTX: the GPU's frame 5-10 % shorter where the ground fills
   the view, 5 % at 4K, the CPU's cost about even).
8. **Middleware**: audio (Wwise/FMOD) and online services through plugins.

The order agreed for the next milestones (2026-11-04): streaming (textures in milestone 45, then the world), upscaling
and frame generation, animation, platforms (Godot's own exports, checked in emulated and containerised environments
for want of other machines: Linux containers, other GPU drivers), and the editor and tools last. Done so far: texture
streaming (45), world streaming (46), the FSR 3.1 upscaler (47), motion matching (48); DLSS, XeSS and frame
generation wait for the GPU; platform checks without the hardware (49); the editor tools (50) — the agreed order done.

**The next order (2026-11-06)**, the owner's large-team and neural-upscaling items set aside: (1) an image regression
suite run before each change (milestone 54: done); (2) a vertical slice — one large level with every system at once,
played for an hour with frame times, hitches and memory recorded (milestone 55: done); (3) terrain and foliage
(milestone 57: the terrain node and its scattered foliage; milestone 58: sculpting and painting: done); (4) pipeline pre-warming against
first-use stutter (milestones 55 and 57: the slice's fire, snow and foliage now compile when they are set up;
milestone 59: FellowEngine's scene bits stable and a warm-up node at load — done on lavapipe, the RTX's measurement
pending); (5) animation: ground adaptation, tags, mirroring (milestone 56: done);
(6) other GPUs (AMD, Intel, the Steam Deck) when the hardware is at hand. Consoles, FellowEngine's own audio and
networking, and Nanite-class geometry are not on it.

**The next order (2026-11-10)**, after the showcase was set aside: (1) a play-through's pipelines recorded and
replayed at load, the burning material baked (milestone 63: done); (2) foliage fading out with distance, tree
impostors baked offline, holes in the terrain (milestone 64: done), terrain regions streamed with the world's cells
(milestone 65: done); (3) proxies baked
offline for unloaded cells, mesh LOD streaming (milestone 66: done).

**With the GPU again (2026-11-10, "kontynuuj z GPU")**: (1) an AAA-scale level measured on the RTX (milestone 67:
Frontier Lab, done); (2) what it found — the gas simulation at any distance, shaders and kernels made in play, the
spikes of the main thread and the GPU (milestone 68: done — the gas asleep when far, no shader made in play, a false
error found; two kinds of rare frames of 17-19 ms remain, once each in twenty minutes); (3) those frames attributed
and an hour of play (milestone 69: done — terrain regions entering were most of them; the rest the GPU, its driver or
a lost scheduling quantum, 0.15 a minute).

**Virtual texturing without the GPU (2026-11-11, "dawaj to wirtualne teksturowanie ale na razie bez gpu")**: the
terrain's material from a virtual texture, built and checked on lavapipe (milestone 70: done — pages baked into a
shared atlas, an overview per terrain, the same views as the layers' within 41-44 dB; the layers' colours fixed on the
way); its cost and benefit on the RTX next (milestone 71: measured — the GPU's frame shorter by 5-10 % where the ground
fills the view, 5 % at 4K; the CPU's cost of choosing and baking pages brought down to about what they save; off by
default, for projects whose frame is the GPU's).

**Out of scope by the owner's decision (2026-11-05).** FellowEngine is developed for its owner's own work, much of it
with AI agents, not for a large team: a content pipeline for large teams (asset validation, cooking, DCC round-trips,
team workflows; point 4's pipeline items) is not pursued, and neither are DLSS and XeSS (neural upscalers) or frame
generation for now (point 3); FSR 3.1's hand-written upscaler stays. The rows above still say what an AAA studio
would miss.

## 6. Neural motion's state (milestones 43-44)

* GPU: a request 4.0 ms, 16 at once 12.6 ms, 64 at once 41 ms on the RTX's tensor cores (milestone 44, the laptop
  presumably idle; milestone 43's kernel 4.1, 15.2 and 51.6 ms the same night): the activations loaded four at a time
  and the epilogue a row at a time, the same results bit for bit. Six other kernel designs were measured and dropped;
  the products are bound by each workgroup's latency through its K steps and by the epilogue, still far from the
  tensor cores' limits.
* CPU (milestone 44): encoding, decoding and endpoint correction on the service's worker threads, in parallel; a
  character's frame only the joints the model drives — 64 characters' main-thread work 0.89 → 0.21 ms a step on the
  laptop's CPU.
* Motion Lab on the RTX (1080p): 64 characters 7.33 ms a frame in flush mode, 3.56 ms asynchronous (the 99th percentile
  6.1 ms) — milestone 43's 8.52 and 4.30 ms the same night; 16 characters 3.43 ms. Neural motion is affordable for a
  crowd of tens of characters on a high-end GPU, not yet for hundreds.
* Behaviour: walks, jogs, runs, turns, stops without clips; falls caught and recovered (32 characters × 40 s: 15
  falls, all back up, no one down for 3 s).
* Not yet: other motions (crouch, jump, interact), terrain, obstacles, cheaper fp32 path for GPUs without tensor
  cores, a measured comparison with clip-based locomotion's look.
