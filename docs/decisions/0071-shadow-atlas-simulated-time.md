# 0071 — The positional shadow atlas's reallocation tolerance in simulated time at a fixed frame rate

Status: **KEEP** (on from `fellow160`) · Branch `fellow/geometry` · Setting
`rendering/fellow/shadows/atlas_simulated_time` (on) · Code: `light_storage.cpp` (`shadow_atlas_update_light`),
`fellow_settings.*` (`shadow_atlas_tick_msec`), `core/config/engine.h`, `main/main.cpp`.

## Problem

After 0067 two runs of FellowEngine give the same warm sequences bit for bit in Bistro interior, PBR Lab and Sponza, but
not in Lights Lab (`m13_f193f_floor_0067`: 102.5 dB, max |d| 3; with 0070 still max |d| 1-3, `m13_f200`). Two runs
against each other with one part of the scene off (`m14_f201`, `m14_f202`, three runs each, compared in pairs):

| off | pairs | max \|d\| |
|---|---|---|
| nothing | 3 | 1-2 |
| async compute | 3 | 1-3 |
| volumetric fog, reflection probes, decals | 1 each | 1-2 |
| **positional shadows** | 3 | **0 (bit for bit)** |
| **SSAO** | 3 | **0 (bit for bit)** |

Not the second queue, not memory no pass wrote (0070 on). Stock's positional shadow atlas moves a light to a quadrant
of another size (its shadow's resolution follows its size on screen) only if 500 ms have passed since its last
allocation (`shadow_atlas_realloc_tolerance_msec`), and measures them with `OS::get_ticks_msec()`, the wall clock. The
benchmark, like Movie Maker mode, runs the main loop at a fixed frame rate (`--fixed-fps 60`: every frame advances the
scene by 1/60 s) while the frames are drawn as fast as the GPU allows (Lights Lab ~430 per second): 500 ms are ~215
frames of one run and a few more or fewer of the next, so a light changes resolution at a frame that depends on how fast
the frames before it were drawn. Without SSAO the frames are faster and the decisions fell with more margin.

## Change

`shadows/atlas_simulated_time`: when the main loop simulates a fixed frame rate (`Engine` now keeps it,
`fellow_get_fixed_fps()`), the tolerance counts the simulated time: the renderer's frame number × 1000 / the rate. At a
free frame rate the wall clock, as stock. The decisions are the same as stock's where the frames are drawn at the
simulated rate (a game at 60 fps with `--fixed-fps 60`); where they are drawn faster, a light changes resolution after
500 ms of the scene's time instead of 500 ms of the machine's.

## Verification

RTX (`m14_f203_floor_0071`, `fellow160`, autotune off, `m13_f180`'s warm sequences at 1920 × 1080):

| | Bistro interior | PBR Lab | Sponza | Lights Lab |
|---|---|---|---|---|
| three runs with it, in pairs | bit for bit (3 of 3) | bit for bit (3 of 3) | bit for bit (3 of 3) | **bit for bit (3 of 3)** |
| two runs without it | bit for bit | bit for bit | bit for bit | max \|d\| 3 |
| without against with | bit for bit | bit for bit | bit for bit | max \|d\| 3 (95.0 dB worst frame) |
| `fellow158` (milestone 13) against `fellow160` | bit for bit | bit for bit | bit for bit | max \|d\| 2 |

lavapipe (480 × 270, the frames drawn far slower than 60 per second, so the wall clock lets a light change size
after a few frames, the simulated time after 30), off against on: captures after 60 frames of settle PBR Lab and City
Lab bit for bit, Lights Lab 2 pixels of 1 level (a shadow's size changed at another frame of the settle, TAA's
history keeps a trace); Lights Lab's walk (60 frames) at most 1 level.

With 0067, 0070 and 0071 two runs of FellowEngine render the same frames bit for bit in all four scenes; stock's two
runs differ at 69-98 dB (`m13_f193f_floor_0067`). What changes against milestone 13's build is the run-dependent part
only (Lights Lab's few pixels).

## Decision

**KEEP, on.** At a fixed frame rate the shadow atlas's decisions follow the scene's time, as every other part of
the frame does; at a free frame rate nothing changes.
