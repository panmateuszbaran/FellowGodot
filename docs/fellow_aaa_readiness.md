# FellowEngine — readiness for AAA production (assessment, 2026-11-04)

Status: an assessment after milestone 44, from the milestones' measurements (the RTX 5070 Ti Laptop and lavapipe) and
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
| Environment and VFX | **Ready** (a strength) | FFT water with SSR over Hi-Z, shallow-water simulation, rain (puddles, ripples, wet and drying ground, shafts, showers, wind-driven, thin streaks), snow with tracks, ice and hoarfrost, slush, rainbows, wind for plants, water, gas and particles, lightning, a weather controller; GPU smoke and fire, burning objects; each measured (e.g. Rain Lab 0.36 ms, the wind +0.56 ms) | authoring: little editor support (the weather does nothing in the editor, few gizmos); not combined at AAA scale (all systems in one large level) |
| Geometry scale | **Partial** | cluster culling of large surfaces, LOD of their parts (option), contribution culling, `FellowHLOD3D` (City Lab's 18 800 nodes in 14 proxies, render thread −26 %), impostors (option); Sponza's 12.8 M triangles in 6.2 ms | no virtualized geometry (Nanite-class), no mesh or LOD streaming; the largest scene 12.8 M triangles and 18 800 nodes; single-precision coordinates (Godot's double-precision builds not tested) |
| Streaming and memory | **Partial** (milestones 45-46) | loading to the first frame 12-62 % faster (parallel, lazy shader creation; milestone 12); texture streaming (`streaming/textures`, opt-in): VRAM-compressed textures keep only the mips the cameras can see, loaded on worker threads within a budget — Streaming Lab at 1080p 3-77 MiB of 384 MiB resident; world streaming (`FellowWorldStreamer3D`): cells loaded on worker threads and freed around the camera — World Lab 289-612 MiB of video memory a view instead of 740 MiB, the same images (lavapipe); on the RTX (milestone 49) the same per-view residency, video memory 377-457 instead of 667 MiB, the first frame 838 → 103 ms, no frame-time cost | no tool to split a level into cells, no baked proxies for unloaded cells, no mesh LOD streaming; DDS and uncompressed textures load whole; the streamers' reports are the only residency tooling; without a budget a flythrough's peak video memory is higher with texture streaming (its swaps' transient copies); Sponza not yet streamed on the RTX |
| Shader compilation and stutter | **Partial** | pipelines created lazily and in parallel, the ubershader while a specialization compiles, a pipeline cache that no longer crashes at 2 GiB, frame pacing (worker counts, hot workers, presentation stalls) | no offline PSO collection and pre-warming for shipping builds; hitches measured per scene, not over a play session |
| Characters and animation | **Partial** (M48) | motion matching on clips with inertialization, foot locking and IK, LOD and worker threads (`FellowMotionMatcher3D`: 15 µs a character, 256 in 3 ms with LOD); Crowd Lab: 256 skinned characters, −19 % (elided work, batched skin transforms); `FellowMotionCharacter3D`: clip-free locomotion from NVIDIA's MotionBricks on the GPU (a request 4.0 ms, 16 at once 12.6 ms, 64 at once 41 ms on the RTX; Motion Lab's 64 characters 7.3 ms a frame waiting for them, 3.6 ms with the service on a worker thread), its CPU work off the main thread, falls caught | no ground adaptation, tags or mirroring in the matching yet, no facial animation, cloth or hair; neural motion is locomotion only, tensor-core-dependent for speed (fp32 products elsewhere: 1.4-2.1× slower on the same GPU), its weights under NVIDIA's Open Model Agreement (shipping needs its terms checked) |
| Physics | **Partial** | GodotPhysics3D and Jolt faster and exactly stock's (pile × 2.27, rays × 8.6 on GodotPhysics; Jolt areas −42 %), deterministic fast modes | no destruction, vehicles or cloth beyond stock; thousands of bodies not measured |
| CPU and threading | **Partial** | the render thread parallel; worker counts tuned; animation and physics multi-threaded where stock is | gameplay on GDScript's main thread (Crowd Lab's frame is the main thread animating); no data-oriented job system for game code |
| Upscaling, frame generation, HDR | **Partial** (M47, M49) | TAA improved, contrast-adaptive sharpening, motion blur, FSR 3.1 upscaler (on by default in the FSR 2 mode from M49): better than FSR 2.2 in motion, the same cost on the RTX (Bistro 2.72 ms at 50 % against TAA's 4.13 ms at 1080p) | no DLSS or XeSS, no frame generation; 1440p and 4K output not measured; HDR output not addressed by FellowEngine |
| Platforms and GPUs | **Gap** (the largest; checked without the hardware in M49) | Windows, Vulkan, Forward+, one NVIDIA laptop GPU measured; milestone 49 without other hardware: the labs on Godot's three renderers (Mobile's lighting stock's again, FellowEngine's RenderingDevice nodes stepping aside in Compatibility), on nine simulated GPUs (Android's 2022 and 2025 baselines, Intel UHD 620 and Arc A770, GTX 1060, RTX 4070, RX 580, RX 7800 XT, Apple M1 through MoltenVK) with no crash, ray tracing falling back cleanly, what each device could not run reported and fixed where FellowEngine's; Windows (MinGW) and Linux arm64 templates built in containers and run under Wine and qemu with Linux's images | no consoles (Godot's console ports are third-party; FellowEngine's RenderingDevice and driver changes would need porting to each); Android, web, macOS and iOS templates not built (toolchains not downloaded; Apple's SDK); no other GPU's performance or driver bugs (a simulated device hides features, it does not reproduce a driver); Direct3D 12 and Metal untouched by FellowEngine's work (ray queries and cooperative matrices are Vulkan-only here); the Mobile renderer draws no gas volumes |
| Editor, tools, content pipeline | **Gap** | the editor builds and opens projects; a headless import crash fixed; the bench (A/B with heat soak, telemetry, per-pass timestamps), autotune | FellowEngine's work is in the runtime: few editor previews or gizmos for its nodes, HLOD and impostors baked at run time, no DCC round-trip, no profiler UI for its passes, no asset validation or cooking pipeline for a large team |
| Audio, networking, UI, localization | **Gap** | not touched (stock Godot) | no Wwise/FMOD-class middleware in the engine, no large-scale multiplayer stack, no platform online services |
| Quality and validation | **Partial** | a strict method: every option against its absence bit for bit on lavapipe, A/B in alternating order with heat soak, Vulkan synchronization validation; the labs on simulated GPUs, Godot's three renderers, Wine and qemu (milestone 49) | one test machine; no farm across GPUs, drivers and OS versions; no long soak or memory-leak runs; no crash reporting |

## 3. What an AAA project would hit first

1. **Memory and streaming.** Textures stream (milestone 45: only the mips the cameras can see) and a level split into
   cells streams (milestone 46); what is missing is the authoring side — splitting a level, baking each cell's proxy
   — and measurements on a GPU and a real drive.
2. **Platforms.** A console version needs a RenderingDevice backend per console (third-party ports exist for stock
   Godot, none carries FellowEngine's changes), and every Vulkan-only feature (ray queries, cooperative matrices,
   async compute choices) needs its counterpart or a fallback. Milestone 49 checked what can be checked without the
   hardware (simulated GPUs, the three renderers, Windows and arm64 builds under Wine and qemu) and fixed what it
   found (a crash without buffer device addresses, Mobile's lights, limits of Android's and Apple's devices); Android
   and the web need their toolchains, Apple's platforms a Mac.
3. **GPU breadth.** Autotune and every default were chosen on one NVIDIA GPU; AMD's and Intel's drivers differ in
   exactly the areas FellowEngine tuned (barriers, async compute, subgroup sizes, ray tracing).
4. **4K and upscaling.** Without DLSS/XeSS and frame generation, the ray-traced feature set's +53-66 % GPU cost is
   hard to afford at 4K; FSR 3.1 (milestone 47) upscales better in motion than FSR 2.2 at FSR 2.2's cost on the RTX
   (milestone 49: −34 to −40 % frame time at half the pixels, 1080p).
5. **Animation.** Motion matching with foot IK (milestone 48) covers locomotion on clips; AAA characters also need
   ground adaptation, tagged actions, facial animation, cloth and hair.
6. **Team scale.** The editor and the content pipeline are stock Godot's, which is built for small teams.

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
   streaming (milestone 46: done; proxies and a cell-splitting tool to come), mesh LOD streaming, memory budgets with
   tooling.
3. **Upscaling and frame generation**: FSR 3.1's upscaler (milestone 47: done, measured on lavapipe); DLSS and XeSS
   behind the same scaling mode (vendor libraries, their hardware), FSR 3 frame generation (a present path for
   generated frames).
4. **Editor and pipeline**: previews and gizmos for every FellowEngine node in the editor, offline HLOD/impostor
   baking, a cooking step, a GPU profiler view of its passes, shader/PSO pre-warming for shipping builds.
5. **Animation**: motion matching on clips with IK and animation LOD (milestone 48: done for locomotion; ground
   adaptation, tags and mirroring to come); neural motion kept as a second path (its next steps: products nearer the
   GPU's limits, target poses beyond locomotion, uneven ground).
6. **Scale and soak tests**: AAA-sized test levels (50-100 M triangles, 10 k+ objects, 100+ lights, all systems
   together), hour-long play sessions, memory and hitch tracking, an automated perf farm.
7. **Middleware**: audio (Wwise/FMOD) and online services through plugins.

The order agreed for the next milestones (2026-11-04): streaming (textures in milestone 45, then the world), upscaling
and frame generation, animation, platforms (Godot's own exports, checked in emulated and containerised environments
for want of other machines: Linux containers, other GPU drivers), and the editor and tools last. Done so far: texture
streaming (45), world streaming (46), the FSR 3.1 upscaler (47), motion matching (48); DLSS, XeSS and frame
generation wait for the GPU; platform checks without the hardware (49).

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
