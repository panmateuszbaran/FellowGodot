# 0250 — Every head's hair stepped on the worker threads at once

Status: **KEEP** (always: no setting) · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_hair_3d.{h,cpp}`
(`_prepare()`, `_simulate()`, `_group_process()`, `get_global_stats()`) · Checks:
`benchmarks/project/bench/hair_crowd_check.gd`, `bench/hair_check.gd` and `bench/hair_lod_check.gd` (the same results),
the regression scene `hair`.

## Problem

A head's hair took 0.3 ms of the main thread a frame (decision 0248): a dozen characters near the camera, 4 ms. Cloth
steps every cloth of a frame on the worker threads at once (decision 0181); hair stepped each node in its own process
callback, one after another, on the main thread.

## Change

The first hair node processed in a frame steps them all: on the main thread each node prepares what its step reads
from the scene — its level of detail, whether it is near enough to simulate, its global transform, gravity, the wind at
each guide's middle, its colliders' shapes — then the steps run on the worker threads at once (WorkerThreadPool's group
task, one node a task; a single node on the main thread), then each node sends its guides and strands to the GPU on
the main thread. `FellowHair3D.get_global_stats()`: the group's main-thread time (`group_usec`), the steps' time summed
over the nodes (`simulate_usec`), the uploads, the node-frames stepped.

## Verification

`bench/hair_crowd_check.gd` (headless, the cloud's two cores; 24 heads of 160 guides and 2 000 strands, each turning,
240 frames): every head stepped every frame, the lengths exact (0 %).

| | the group, main thread | the steps, summed | uploads | steps ÷ (group − uploads) |
|---|---:|---:|---:|---:|
| release template | 3.52 ms | 5.82 ms | 0.47 ms | 1.90 |
| editor build | 3.70 ms | 6.16 ms | 0.47 ms | 1.91 |

`bench/hair_check.gd` and `bench/hair_lod_check.gd`: every figure as before (stretch 0, 0.110 m from the head's centre,
the tips 0.231 m behind in the turn, 3.6 cm in the wind; the strands drawn at each distance); the regression scene
`hair` the same as its golden from both builds.

**On the laptop** (milestone 131, job f464: the release template on its 24 threads, headless, twice): the group 0.89 /
0.93 ms a frame on the main thread, the steps 5.20 / 5.36 ms summed, the uploads 0.40 ms — the steps 10.4 / 10.3 times
faster than one after another (the uploads, on the main thread, now a little under half of what the main thread pays).

## Decision

Keep: the hair of every head near the camera stepped in parallel; the main thread pays the preparing and the uploads.
