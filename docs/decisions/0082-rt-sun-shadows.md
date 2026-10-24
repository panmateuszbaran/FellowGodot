# 0082 — Ray-traced sun shadows

Status: **KEEP**, off by default (a look) · Branch `fellow/geometry` · Settings `rendering/fellow/raytracing/sun_shadows`,
`raytracing/sun_shadow_rays`, `raytracing/sun_shadows_interleaved` (with `raytracing/enabled`, decision 0080) ·
Code: `servers/rendering/renderer_rd/shaders/effects/fellow_rt_shadow.glsl`, `renderer_rd/effects/fellow_rt_shadow.*`,
`renderer_rd/shaders/forward_clustered/scene_forward_clustered{,_inc}.glsl`,
`renderer_rd/forward_clustered/render_forward_clustered.*`, `scene_shader_forward_clustered.h`, `fellow_settings.*`.

## Problem

The sun's shadow is the largest part of an outdoor image's shading, and shadow maps show their limits there: the
texel size grows with the cascade (soft, blocky shadows a few metres away), the bias trades acne against light leaking
at contacts (peter-panning), and the soft-shadow filter's penumbra is an estimate from the blocker search. With the
frame's ray tracing scene (0080) the shadow can be traced per pixel.

## Change

For the first directional light of the frame (as `LightStorage` numbers them; its shadow on), after the light
buffers' update and before the colour passes, three compute passes write a mask (RG8: the shadow, and 1 where it was
traced):

1. **Trace**: per pixel of the depth pre-pass within half the ray tracing radius of the camera (100 m with the default
   200 m: a ray towards the light stays inside the scene's box for as long), `sun_shadow_rays` (1) rays towards the light
   against the scene's shadow casters (instance mask bit 1), up to the radius, first hit. The rays are spread uniformly
   over the light's disc (its angular size, `light_angular_distance`: soft shadows; 0 = one exact ray). **Front faces
   are culled**: the ray keeps the faces that face the light, as a shadow map rendered from the light does — a receiver's
   own lit faces never occlude it (no acne, no bias to tune), and a receiver inside a closed mesh is shadowed by its top
   as with the shadow map. The origin is lifted along the geometric normal from the depth buffer; pixels facing away
   from the light by that normal are left to the shadow map (no ray: the normal is unreliable on alpha-tested foliage
   and at silhouettes, where shadowing them had darkened lit leaves on the RTX; a surface that truly faces away is unlit
   by N·L anyway). Alpha-tested casters (0080: non-opaque, mask bit 3) are candidates the
   ray notes and passes: a pixel whose ray meets one and no opaque caster is left to the shadow map, which tests their
   alpha (leaves' shadows stay as before, opaque casters' are traced). Shadow casters the scene does not hold (particles
   noted by the culler within the radius, MultiMeshes whose transforms are not on the CPU) make the trace assert shadows
   only: its lit pixels are left to the
   shadow map (Foliage Lab's forest floor had come out sunlit under its MultiMesh trees).
   **Interleaved** (`raytracing/sun_shadows_interleaved`, on): one pixel of each 2 × 2 block is traced per frame (all
   four in four frames, as RTAO's).
2. **Temporal**: soft shadows are blended with the previous frames at the reprojected position (depth-checked), up to
   12 frames' worth (interleaved: three samples, each four frames apart). Interleaved, the pixels not traced this frame
   keep that history (or where it is lost take their block's new sample), and every history is clamped to the range of
   the new samples in the 3 × 3 blocks around: where the traced neighbours agree (a shadow moved on, a lit wall) the
   pixel follows them within the frame, at an edge (they disagree) it keeps its accumulated value. Hard shadows
   interleaved go the same way (a new exact sample, the others clamped history).
3. **Blur** (soft shadows only): a depth-aware 3 × 3 blur, its weight falling as frames accumulate.

The opaque colour pass and the motion-vector colour pass (their surfaces are in the depth pre-pass) are compiled with a
new specialization constant (`fellow_rt_sun_shadow`, `packed_2` bit 1) and read the mask (binding 43 of the render pass
set; black, "not traced", for other passes) in place of the shadow map lookup for light 0 where it is valid; elsewhere
(farther than half the radius, transparent surfaces, XR, vertex lighting) the shadow map is used as before. The shadow
map is still rendered (transparent surfaces, volumetric fog and distant pixels use it).

## Verification

lavapipe, City Lab captures (sun 0.5°, 4 cascades):

* Off, and ray tracing on without this setting: the image is unchanged (see the milestone 16 verification run).
* On: the same shadows as the shadow map, sharper and with exact contacts (the buildings' and poles' shadows, the crowns
  of the trees); 34 698 of 388 800 pixels differ at 480 × 270 (max |d| 93).
* **Interleaved** against every pixel traced (City Lab's 60-frame pan): see the milestone 16 report. It first showed
  City Lab's sunlit walls shadowed, striped, in one frame in eight: present in every pixel's trace too (hard shadows
  traced every frame: frames 4, 13, 20, 29, 36, 45, 52 — the TAA jitter's largest offsets), hidden there by the
  accumulation and made visible by the clamp. The effects rebuilt positions from the depth buffer with the projection
  without the TAA jitter it was rendered with: on walls seen at a grazing angle up to half a pixel off, inside the wall,
  where the shadow ray meets the wall's back face. With the jittered projection (all ray-traced effects, decision 0080)
  those frames are gone (pixels darker than the shadow map's by more than 40 levels: at most 79 in the first frame,
  otherwise up to 30, against 2 400-5 900 in the glitched frames).
* Backface culling first (as for AO rays): the street camera, inside a building, left that building's footprint lit,
  where the shadow map shades it; culling front faces matches the shadow map there and removes self-shadowing by design.

Cost on the RTX laptop (`m16_f231_rt_ab`, 1080p, one ray, in one process against ray tracing with no effect): GPU
+0.21 ms in City Lab, +0.08 ms in PBR Lab, +0.67 ms in Sponza (the trace 0.92 ms, the opaque pass 0.30 ms faster),
+0.45 ms in Bistro exterior (0.90 ms, the opaque pass 0.53 ms faster); plus the scene's build (0.2-0.3 ms).
Interleaved (`m16_f243_rt_ab`, against ray tracing with no effect): Sponza's trace 1.06 → 0.54 ms, the GPU frame
+0.98 → +0.53 ms; City Lab +0.36 → +0.20 ms.

With the milestone's last build (`m16_f246_rt_ab`, `fellow182`, interleaved, in one process against ray tracing with
no effect): GPU +4 % in City Lab, +7 % in Sponza (the trace 0.48 ms), +14 % in Bistro exterior (0.72 ms).

## Decision

**KEEP, off by default.**
