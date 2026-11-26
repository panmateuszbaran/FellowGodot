# FellowEngine — milestone 231 report (omni and spot lights' shadows: the static casters cached)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0373. No GPU work (the owner's request): checked on
lavapipe; the GPU's gain to be measured on the RTX when allowed.

## 0. Result

The first of the three gaps named after milestone 230 (`docs/fellow_gap_analysis_m230.md`): the shadowed lights. An
omni or spot light's shadow had to be drawn again whole — six cube faces with every caster in the light's range —
whenever anything in that range moved. Now (`rendering/fellow/shadows/positional_cache`, on) the static casters come
from a copy of each face made when they were last drawn, and only the moving ones are drawn over it, as the sun's
cascades have done since decision 0004.

* **Exact**: the static casters are drawn exactly as stock draws them (a cube face into its cache from the same
  origin; a spot map or a paraboloid into its own atlas rect, then copied out), so the shadow maps are stock's. The
  regression suite's new `pos_cache` scene — three kinds of light, a ball circling between them, a pillar hidden and a
  crate moved while cached — renders the same picture with the cache off, on, and made again at every draw; the other
  45 scenes are unchanged; no Vulkan synchronization message.
* **Draws**: in a tavern of 220 static meshes with six walkers under four shadowed lights, the shadow passes' draw
  calls fall from 803 to 333 a frame (−59 %).
* A light that moves is drawn the stock way (until still for `directional_cache_static_frames`); a caster that becomes
  static is taken into the copy at the light's next draw after `static_frames`; the copy is made again when the camera
  has moved 4 m (`positional_cache_camera_travel`: the casters' mesh LOD follows the camera).

## 1. Code

| file | change |
|---|---|
| `servers/rendering/renderer_scene_cull.*` | `_fellow_pos_shadow_cache_prepare` (what the light's faces were made for, invalidation events, promotion, camera travel, a moving light), `_fellow_pos_shadow_collect_static` (the cached casters, before the tighter caster culling), the static casters skipped in the faces' lists, `_fellow_shadow_events_gc` (shared with the directional cache; a cache that missed forgotten events is made again) |
| `servers/rendering/fellow_positional_shadow_cache.h` (new) | the settings and counters, outside `fellow_settings.h` (included by the whole engine) |
| `renderer_rd/effects/fellow_shadow_cache.*`, `fellow_shadow_cache_copy.glsl` | six faces per cache, where a face's content was drawn, the copy's upper clamp (1 for a positional face) |
| `render_forward_clustered.cpp` | the positional faces' cache path in `_render_shadow_pass`, `_fellow_push_shadow_copy` |
| `tools/fellow_regression/` | the `pos_cache` scene, the groups `pos_cache_off`, `_on`, `_rebuild` |
| `showcase/tools/climb_look.gd` | `--shadow_info=1`: the shadow passes' draws and primitives a frame, the cache's counters |

## 2. Open

* The GPU's time on the RTX (the osteria: its chandelier's cube with the people moving under it) — the osteria on
  lavapipe outgrows the cloud machine's 7 GB.
* A fire's light (FellowGasVolume3D) follows the flames' centre and range every frame, so its shadow stays stock's;
  moving it only beyond a small step would let its static casters be cached (a change of the look: to measure).
* Next for the lights (task: the per-frame budget): shadow resolution from the light's size on screen, rarer updates of
  far lights.
