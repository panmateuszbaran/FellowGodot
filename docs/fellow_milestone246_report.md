# FellowEngine — milestone 246 report (the sun's cascades fitted to what the view shows)

Status: **FINAL** (2026-11-27). Branch `fellow/geometry`. Decision 0388. No GPU (the owner: "continue without GPU"):
checked on lavapipe.

## 0. Result

The next medium gap, sun shadows: the directional light's cascades can now be fitted to the depths the view actually
shows — sample distribution shadow maps — instead of always spanning the shadow's whole max distance. Where the view
ends short of it (courtyards, streets' ends, rooms, looking down) the same shadow atlas gives much sharper shadows:
against a 16384 atlas a courtyard goes from 25.5 to 29.2 dB, looking down from 29.8 to 33.0 dB. An option, off.

## 1. Changes

* `servers/rendering/fellow_depth_fit.h` (new): the setting, the store of views' ranges (held over recent frames),
  counters (`shadows_depth_fit_renders`, `_near_mm`, `_far_mm`).
* `renderer_rd/shaders/effects/fellow_depth_range.glsl`, `renderer_rd/effects/fellow_depth_range.{h,cpp}` (new): the
  depth buffer reduced to its nearest and farthest surface, read back asynchronously.
* `render_forward_clustered.{h,cpp}`: the reduction after the depth pre-pass; the readback with the camera it saw.
* `renderer_scene_cull.{h,cpp}`: the cascades' range from the held range (widened by the camera's movement, stock after
  a cut); the light culler prepared after the distances are known.
* `rendering_light_culler.{h,cpp}`: `fellow_set_directional_distances` (the cascades' real slices for its caster
  culling).
* Docs: decision 0388, `FELLOWENGINE_OPTIONS.md`, `ProjectSettings.xml`, the changelog,
  `docs/showcase/sun_shadows_m246.jpg`.

## 2. Checks

* Four views against a 16384 atlas (the decision's table); a camera cut (the first frame stock, no frame without its
  shadows); the depth conversion (a wall at 19.5 m read as 19.50 m).
* A bug found on the way: Godot's light culler culled each cascade's casters for the slice the light's settings imply,
  so narrowed cascades lost theirs (a courtyard's shadow came out lit); it now takes the real slices.
* Regression suite: every scene exact (off by default; the culler's preparation moved without changing it).

## 3. Open

* On the RTX: the pass's cost (expected: a few hundredths of a millisecond) and whether to turn it on in the showcase.
* SDSM's full form: the splits placed by the depths' distribution, not only their range; per-cascade tight bounds.
