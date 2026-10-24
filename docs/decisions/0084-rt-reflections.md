# 0084 — Ray-traced reflections in place of SSR

Status: **KEEP**, off by default (a look) · Branch `fellow/geometry` · Settings `rendering/fellow/raytracing/reflections`,
`raytracing/reflections_max_roughness` (0.5) (with `raytracing/enabled`, decision 0080; lit with DDGI, 0083, when it is
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
2. **Temporal**: rough surfaces accumulate up to 16 frames at the reprojected position (mirror-like ones take one exact
   sample).
3. **Blur**: depth-, normal- and roughness-aware, its radius growing with the roughness (0 to 3 pixels); the weight fades
   out between 80 % of the maximum roughness and the maximum, where the probes and the sky take over.

The opaque and motion-vector colour passes are compiled with a new specialization constant (`fellow_rt_reflections`,
`packed_2` bit 3) and mix the traced radiance into the indirect specular light (binding 47; black, weight 0, for other
passes), after the specular occlusion (the traced rays carry their own occlusion). SSR's flag is cleared from the
scene's implementation data while the traced reflections replace it.

## Verification

lavapipe: see the milestone 16 report (PBR Lab with SSR enabled, the defaults and ray tracing without this setting
unchanged).

Cost: measured on the RTX when the benchmark queue runs again (one ray per pixel up to the maximum roughness, a shadow
ray per lit hit).

## Decision

**KEEP, off by default.**
