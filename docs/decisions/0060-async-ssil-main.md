# 0060 — SSIL on the main queue beside the async SSAO and SSR

Status: **measuring** (`fellow145`) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/async_ssil_main`
(runtime, off until measured) · Code: `render_forward_clustered.*` (`_fellow_process_ss_effects` parts,
`_pre_opaque_render`), `effects/ss_effects.*`, `shaders/effects/ssao.glsl`, `ssil.glsl` (the normal buffer sampled),
`rendering_device_graph.cpp` (the async conflict check), `fellow_settings.*`.

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

## Verification

lavapipe, async compute emulated on the main queue, City Lab with SSIL and SSR (container test argument): on against
off **0 differing pixels**, 0 async conflicts, 0 synchronization hazards (Vulkan synchronization validation).

## Measured

Pending (`m12_f171`).
