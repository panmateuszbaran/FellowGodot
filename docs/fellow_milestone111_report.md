# FellowEngine — milestone 111 report (FSR 3.1 at 1440p and 4K; each source's own room; the broadphase's pairs)

Status: **FINAL** (2026-11-14): the owner said "kontynuuj"; the laptop's jobs f427-f431 done. Branch
`fellow/geometry`. A measurement the readiness assessment listed as missing (FSR 3.1 at 1440p and 4K output), the
acoustics' second room model (decision 0218), GodotPhysics3D's broadphase at thousands of bodies (decision 0219), and
the editor's car with doors.

## 0. Result

* **FSR 3.1 at 1440p and 4K** (the laptop's RTX 5070 Ti Laptop, milestone 110's build, autotune off, two runs each):
  at half the pixels in each direction (50 %) the frame is 44-48 % shorter than TAA's at native resolution, at a third
  (33 %) 55-61 %; the upscaler costs 0.92-0.98 ms at 1440p and 2.31-2.36 ms at 4K at 50 % (section 1). Its half
  precision gains nothing on this GPU.
* **Each source's own room** (decision 0218): `FellowAcoustics3D.source_rooms` sends a source in another room than the
  listener's to a reverb of its own room's (section 2).
* **The broadphase's existing pairs** (decision 0219): GodotPhysics3D's broadphase drops on the worker threads the
  culls' hits that are pairs already: the pile −9.6 % at 2 000 bodies, −13.3 % at 5 000, −14.0 % at 10 000 (its
  broadphase 2.2 × faster at 2 000 and 5 000), the same hashes as stock's (section 3).
* **Thirty minutes of the slice** with the milestone's build on the RTX: nothing growing, no hitch from compiling
  (section 5).
* **Add Car's doors** (section 4).

## 1. FSR 3.1 at 1440p and 4K

FellowBench's `--render_size=WxH` (new: the window's content scaled to that size, whatever the window's own; Windows
clamps a 3840 × 2160 window on the laptop's 2560 × 1600 screen) renders Bistro exterior and Sponza at 2560 × 1440 and
3840 × 2160; `benchmarks/run_benchmarks.py --profile` (per-pass GPU timestamps), the release template of milestone 110,
`rendering/fellow/autotune` off, two alternating runs per variant (jobs f427-f428). The frame (ms, the GPU's) against
TAA at native resolution, and the FSR 3.1 pass:

**2560 × 1440**

| scale | Bistro frame | vs TAA | FSR pass | Sponza frame | vs TAA | FSR pass |
|---|---:|---:|---:|---:|---:|---:|
| TAA (native) | 7.111 | | 0.478 (TAA) | 9.235 | | 0.427 (TAA) |
| 33 % | 3.140 | −55.8 % | 0.751 | 4.109 | −55.5 % | 0.816 |
| 50 % | 3.785 | −46.8 % | 0.917 | 5.159 | −44.1 % | 0.977 |
| 59 % | 4.324 | −39.2 % | 1.045 | 5.926 | −35.8 % | 1.077 |
| 67 % | 4.839 | −31.9 % | 1.161 | 6.656 | −27.9 % | 1.216 |
| 100 % (native AA) | 7.745 | +8.9 % | 1.772 | 10.489 | +13.6 % | 1.784 |
| 50 %, fp32 | 3.793 | | 0.934 | 5.185 | | 0.974 |

**3840 × 2160**

| scale | Bistro frame | vs TAA | FSR pass | Sponza frame | vs TAA | FSR pass |
|---|---:|---:|---:|---:|---:|---:|
| TAA (native) | 13.496 | | 0.983 (TAA) | 18.433 | | 1.042 (TAA) |
| 33 % | 5.586 | −58.6 % | 1.907 | 7.195 | −61.0 % | 1.967 |
| 50 % | 7.240 | −46.4 % | 2.357 | 9.569 | −48.1 % | 2.310 |
| 59 % | 8.453 | −37.4 % | 2.536 | 11.230 | −39.1 % | 2.503 |
| 67 % | 9.710 | −28.1 % | 2.708 | 12.634 | −31.5 % | 2.730 |
| 100 % (native AA) | 16.545 | +22.6 % | 4.002 | 21.166 | +14.8 % | 4.099 |
| 50 %, fp32 | 7.254 | | 2.358 | 9.616 | | 2.317 |

* The upscaler's cost grows with the output's pixels (1440p → 4K: 2.25 × the pixels, 2.4-2.6 × the pass) and little
  with the input's (33 % → 67 % at 4K: 1.9 → 2.7 ms); at 4K and 50 % it is 2.3 ms of a 7.2-9.6 ms frame, twice TAA's
  pass at native resolution, and saves 6.3-8.9 ms.
* **Half precision** (`rendering/fellow/fsr3/half_precision`, on): the same pass time as fp32 within 0.02 ms — NVIDIA's
  GPUs run fp16 arithmetic at fp32's rate; decision 0148 kept it for AMD's and Intel's, where it is twice the rate (not
  measured: no such GPU).
* Bistro's TAA frame at 1440p varied by 9 % run to run (its coefficient of variation); the others under 3 %.

## 2. Each source's own room (decision 0218)

`FellowAcoustics3D.source_rooms` (0-8, 0 by default; the FellowEngine menu's "Add Acoustics" sets 2): that many more
reverbs, each driven by the room of the sources sent to it. A source's room is measured from the source (decision
0186's rays: how closed, how large, the nearest wall), one source a frame in turn; a source whose room is like the
listener's, or that has none of its own (closed over by less than half), keeps the listener's reverb; the others share
the room reverbs by alike rooms. `acoustics_check.gd`'s three new lanes (a hall 20 × 20 × 8 m with a doorway, a closed
closet 4 × 4 × 3 m, a source in each and one in the open) and `--listen=1`:

| | the listener in the open | the listener in the hall | `source_rooms` 0 |
|---|---|---|---|
| the hall's source | FellowReverbRoom1: RT60 1.35 s, sent 0.5 | the listener's (1.25 s) | the listener's |
| the closet's source | FellowReverbRoom2: 0.29 s, sent 1.0 | Room2: 0.29 s | the listener's |
| the open one | the listener's | the listener's | the listener's |

Heard from the open (Movie Maker's mix), the hall's source has its hall's tail 15.2 dB under the direct sound; without
source rooms the tail is 10.5 dB weaker. The cloud and the laptop (job f429) the same values with GodotPhysics3D and
Jolt, the editor build and the release template the same. The cost (`--many=64`: 64 sources among walls, three
alternating runs on the laptop): 116-133 µs a frame without, 135-137 µs with two room reverbs; one source's room
measured a frame (32 rays).

## 3. The broadphase's existing pairs (decision 0219)

Decision 0087 runs the broadphase's culls on the worker threads; the loop after them looked up every hit in the moved
object's pair list, and in a pile almost every hit is a pair that exists. Now each worker drops the hits whose pair
exists and will still exist when the loop reaches the object (only an earlier moved partner's leavers can remove it,
and they keep it when that partner's expanded box still meets this object's — tested on the worker on the same boxes),
so the loop looks up only the hits that may be new pairs; the pairs are made and freed in stock's order.

The laptop (job f431, 24 threads, 600 ticks, two alternating runs; ms a tick):

| bodies | stock (the frame) | without | with the prefilter | | the broadphase |
|---:|---:|---:|---:|---:|---|
| 2 000 | 28.1 | 14.27 | 12.90 | −9.6 % | 2.73 → 1.25 ms |
| 5 000 | 121.0 | 41.38 | 35.88 | −13.3 % | 8.30 → 3.78 ms |
| 10 000 | 421.5 | 149.64 | 128.70 | −14.0 % | 69.5 → 50.2 ms |

Every hash equal to stock's (the pile at the three sizes, and the five cases: `pile` 12.25 → 10.18 ms, `areas` 2.43 →
2.23 ms, `stack` 1.08 → 1.06 ms, `chars` and `rays` the same). FellowEngine's GodotPhysics3D is now 3.3 × stock's at 5 000
and 10 000 bodies (milestone 110: × 2.7). What is left of the broadphase at 10 000 bodies is the pairs made and freed
as the tall pile falls through itself (about 8 000 of each a tick in its first four seconds on the cloud).

## 4. Add Car's doors

The FellowEngine menu's "Add Car" (milestone 110) gives the car two doors hinged at their front edges
(`fellow_hinge_axis` up on the left, down on the right, `fellow_hinge_limits` (0, 70): both open outwards) and lists
them among the detachable parts with the bumper. `vehicle_check.gd`'s door lane runs for a right door too
(`door_right`: hit at its rear end, the car turning left): 73.6° at 4 s (the left door 73.3°). The editor check: the
preset's 17 nodes added without a configuration warning, owned by the scene, undone.

## 5. Thirty minutes of the slice

Slice Lab's soak (milestone 55's vertical slice: every system in one valley, the weather cycling) for 30 minutes at
1080p on the RTX with the milestone's release template (job f430): 239 fps (4.18 ms; p99 6.85 ms, p99.9 7.71 ms, the
largest frame 19.8 ms); 6 hitches (frames longer than twice the median and than the median and 10 ms: 0.21 a
minute), none of them compiling a pipeline; objects, nodes and resources the same from start to end (2 789, 1 184, 8);
video memory 404.6 MiB at the start, 405.8 at the end (a peak of 464). The pipelines compiled after the warm-up: 86
specializations.

## 6. Next

GodotPhysics3D's pairs made and freed at 10 000 bodies (an allocation and two hash-map insertions each); Jolt's 8 ms
of node updates at 10 000 bodies (memory-bound: about 15 cache lines a body across 7 objects — the cloud showed
prefetching ahead gains 15 % at best); FSR 3.1's pass at 4K (2.3 ms at 50 %).
