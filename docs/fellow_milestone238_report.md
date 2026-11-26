# FellowEngine — milestone 238 report (a budget for the lights' shadows)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0380. No GPU work: checked on lavapipe.

## 0. Result

The second half of the shadowed lights' plan (after milestone 231's static cache): a budget per frame. With
`shadows/positional_update_budget` N, at most N omni and spot lights' shadows are drawn again in a render; when more
want it, the ones that must (a new place in the atlas, or 8 renders waited) go first, then the largest on screen
times the renders waited, and the others keep an earlier frame's shadow. On 16 lights with balls moving among them,
a budget of 4 draws a quarter of the shadows; the balls' shadows from the waiting lights lag a frame or three (45.6
dB against no budget). Off by default.

## 1. Changes

* `servers/rendering/renderer_scene_cull.{h,cpp}`: the positional shadows' redraws decided after the loop under the
  budget; each light's renders waited.
* `servers/rendering/storage/light_storage.h`, `renderer_rd/storage_rd/light_storage.{h,cpp}`: the atlas tells whether
  its last update gave the light a new place (`fellow_shadow_atlas_last_reallocated`).
* `servers/rendering/fellow_positional_shadow_cache.h`, `fellow_settings.cpp`: the two settings; counters
  `shadow_pos_budget_drawn`, `_deferred`, `_forced`, `_new_place`.
* Regression: `pos_budget_all` (bit for bit `base`), `pos_budget` (its golden).
* Docs: decision 0380, `FELLOWENGINE_OPTIONS.md`, `ProjectSettings.xml`, the regression README, the changelog.

## 2. Measurements (lavapipe, 16 lights, 8 moving balls, 90 frames)

| budget | shadows drawn | image against no budget |
|---|---:|---|
| 0 | 1 440 | — |
| 8 | 728 | 4 of 6 frames bit for bit, 52-57 dB |
| 4 | 372 | 45.6 dB on average |

## 3. Open

* On the RTX: the rooftops at night with a budget; the frame time saved.
* With task #189 closed, the three gaps' engine work (shadows 231/238, people 232-236, geometry 237) is in; their GPU
  measurements wait for the owner's go.
