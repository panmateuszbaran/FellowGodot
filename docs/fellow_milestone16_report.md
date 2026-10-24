# FellowEngine — milestone 16 report (a better image: post-processing and ray tracing)

Status: **DONE** (`fellow171`-`fellow182`, RTX measurements and captures `m16_f231`-`m16_f246`, 2026-10-24). Branch
`fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU,
NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records `docs/decisions/0078`-`0084`; user-facing
summary `docs/FELLOWENGINE_OPTIONS.md` ("Visual features", "Ray tracing").

## 0. Result

The direction changed with this milestone (the user, 2026-10-22): the engine should also look better — global
illumination, ray tracing, better post-processing — at a cost the frame can afford. Everything new is **off by default**:
with every setting at its default the image is the previous milestone's bit for bit (lavapipe: City Lab's 60-frame warm
sequence and Crowd Lab's captures against the binary before the milestone, 0 differing pixels, rerun with the last
build; ray tracing enabled with no effect on, the same).

* **Motion blur (0078)** after TAA, a reconstruction filter over the velocity buffer (the sky blurred by the camera's
  motion): City Lab GPU +0.35 ms, Sponza +0.39 ms on the RTX (`m16_f231`).
* **Lens effects (0079)** folded into the tone mapping pass (chromatic aberration, vignette, film grain): within noise.
* **Ray tracing (0080)**: ray queries enabled in the Vulkan driver (stock Godot never turned them on), a bottom-level
  structure per mesh surface, the frame's top-level structure from the mesh and MultiMesh instances within 200 m of the
  camera. On it four effects, each replacing a screen-space or approximate one:
  * **RTAO (0081)** in place of SSAO: what is off screen or behind the visible surface occludes;
  * **ray-traced sun shadows (0082)**: exact contacts, no bias, no acne;
  * **DDGI (0083)**: coloured bounce light from two cascades of ray-traced irradiance probes around the camera, the
    materials' texture colours at the hits, probes moved out of walls;
  * **ray-traced reflections (0084)** in place of SSR: off-screen and occluded objects reflected;
  * and a **path-traced reference** for DDGI (`debug/raytracing_gi_reference`).

Their GPU cost with the milestone's last build, on the RTX at 1080p, in one process against ray tracing on with no
effect (`m16_f246_rt_ab`, `fellow182`; SSR on, autotune off):

| effect | City Lab | Sponza | Bistro exterior |
|---|---:|---:|---:|
| RTAO (a quarter of the pixels per frame) | −4 % (cheaper than SSAO) | +5 % | +17 % |
| RT sun shadows (a quarter of the pixels per frame) | +4 % | +7 % | +14 % |
| DDGI (two cascades, a quarter / a sixteenth of the probes per frame, screen pass) | +57 % | +22 % | +44 % |
| RT reflections (rough ones interleaved) | +8 % | +13 % | +5 % |
| all four | +66 % (2.15 → 3.55 ms) | +53 % (6.33 → 9.72 ms) | +59 % (4.07 → 6.45 ms) |

The render thread pays for the scene: about 1 ms in City Lab and Bistro (the culler's gathering 0.1-0.4 ms, the build
0.6-0.9 ms), 0.1-0.2 ms in Sponza. The first build measured (`m16_f231`, everything at full rate) had cost +60-78 % for
DDGI alone, up to +33 % for RTAO, and in City Lab 6.4-7.0 ms of the render thread for every effect (section 1); all four
cost Sponza +69 % in `m16_f235`.

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
  rays skip them, shadow rays leave the pixels only they shadow to the shadow map. Shadow casters the scene does not
  hold make the traced shadows assert shadow only (Foliage Lab's forest floor had come out sunlit under its MultiMesh
  trees, before they were in the scene: section 2).
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

## 2. Closing the ray tracing (`fellow179`-`fellow182`; `m16_f243`-`m16_f246`)

* **MultiMesh instances in the scene** (0080). Foliage, rocks and props drawn with MultiMesh were not in the ray
  tracing scene: they did not occlude AO or GI, were not reflected, and made the traced shadows fall back to the shadow
  map. Their transforms are now read on the CPU (the buffer last set, kept by reference while ray tracing is active),
  and, as Unreal culls small instances from its ray tracing scene, an instance seen under less than 1° is left out (a
  grass blade beyond 17 m). Foliage Lab on the RTX: the forest floor and the trunks now take the canopy's occlusion and
  bounce, the meadow's rocks the grass's (lavapipe counts about 8 400 MultiMesh instances in the scene at the meadow
  camera). Cost (`m16_f243_foliage_ab`, every effect, in one process): GPU +0.22 ms (the TLAS build 0.32 ms instead of
  0.04 ms), render thread +0.6 ms. Telling a MultiMesh had read every instance's data, a cache miss per instance (City
  Lab's build 0.5 → 0.8 ms): the culler now notes it in the instance's mask byte (0.6 ms).
* **Sun shadows interleaved** (0082), as the AO: Sponza's trace 1.06 → 0.54 ms (GPU +0.98 → +0.53 ms against no
  effect). Interleaving exposed an older error: the effects rebuilt positions from the depth buffer with the projection
  without the TAA jitter it was rendered with, up to half a pixel off — inside walls seen at a grazing angle. One frame
  in eight City Lab's sunlit walls were traced as shadowed; accumulation had hidden it, the interleaved clamp showed it.
  Every effect now uses the jittered projection.
* **Reflections**: a thread per 2 × 2 block (no lane idle over interleaved rough surfaces) was tried and was slower
  (Sponza 2.04 ms against 1.57 ms: smooth pixels traced four in a row on one lane); back to a thread per pixel.
* **DDGI** (0083):
  * the hits see the material's colour × the mean of its albedo texture (averaged on the GPU, read back without a
    stall): Sponza's and Bistro's materials are white with the colour in the texture, and every surface had reflected
    all the light it received, in grey;
  * probe relocation: probes inside geometry or close to a surface move within their cell;
  * a coarser cascade (4 × the spacing) where the fine volume ends, at a sixteenth of the probes per frame and half
    the rays (at an eighth and every ray it had cost City Lab 0.67 ms, as much as the fine volume; now 0.35-0.44 ms in
    City Lab, Sponza and Foliage Lab, `m16_f245`);
  * transparent surfaces take the screen pass's irradiance (of the opaque surface behind them): looking the probes up
    in the transparent shaders had cost Sponza's transparent pass 0.76-0.85 ms, with or without the visibility test
    (`m16_f243`-`m16_f245`); now nothing (`m16_f246`).
* **A path-traced reference** (`debug/raytracing_gi_reference`): per pixel, a path of up to n bounces from the depth
  buffer's surface, accumulated while the camera stands still, in place of the probes' irradiance. Against it, outdoors
  (PBR Lab's ground, lavapipe) DDGI agrees; inside, DDGI is much brighter (Sponza on the RTX, 600 paths per pixel,
  `m16_f243`): in Sponza's arcades the reference
  is close to black where DDGI keeps a tenth of the ambient light (the sky leak) and, where its probes are hidden from a
  point, the ambient light itself. That is the choice made in 0083 (no black interiors, as Unreal's skylight leaking);
  the reference shows its price. A real exposure (histogram, local) is what would let the interiors be dark.

## 3. Lighting units

The first DDGI and reflections lit their hits with the sun π times too bright (the scene shader's Lambert 1/π left out)
and lit metals as diffuse surfaces; a chrome sphere reflected the ground brighter than the ground (lavapipe, 0084).
Fixed in both.

## 4. Open

* **The render thread** builds the scene every frame: about 1 ms in City Lab and Bistro exterior (6 000-7 000
  instances), 0.6 ms more for Foliage Lab's MultiMesh instances. Static instances could keep their records between
  frames; MultiMesh instances could be written on the GPU from their buffers (those written on the GPU are not in the
  scene now, nor are particles).
* **DDGI's interiors are brighter than the reference** (section 2): the sky leak and the ambient light where the probes
  are hidden. Physically right interiors need an exposure that adapts (the "Unreal look" list below).
* The coarser cascade is centred on the camera like the fine one: from City Lab's aerial camera (70 m up) the street is
  below both.
* The rays do not test alpha (leaves are skipped, not cut out); the hits see a material's mean texture colour.
* Sponza's reflections still cost 1.4 ms: its smooth surfaces are traced every pixel, every frame.

## 5. Incidents

* **The laptop hung** on 2026-10-24 at about 07:08 and had to be switched off; the runner's last heartbeat is 07:07:51.
  Nothing of the benchmark's was running then: the last job had ended at 06:46, the runner was paused, its loop rewriting
  one line every 15 s. Since then each job does at most about 15 minutes of GPU work, with a rest before it, and large
  result files are analysed in the cloud rather than on the laptop.
* **The queue runner (v2)** no longer exits on a pause: `PAUSE`, `ABORT` (ends the running job's process tree) and its
  own update are files in `.tools/queue/`, so pausing, stopping a job and updating the runner need nobody at the machine.
  Only its start does (after a restart of the laptop).

## 6. Next

As decided with the user (2026-10-24): the ray tracing is closed here; next, **collisions and physics** (milestone 17).
The non-ray-traced features that would bring the optional image closer to Unreal's, by expected effect, for a later
milestone: a physically based sky atmosphere with aerial perspective (Hillaire), volumetric clouds, histogram and local
auto exposure, a sharper TAA, contact shadows per light, an Unreal-style bloom with lens dirt.
