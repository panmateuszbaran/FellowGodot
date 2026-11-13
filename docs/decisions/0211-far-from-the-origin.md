# 0211 — Far from the origin: double precision

Status: **KEEP** (builds with `precision=double`; no setting) · Branch `fellow/geometry` · Code:
`servers/rendering/renderer_rd/storage_rd/render_scene_data_rd.cpp`, the two scene shaders' emulated double sum
(marked, stock files); `servers/rendering/renderer_rd/effects/fellow_camera_relative.h` and the gas, cloud, atmosphere
and rainbow effects; `modules/fellow_world/fellow_terrain_3d.cpp`; five calls made unambiguous for MSVC · Checks:
`tools/fellow_regression/regression.py far` (each scene near the origin and far from it, by one build).

## Problem

An open world of a AAA game spans 10-100 km; a float's step is 1 mm at 8 km and 8 mm at 100 km, and what the CPU
simulates there (characters, vehicles, physics) shakes. Godot's answer is a build in double precision
(`precision=double`: `real_t` a double on the CPU, the GPU's model and view origins emulated in two floats).
FellowEngine had never been built or run that way; the readiness assessment listed "single-precision coordinates
(Godot's double-precision builds not tested)".

`regression.py far` renders each scene of the image regression suite where it is and again moved far out (instanced
under a node at `--offset`, its camera with it) with the same build, and compares the two pictures. The scenes'
scripts now place their cameras and break their objects relative to their root (the same pictures at the origin).
First runs, 100 km out:

* **Single precision**: characters, vehicles and debris off (traversal and a car: 6.6-7.2 % of the pixels more than
  8 levels apart); the fire's volume drawn in blocks, the clouds in blocks, the rainbow in steps.
* **Double precision**: the characters and the car right — but the water gone (from about 5 km out), the snow cover
  and the rain's wet ground wrong even near the origin, the terrain's collision refused ("Expected PackedFloat64Array"),
  the fire, the clouds and the rainbow in blocks as in single precision, the sky brown and the clouds below the
  camera.

## Change

* **The camera's position, double builds (a stock bug)**: the scene data kept the camera's position negated in the
  inverse view matrix's translation, the operand the scene shaders' emulated double sum (`double_add_vec3`) adds to the
  model's origin. Every other reader of that matrix — a shader's `INV_VIEW_MATRIX` and `CAMERA_POSITION_WORLD`, stock's
  fog height, FellowEngine's world positions in the scene shader (the rain's wetness and shelter, the snow cover, the
  clouds' shadow) — got the camera mirrored through the origin. The position is stored as it is; the two scene shaders
  (Forward+, Mobile) subtract it, both parts negated (exact). Single precision: untouched.
* **The terrain's collision heights** are `real_t` (a `PackedFloat64Array` in double builds, as the physics servers
  want).
* **The screen effects relative to the camera** (`fellow_camera_relative.h`): the gas volumes' trace, the clouds,
  the atmosphere's aerial perspective and the rainbow reconstructed world positions from depth (a matrix with the
  camera's 100 km in it) and subtracted the camera on the GPU, keeping millimetres of a float's 24 bits. The CPU (in
  `real_t`) now makes the inverse view projection with the camera at the origin, and takes the gas grids, the fire
  lights, the view for the shadow cascades, the clouds' reprojection and the lightning's flash there; the GPU sees
  positions near zero.
* **The clouds' planet** (single precision too): the trace put the camera at (x, R + h, z) km on a planet of radius
  R, so 100 km out it stood 1.7 km above the ground — in or over the cloud layer. The camera stays on top of the
  planet and the clouds' field moves under it (its x and z an offset of the noise); the clouds' shadow map and the
  showers' probe the same about their own centres.
* **MSVC**: five braced lists narrowing a `real_t` to a float and three calls ambiguous between float and double
  overloads (GCC only warned): explicit conversions (the same values and overloads in single precision).

## Checks

`regression.py far --offset 103680,0,103680` (24 × 4 320 m, where the water's wave periods — 160, 27 and 5 m — and
the rain's 1.5 m ripples repeat), the release templates on lavapipe: the share of pixels more than 8 levels from the
near picture, and the largest difference:

| scene | single precision | double precision |
|---|---|---|
| lights, terrain (three) | 0.02-0.03 %, up to 184 | **identical** (terrain VT: 0.001 %, 20) |
| fire (a gas volume) | 0.32 %, 188 | **0.000 %, 1** |
| vehicle (two cars driving) | 7.2 %, 218 | 0.07 %, 37 |
| traversal, climbing, ledges, cape | 1.4-6.6 %, up to 219 | 0.09-0.22 %, up to 206 |
| face | 0.12 %, 198 | 0.000 %, 8 |
| crowd, geometry (HLOD, MultiMesh) | 0.87 %, 2.2 % | 0.33 %, 0.89 % |
| cloth, destruction, structure | 1.1-3.9 % | 0.2-2.6 % (the debris's fall diverging: chaotic, not drawn wrong) |
| water, rain, snow | 2.0 %, 11.5 %, 0.09 % | 2.1 %, 11.5 %, 0.09 % (the rain's streaks and the gusts are the world's, not repeating) |
| sky (clouds) | 75 % | 73 % (other clouds overhead 147 km away; drawn as near the origin) |
| raytracing | 0.76 % | 0.23 % |

Before the changes, 100 km out in double precision: the water missing, the fire 2.6 % (in blocks), the sky's clouds in
blocks over a brown sky, the terrain without collision. The laptop (job f406, the RTX 5070 Ti, MSVC): the same
picture — the double build's fire 0.000 %, terrain identical, the vehicle 0.07 %, single precision's vehicle 7.1 %.

At the origin (single precision, the image regression suite): the rain's, the fire's and the sky's pictures within 1-4
levels of their goldens (the camera-relative arithmetic's rounding); the cape scene's walker turned with
`Basis.looking_at()` (`look_at()` compares positions relative to their size and failed far out), its cloth within
146 levels on 0.07 % of the pixels. New goldens for those (six pictures); the other 29 exact, the release template
the editor build's.

## Cost

The laptop (job f407, release templates, 1920 × 1080, three interleaved runs each): Frontier Lab 3.700 → 3.730 ms a
frame (+0.8 %, within its runs' spread), City Lab 2.148 → 2.230 ms (+3.8 %: the render thread), Crowd Lab 4.374 →
4.954 ms (+13 %: 256 characters' animation on the CPU); the GPU's time the same. A double build's first start compiles
its own shader variants (Frontier Lab 19.2 s instead of 0.6, once; the next starts 0.5-0.6 s).

## Decision

Keep: FellowEngine builds, runs and draws right in double precision, for worlds beyond about 10 km (single precision
stays the default build: 4-13 % less CPU).

## Rejected / not done

* **A floating origin for single-precision builds** (the world moved back under the camera): Godot's way to large
  worlds is the double build; a rebasing service would touch every node that keeps world positions.
* **What stays in floats on the GPU**: GPU particles, the ray-tracing acceleration structures, MultiMesh instances and
  FellowEngine's per-node GPU data — within 8 mm at 100 km (the fire light's position, the rain's maps' origins).
* **The Mobile and Compatibility renderers far out**: the Mobile scene shader's sum fixed with Forward+'s, its far
  pictures not measured; Compatibility has no double emulation.
