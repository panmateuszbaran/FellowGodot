# FellowEngine — milestone 16 report (a better image: post-processing and ray tracing)

Status: **OPEN** (`fellow171`-`fellow174`, measurements `m16_f231`-`m16_f236`, 2026-10-24). Branch `fellow/geometry`.
Benchmark machine as before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only,
Windows 11, 1920×1080, "high" preset). Decision records `docs/decisions/0078`-`0084`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Visual features", "Ray tracing").

## 0. Result

The direction changed with this milestone (the user, 2026-10-22): the engine should also look better — global
illumination, ray tracing, better post-processing — at a cost the frame can afford. Everything new is **off by default**:
with every setting at its default the image is the previous milestone's bit for bit (lavapipe: City Lab's 60-frame warm
sequence and Crowd Lab's captures, 0 differing pixels, rerun after every change; ray tracing enabled with no effect on,
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

Their cost on the RTX at 1080p, in one process against ray tracing on with no effect (GPU time; the latest
measurement of each, `m16_f233`/`m16_f235`):

| effect | City Lab | Sponza |
|---|---:|---:|
| RTAO | −6 % (cheaper than SSAO) | +8 % |
| RT sun shadows | 0 % | +11 % |
| DDGI | +28 % | +32 % |
| RT reflections | +12 % | +17 % |
| all four | +32 % | +69 % |

## 1. The first measurement and what it found (`m16_f231`)

The first build with all of it (`fellow171`) measured each effect on the RTX and captured the scenes with every effect
on. Three things only the real GPU showed:

* **The render thread.** In City Lab every effect took the frame from 2.1 to 8.6-9.2 ms: building the top-level
  structure cost 5.7 ms of the render thread for 7 600 instances. Stock Godot's Vulkan driver wrote each instance record
  into the mapped instance buffer field by field — its bitfields with read-modify-writes of memory the GPU reads
  (uncached or write-combined) — and asked the driver for the BLAS's device address for every instance. Built in local
  memory and copied whole, with the address asked once per BLAS: **0.6 ms** (`m16_f233`; frame 3.0-3.3 ms).
* **Alpha-tested surfaces.** The rays do not test alpha, and as whole quads leaves occluded everything behind them:
  Sponza's tree came out black. They are now non-opaque instances on their own mask bits: AO, GI and reflection rays
  skip them, shadow rays leave the pixels only they shadow to the shadow map. MultiMesh and particle shadow casters,
  which the scene does not hold yet, make the traced shadows assert shadow only (Foliage Lab's forest floor had come out
  sunlit under its MultiMesh trees).
* **The GPU cost of DDGI and RTAO** (+60-78 % and up to +33 %). DDGI now updates a quarter of its probes per frame and
  gives the opaque passes its irradiance from a screen pass (the lookup had cost Sponza's opaque pass 1.28 ms); RTAO
  traces a quarter of the pixels per frame; rough reflections too (`m16_f235`: Sponza's reflections 2.26 → 1.57 ms).

## 2. Lighting units

The first DDGI and reflections lit their hits with the sun π times too bright (the scene shader's Lambert 1/π left out)
and lit metals as diffuse surfaces; a chrome sphere reflected the ground brighter than the ground (lavapipe, 0084).
Fixed in both.

## 3. Open

* **Sponza with DDGI**: arcades and a column came out black on the RTX (`m16_f235`): probes inside walls and columns
  modelled double-sided stayed active. `fellow174` counts their back faces (`m16_f236`, being checked).
* The scene holds mesh instances only: MultiMesh (foliage) and particles are not in it.
* The render thread still spends about 1 ms on the scene in City Lab (6 400 instances): the culler's gathering 0.4 ms,
  the build 0.5 ms.

## 4. Incidents

* **The laptop hung** on 2026-10-24 at about 07:08 and had to be switched off; the runner's last heartbeat is 07:07:51.
  Nothing of the benchmark's was running then: the last job had ended at 06:46, the runner was paused, its loop rewriting
  one line every 15 s. Since then jobs are kept to about 15 minutes of GPU work with rests before them, and large result
  files are analysed in the cloud rather than on the laptop.
* **The queue runner (v2)** no longer exits on a pause: `PAUSE`, `ABORT` (ends the running job's process tree) and its
  own update are files in `.tools/queue/`, so pausing, stopping a job and updating the runner need nobody at the machine.
  Only its start does (after a restart of the laptop).
