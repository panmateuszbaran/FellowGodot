# 0272 — Instances beyond their range left out of the far shadow cascades

Status: **KEEP** (on; on the RTX the same frame time, milestone 151) · Branch `fellow/geometry` · Code: `servers/rendering/renderer_scene_cull.cpp`
(the directional cascades' caster loop), `fellow_settings.{h,cpp}` (`shadows/instance_range_cascades`, the statistic
`shadow_range_skips`) · Checks: `bench/terrain_check.gd` (new options `--shadow_distance`, `--foliage_kinds`,
`--grass_range`), the image regression suite, on lavapipe.

## Problem

An instance range (`fellow_instance_set_instance_range()`: the terrain's foliage, decision 0172 — grass to its
`range`, trees to `impostor_distance` and their impostors from there to `range`; HLOD's instanced proxies, decision
0122) is applied per instance by the camera's MultiMesh culling, and not at all in the shadow passes: decision 0172 left
it out because the directional shadow cache keeps a cascade's casters independent of the camera. So every instance of
a foliage cell casts a shadow in every cascade the cell's box reaches — Frontier Lab's grass (range 80 m) in the
cascades out to the sun's 400 m, Slice Lab's (90 m) to 300 m: instances the camera never draws, drawn into the far
shadow maps.

## Change

A cascade that begins (its near split) beyond an instance's range end (plus its end margin) leaves the instance out:
every point the cascade covers is at least that far from the camera, so none of the instance's instances is drawn
there and none of the cascade's receivers sees one. Per cascade, as HLOD's hand-over between members and proxies is:
what a cascade draws does not depend on where the camera is, and the shadow cache keeps it. The cascade straddling the
range still draws them all (a cascade's instances culled by distance from the camera would make the cached ones depend
on it). `shadows/instance_range_cascades` (on); FellowEngine's statistics count the instances left out.

## Checks

* `terrain_check.gd --foliage=1 --shadow_distance=400` on lavapipe (grass to 60 m, rocks to 200 m, trees to 500 m;
  the sun's cascades split at 40, 80 and 200 m): 4 710 instance × cascade pairs left out over the run; with grass alone
  (to 25 m) 2 310, and the four views' pictures identical to the build before (the grass's shadows beyond its range too
  small for the far cascades' texels to show; the same with the setting off, which leaves none out). With all three
  kinds the check is not deterministic from run to run (the old build against itself: up to 66 levels on the trees'
  edges), and the new build against the old differs on the same edges (up to 159).
* The image regression suite: 48 of 48 identical (none of its scenes has a cascade beyond an instance range).
* The RTX (job `m147_f511`, milestone 151; 1920 × 1080, on and off interleaved, 3 runs each, the heat soak):

  | | frame, on / off | GPU, on / off | p99, on / off |
  |---|---:|---:|---:|
  | Frontier Lab (grass to 80 m, the sun's shadows to 400 m) | 3.661 / 3.651 ms | 3.222 / 3.200 ms | 5.01 / 4.97 ms |
  | Slice Lab (grass to 90 m, shadows to 300 m) | 4.262 / 4.269 ms | 3.215 / 3.227 ms | 5.53 / 5.67 ms |

  Each difference smaller than its runs' spread (0.02-0.06 ms): the far cascades are cached and drawn again rarely, and
  the grass they no longer draw is a small part of them when they are.

## Decision

KEEP, on: the shadow passes no longer draw instances the camera never draws, with nothing changed in what a cascade's
receivers can see; the cache unaffected. On the RTX its saving is below what three runs can tell in either scene;
it costs nothing, and a far cascade drawn again (the sun moving, an uncached cascade) has less to draw.

## Rejected / not done

* **The range per instance in the shadow passes' MultiMesh culling** (the straddling cascade too): the cached cascades
  would then depend on the camera's position, and the cache would hold instances' shadows by where the camera was when
  it was built.
