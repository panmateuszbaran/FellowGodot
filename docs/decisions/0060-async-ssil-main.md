# 0060 — SSIL on the main queue beside the async SSAO and SSR

Status: **KEEP** (`fellow145`-`fellow147`; default 3 from `fellow147`) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/async_ss_split` (runtime: 0 stock's layout, 1 SSIL on the main queue, 2 SSIL and SSR on the
main queue, 3 SSIL on the main queue and SSR where autotune measures it faster, the default) · Code:
`render_forward_clustered.*` (`_fellow_process_ss_effects` parts, `_pre_opaque_render`), `effects/ss_effects.*`,
`shaders/effects/ssao.glsl`, `ssil.glsl` (the normal buffer sampled), `rendering_device_graph.cpp` (the async conflict
check; driver callbacks' stages), `fellow_autotune.*` (`TUNE_SSR_MAIN`), `fellow_settings.*`.

## Problem

With async compute (0015) the screen-space effects run on the second queue (depth preparation, SSAO, SSIL, SSR, their
blurs) while the main queue renders the shadows, the light clusters and the fog; the opaque pass waits for both. In
Bistro interior the main queue's part is short (no shadow casters in view, no fog) and the region in which it waits
takes 1.06 ms of a 3.11 ms frame (`m11_f167_v4_ab`): the effects' chain is the critical path. Each effect is a chain of
dependent dispatches with barriers between them (gather, blur, interleave); two independent chains on two queues can
fill each other's gaps.

## Change

With the setting: the depth preparation is recorded before the async part (with the shared images' layouts set there:
a no-op driver callback with their usages), SSAO and SSR go to the second queue, and SSIL is recorded on the main queue
first in its part, beside them. The same passes with the same inputs.

Two things made this possible:

* SSAO and SSIL read the normal-roughness buffer as a storage image and SSR samples it, so one queue would change its
  layout while the other reads it. They now sample it too (`texelFetch`, and zero outside the image, which `imageLoad`
  gives): the same values (lavapipe, City Lab with SSIL and SSR: imageLoad against texelFetch 0 differing pixels), and
  no layout change between the three in any configuration.
* The async conflict check (0015) counted a resource as written by the async part only for writing usages. The render
  graph records a texture's layout transition where it is first used: in the async part the main queue would not wait
  for it. A texture's first usage change in the async part now counts as a write (`FELLOW_SEGMENT_LOG` names the
  conflicts). The default configuration has no such conflict (lavapipe City Lab: 0 in 91 frames).

With mode 2 SSR's passes are recorded on the main queue too, after SSIL; mode 3 lets autotune (0020) choose between 1
and 2 by the frame's GPU time (tunable `TUNE_SSR_MAIN`, while the effects run split with SSIL and SSR; it starts with
SSR on the main queue).

**The layouts set before the async part (fixed in `fellow147`).** The no-op driver callbacks carry the images' usages,
but a callback command had no pipeline stages: the barrier that changed the images' layouts before it had
BOTTOM_OF_PIPE as its destination stage, which no later command waits on, and the main queue's SSIL, reading the
images with the same usage, got no barrier of its own. Vulkan validation reported it every frame
(`VUID-vkCmdPipelineBarrier-dstAccessMask-02816`, found while testing mode 3); the RTX images were exact anyway. A
callback's command now has the stages of its resources' usages (every shader stage for shader reads and writes, the
copy stage for copies, all stages otherwise); stock records callbacks only for MetalFX, whose driver ignores barriers.

## Verification

lavapipe, async compute emulated on the main queue, City Lab with SSIL and SSR (container test argument): on against
off **0 differing pixels**, 0 async conflicts, 0 synchronization hazards (Vulkan synchronization validation). Mode 3
against 0 (`fellow147`): 0 differing pixels, 0 conflicts, 0 validation messages (before the callbacks' fix: 324 per
capture run); a run with autotune's evaluations every 20 frames switched SSR between the queues without conflicts.

RTX (`m12_f171_ssil_warm`, warm sequences): `fellow144` (imageLoad) against `fellow145` (texelFetch), and SSIL on the main
queue against off: City Lab and Bistro exterior **bit for bit**, Bistro interior 67.1 dB and Sponza 75.5 dB (their
floors). 0 async conflicts in every A/B run.

## Measured

In-process A/B (`m12_f171_ssil_ab`, `fellow145`, autotune off, profiled), SSIL on the main queue against off:

| scene | GPU | the main queue's wait region | SSAO (ms) | SSIL (ms) |
|---|---|---|---|---|
| Bistro interior | **−1.5 %** (p 8e-15) | 1.114 → 0.533 ms | 0.400 → 0.699 | 0.211 → 0.270 |
| Bistro exterior | **−0.8 %** (p 1e-5) | | 0.591 → 0.797 | 0.238 → 0.302 |
| Sponza | **−0.4 %** (p 3e-4) | | 0.516 → 0.714 | 0.228 → 0.299 |

Run beside each other the two effects each take longer (they share the GPU), but together less: the gaps of one chain
(its barriers between dependent dispatches) are filled by the other.

Modes 1 and 2 against 0, in one process (GPU, autotune off; `m12_f172_split_ab` with `fellow146`, `m12_f173_split_ab`
with `fellow147`, the callbacks' barriers fixed):

| scene | 1: SSIL on main (f172 / f173) | 2: SSIL and SSR on main (f172 / f173) |
|---|---|---|
| Bistro interior | −2.8 % / **−2.7 %** | −4.3 % / **−3.8 %** |
| Bistro exterior | −1.5 % / **−1.1 %** | −1.6 % / **−0.7 %** |
| Sponza | −0.3 % / **−0.6 %** | +0.1 % (n.s.) / **+0.3 %** |

(all significant except the one marked; each scene's runs at its own clocks.) Where the main queue has little to do
beside the effects (Bistro interior: no shadow casters in view, no fog) SSR on it gains another 1.1 %; elsewhere it
costs 0.4-0.9 % against mode 1.

Between processes with autotune on (`m12_f173_auto`, 3 runs each, frame time against mode 1): mode 2 +0.5 %, −0.5 %,
−0.0 %; **mode 3 +0.5 %, −1.7 %, −0.1 %** (Bistro exterior, Bistro interior, Sponza; none significant at 3 runs).
Autotune's evaluations of `TUNE_SSR_MAIN` kept SSR on the main queue in all three scenes (Bistro interior on faster by
0.03-0.14 ms per frame, Bistro exterior by 0.03-0.05 ms with standard errors of 0.01-0.02, Sponza undecided), so mode
3 behaved as mode 2 there; for Bistro exterior and Sponza this disagrees with the in-process A/B above (autotune off:
mode 1 faster by 0.4 and 0.9 %), measured with every other tunable on.

## Decision

KEEP, default 3 (`fellow147`): SSIL on the main queue in every scene with async compute, SSR beside it where autotune
finds it faster. Exact: lavapipe bit for bit; RTX warm sequences of the defaults (mode 3, autotune on) against mode 0
with autotune off (`m12_f173_warm`): Bistro exterior **bit for bit**, Bistro interior 67.10 dB and Sponza 75.47 dB,
their floors.
