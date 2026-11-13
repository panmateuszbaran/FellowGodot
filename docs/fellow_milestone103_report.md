# FellowEngine — milestone 103 report (far from the origin: double precision)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj"; the laptop's jobs f404-f407 done. Branch
`fellow/geometry`. Decision record: `docs/decisions/0211-far-from-the-origin.md`.

## 0. Result

* **FellowEngine in double precision** (`precision=double`): builds with GCC and MSVC, runs and draws right 100 km
  from the origin — terrain and lights identical to the near picture, the fire within one level, characters and cars
  within 0.1-0.2 % of the pixels (single precision there: 6.6-7.2 %).
* **Found and fixed**: double builds kept the camera's position negated for shaders (a stock bug: the water vanished,
  the snow and the rain's wetness were wrong even near the origin); the terrain's collision refused in double builds;
  the gas volumes', clouds', atmosphere's and rainbow's traces lost precision far out (now relative to the camera, in
  single precision too); the clouds' camera rose off the planet with distance (1.7 km at 100 km).
* **A check for it**: `regression.py far` (each scene near and far by one build).
* **Cost**: 0.8-3.8 % of the frame in the open world and the city, 13 % in a crowd of 256 characters (CPU).

## 1. Development

`servers/rendering/renderer_rd/storage_rd/render_scene_data_rd.cpp`, `scene_forward_clustered.glsl`,
`scene_forward_mobile.glsl` (marked); `servers/rendering/renderer_rd/effects/fellow_camera_relative.h` (new),
`fellow_gas.cpp`, `fellow_clouds.{h,cpp}`, `fellow_atmosphere.cpp`, `fellow_rainbow.cpp` and their shaders;
`modules/fellow_world/fellow_terrain_3d.cpp`; MSVC's conversions in `fellow_face_animator.cpp`, `fellow_motion_math.h`,
`fellow_burnable_3d.cpp`, `fellow_lightning_3d.cpp`, `fellow_cloth_3d.cpp`, `fellow_terrain_3d.cpp`;
`tools/fellow_regression/regression.py` (`far`, `--importer`), the regression scenes' scripts (cameras, breaks and the
cape's walker relative to their root), six new goldens.

## 2. Verification

* Decision 0211's table (lavapipe, both templates, 103.68 km) and the laptop's (job f406, the RTX): the same picture.
* The image regression suite: 29 scenes exact, six within 1-4 levels (rain, fire, sky in two groups; the camera-relative
  rounding) or the cape's script change — new goldens; the release template the editor build's.
* Found on the way: Godot's `look_at()` fails 100 km out (it compares positions relative to their size); the cape
  scene turns its walker with `Basis.looking_at()`.

## 3. Measurements

Job f407 (the laptop, 1920 × 1080, single against double, interleaved): Frontier Lab 3.700 → 3.730 ms, City Lab 2.148
→ 2.230 ms, Crowd Lab 4.374 → 4.954 ms; the GPU the same; a double build's first start compiles its own shaders
(19.2 s once).

## 4. Next

A floating origin for single-precision builds (not planned: the double build is Godot's way); GPU particles and the
ray-tracing structures far out.
