# FellowEngine — milestone 16 report (a better image: post-processing and ray tracing)

Status: **IN PROGRESS** (`fellow171`-`fellow178`, RTX measurements and captures `m16_f231`-`m16_f242`, 2026-10-24).
Branch `fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop
GPU, NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records `docs/decisions/0078`-`0084`;
user-facing summary `docs/FELLOWENGINE_OPTIONS.md` ("Visual features", "Ray tracing").

## 0. Result

The direction changed with this milestone (the user, 2026-10-22): the engine should also look better — global
illumination, ray tracing, better post-processing — at a cost the frame can afford. Everything new is **off by default**:
with every setting at its default the image is the previous milestone's bit for bit (lavapipe: City Lab's 60-frame warm
sequence and Crowd Lab's captures, 0 differing pixels, rerun after the changes; ray tracing enabled with no effect on,
the same).

* **Motion blur (0078)** after TAA, a reconstruction filter over the velocity buffer (the sky blurred by the camera's
  motion): City Lab GPU +0.35 ms, Sponza +0.39 ms on the RTX (`m16_f231`).
* **Lens effects (0079)** folded into the tone mapping pass (chromatic aberration, vignette, film grain): within noise.
* **Ray tracing (0080)**: ray queries enabled in the Vulkan driver (stock Godot never turned them on), a bottom-level
  structure per mesh surface, the frame's top-level structure from the mesh instances within 200 m of the camera. On
  it four effects, each replacing a screen-space or approximate one:
  * **RTAO (0081)** in place of SSAO: what is off screen or behind the visible surface occludes;
  * **ray-traced sun shadows (0082)**: exact contacts, no bias, no acne;
  * **DDGI (0083)**: coloured bounce light from a volume of ray-traced irradiance probes around the camera;
  * **ray-traced reflections (0084)** in place of SSR: off-screen and occluded objects reflected.

Their GPU cost on the RTX at 1080p, in one process against ray tracing on with no effect (`m16_f233`, reflections and
all four `m16_f235`):

| effect | City Lab | Sponza |
|---|---:|---:|
| RTAO (a quarter of the pixels per frame) | −6 % (cheaper than SSAO) | +8 % |
| RT sun shadows | 0 % | +11 % |
| DDGI (a quarter of the probes per frame, screen pass) | +28 % | +32 % |
| RT reflections (rough ones interleaved) | +12 % | +17 % |
| all four | +32 % | +69 % |

The first build measured (`m16_f231`, everything at full rate) had cost +60-78 % for DDGI, up to +33 % for RTAO, and
in City Lab 6.4-7.0 ms of the render thread for every effect (section 1).

## 1. What the RTX showed

The first build with all of it (`fellow171`) was measured effect by effect on the RTX and captured with every effect on.
Most of what followed came from those captures, which lavapipe's scenes had not shown:

* **The render thread.** In City Lab every effect took the frame from 2.1 to 8.6-9.2 ms: building the top-level
  structure cost 5.7 ms of the render thread for 7 600 instances. Stock Godot's Vulkan driver wrote each instance record
  into the mapped instance buffer field by field — its bitfields with read-modify-writes of memory the GPU reads — and
  asked the driver for the BLAS's device address for every instance. Built in local memory and copied whole, the address
  asked once per BLAS, each surface's GI data kept with the surface: **0.6 ms** (`m16_f233`; frame 3.0-3.3 ms).
* **Alpha-tested surfaces** (leaves, fences): the rays do not test alpha, and as whole quads leaves occluded everything
  behind them (Sponza's tree black). They are now non-opaque instances on their own mask bits: AO, GI and reflection
  rays skip them, shadow rays leave the pixels only they shadow to the shadow map. MultiMesh and particle shadow casters,
  which the scene does not hold yet, make the traced shadows assert shadow only (Foliage Lab's forest floor had come out
  sunlit under its MultiMesh trees).
* **The GPU cost**: DDGI updates a quarter of its probes per frame and gives the opaque passes its irradiance from a
  screen pass (the lookup had cost Sponza's opaque pass 1.28 ms); RTAO traces a quarter of the pixels per frame; rough
  reflections too (Sponza's reflections 2.26 → 1.57 ms).
* **Sponza with DDGI** came out with black areas, which took four RTX rounds to separate (`m16_f235`-`m16_f242`), with
  per-effect captures, the ray tracing scene seen from the camera (`debug/raytracing_view`) and the probes dumped
  (`debug/raytracing_gi_dump_every`):
  * pixels facing away from the sun by the depth buffer's normal were shadowed without a ray — that normal is noise on
    foliage: now left to the shadow map (0082);
  * probes inside walls and columns modelled double-sided stayed active and black: their back faces now count (0083);
  * the hits' normals inverted the instance transform, singular for compressed flat meshes (NaN) — now taken from the
    world-space triangle, and a NaN never stored;
  * what remained is physical: the probes in Sponza's courtyard hold about 13 % of the light outside the building, those
    in the galleries behind the curtains under 1 % — Godot's ambient light, never occluded, had lit them all as outdoors.
    As Unreal's Lumen ("skylight leaking"), a share of the ambient light (`gi_sky_leak`, 0.1) is kept: the galleries are
    dark, no longer black.

## 2. Lighting units

The first DDGI and reflections lit their hits with the sun π times too bright (the scene shader's Lambert 1/π left out)
and lit metals as diffuse surfaces; a chrome sphere reflected the ground brighter than the ground (lavapipe, 0084).
Fixed in both.

## 3. Open

* The scene holds mesh instances only: MultiMesh (foliage) and particles are not in it (their shadows are the shadow
  map's; they do not occlude AO or GI, and are not reflected).
* The rays do not test alpha (leaves are skipped, not cut out) and see material colours, not textures.
* The render thread spends about 1 ms on the scene in City Lab (6 400 instances): the culler's gathering 0.4 ms, the
  build 0.5 ms.
* All four effects together still cost Sponza 69 % of its GPU frame: the traced sun shadows (0.9 ms) and the
  reflections' smooth surfaces are traced every frame at full resolution.

## 4. Incidents

* **The laptop hung** on 2026-10-24 at about 07:08 and had to be switched off; the runner's last heartbeat is 07:07:51.
  Nothing of the benchmark's was running then: the last job had ended at 06:46, the runner was paused, its loop rewriting
  one line every 15 s. Since then each job does at most about 15 minutes of GPU work, with a rest before it, and large
  result files are analysed in the cloud rather than on the laptop.
* **The queue runner (v2)** no longer exits on a pause: `PAUSE`, `ABORT` (ends the running job's process tree) and its
  own update are files in `.tools/queue/`, so pausing, stopping a job and updating the runner need nobody at the machine.
  Only its start does (after a restart of the laptop).

## 5. Next

* MultiMesh instances in the ray tracing scene (Foliage Lab), built on the GPU from their transforms.
* The traced sun shadows interleaved like the AO; the reflections of smooth surfaces at half resolution.
* DDGI: probe relocation (out of walls), a coarser cascade around the fine volume, texture albedo at hits.
* A path-traced reference view for comparing the real-time image against.
* Then, as asked for the engine's direction: collisions and physics.
