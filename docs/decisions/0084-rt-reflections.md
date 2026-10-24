# 0084 — Ray-traced reflections in place of SSR

Status: **KEEP**, off by default (a look) · Branch `fellow/geometry` · Settings `rendering/fellow/raytracing/reflections`,
`raytracing/reflections_max_roughness` (0.7) (with `raytracing/enabled`, decision 0080; lit with DDGI, 0083, when it is
on) ·
Code: `servers/rendering/renderer_rd/shaders/effects/fellow_rt_reflections.glsl`, `renderer_rd/effects/fellow_rt_reflections.*`,
`renderer_rd/shaders/forward_clustered/scene_forward_clustered{,_inc}.glsl`,
`renderer_rd/forward_clustered/render_forward_clustered.*`, `scene_shader_forward_clustered.h`, `fellow_settings.*`.

## Problem

Glossy floors, wet streets, metal and glass are where a real-time image gives itself away: SSR only reflects what is on
screen (the reflection of anything behind the camera, off the edges or hidden behind a foreground object falls back to
the reflection probes or the sky, and the switch shows), and reflection probes are blurred, parallax-free cubes. With the
frame's ray tracing scene (0080) the reflection can be traced.

## Change

Where the environment enables SSR and `raytracing/reflections` is on (a single view, the ray tracing scene built), three
compute passes run after the screen-space effects (SSR itself does not run, its framebuffer copy neither) and write a
buffer the opaque colour passes read:

1. **Trace**: per pixel of the depth pre-pass with a roughness up to `reflections_max_roughness`, one ray in the
   direction the surface's GGX lobe reflects the view into (visible normals sampled, Heitz 2018; the mirror direction for
   roughness 0), against the scene's camera-visible geometry, back faces culled. The ray starts above the geometric
   surface (the normal reconstructed from the depth buffer, as RTAO's) and is mirrored above it when the shading normal
   (a normal map, smooth normals on facets) sends it below: otherwise it sees the surface it leaves. A hit is lit as
   DDGI's (0083): its diffuse albedo (× (1 − metallic)) × (the sun × N·L / π × a shadow ray + the DDGI volume's
   irradiance, or the environment's ambient light outside it) + its emission. A miss returns the sky's radiance at a
   level that grows with the roughness.
   Alpha-tested surfaces (0080) are not hit: PBR Lab's grass no longer darkens the chrome sphere's horizon (the rays do
   not test alpha; as quads the grass would reflect as green blocks).
   **Interleaved** (`raytracing/reflections_interleaved`, on): surfaces rougher than 0.3 are traced one pixel of each
   2 × 2 block per frame; the others keep their reprojected history, or where it is lost take the block's new sample
   (lavapipe, PBR Lab: against tracing every pixel at most 33 levels, 56-60 dB). Interleaving had taken Sponza's
   reflections only from 2.26 to 1.57 ms on the RTX; a thread per 2 × 2 block (its smooth pixels and its rough pixel of
   the frame, so that no lane idles over rough surfaces) was slower still — 2.04 ms (`m16_f243`): smooth pixels then
   take four traces in a row on one lane. The trace stays a thread per pixel.
2. **Temporal**: rough surfaces accumulate up to 16 frames at the reprojected position (mirror-like ones take one exact
   sample; interleaved ones count their new samples only, up to 8).
3. **Blur**: depth-, normal- and roughness-aware, its radius growing with the roughness (0 to 3 pixels); the weight fades
   out between 80 % of the maximum roughness and the maximum, where the probes and the sky take over.

The opaque and motion-vector colour passes are compiled with a new specialization constant (`fellow_rt_reflections`,
`packed_2` bit 3) and mix the traced radiance into the indirect specular light (binding 47; black, weight 0, for other
passes), after the specular occlusion (the traced rays carry their own occlusion). SSR's flag is cleared from the
scene's implementation data while the traced reflections replace it.

## Verification

lavapipe, PBR Lab captures with SSR enabled (480 × 270), SSR against the traced reflections:

* The glossy, normal-mapped chrome sphere (`pbr_specular`): SSR's lower half shows the sky's brown ground colour where
  its rays leave the screen (78 of 255 against the grey ground's 150); traced, it reflects the ground itself (148), the
  checker strip and the horizon. The first version reflected it at 197, brighter than the ground: the hits were lit
  with the sun π times too bright, metals lit as diffuse, and rays sent into the sphere by its normal map hit its own
  surface. All three fixed (the first two in DDGI too).
* The roughness sweeps, gold and copper spheres: as SSR where SSR finds its hits on screen.
* With the maximum at 0.5 the checker strip (roughness 0.6) lost SSR's reflection of the sphere and took the sky's,
  a bright halo beside it; the default is 0.7, SSR's own range (it fades out from 0.6 to 0.7): the halo is gone.
* With DDGI, the hits inside the open room take its bounce light instead of the blue ambient.
* The defaults, and ray tracing with no effect: unchanged (the milestone 16 identity run).

Cost on the RTX laptop (`m16_f231_rt_ab`, 1080p, with the earlier maximum of 0.5, in one process against SSR with ray
tracing on): GPU +0.02 ms in City Lab, +0.07 ms in PBR Lab, +0.51 ms in Sponza (the trace 0.83 ms in place of SSR's
0.18 ms), −0.04 ms in Bistro exterior; plus the scene's build. With the default of 0.7: `m16_f233`.

With the milestone's last build (`m16_f246_rt_ab`, `fellow182`, in one process against ray tracing with no effect):
GPU +8 % in City Lab, +13 % in Sponza (the trace and its passes 1.43 ms, SSR no longer run), +5 % in Bistro exterior.

## Decision

**KEEP, off by default.**
