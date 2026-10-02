# 0013 — TAA motion vectors of static geometry from the depth buffer

Status: **in test** (`fellow33`) · Branch `fellow/geometry` · Toggle `rendering/fellow/postfx/taa_camera_motion_vectors`
(default **off** until measured) · Code: `forward_clustered/render_forward_clustered.{h,cpp}` (`fellow_taa_motion_pass`),
`effects/motion_vectors_store.{h,cpp}` (`process_fellow_taa`), `shaders/effects/motion_vectors_store.glsl`,
`fellow_settings.*`.

## Problem

With TAA, stock draws every opaque surface with the motion-vector variant of the scene shader (renderer audit A.3,
item 5): the vertex shader unpacks and transforms each vertex a second time with the previous frame's model and
camera matrices, passes a second clip position to the fragment shader, and the opaque pass writes a third colour
target (RG16F velocity). For geometry that did not move, that velocity is a function of the pixel's depth and the two
camera matrices only. In the audit's profiles the TAA preset made the opaque pass 0.1-0.6 ms slower than the preset
without TAA (single runs, ±0.2 ms), most of all in the geometry-heavy Sponza (12.8 M distinct triangles, 4-5 M per
camera pass after mesh clusters).

Stock already has the other path for the upscalers (FSR2, MetalFX): static surfaces drawn without motion vectors,
their velocity filled afterwards, and the surfaces that move (`transform` changed this frame, skinned, particles,
multimeshes with motion offsets, materials whose vertex code is animated) drawn in a separate *motion pass* with the
motion-vector shaders. TAA does not use it.

## Change

With the toggle on, TAA uses the motion-pass path (`fellow_taa_motion_pass`: TAA, no upscaler, one view, no MSAA,
storage-capable buffers; otherwise stock's path):

1. The opaque pass draws the static surfaces with the colour-pass variants without motion vectors (no second vertex
   transform, no velocity target). Moving surfaces are left out of it (stock's inclusion mask).
2. After the opaque pass, a full-screen compute pass (`MotionVectorsStore`, stock's MetalFX pass, with a FellowEngine
   entry point) writes every pixel's velocity from the depth buffer.
3. The motion pass draws the moving surfaces with the motion-vector variants (colour and velocity), as for FSR2.

**What the store computes.** The scene shader writes `0.5 · ((prev_clip.xy / prev_clip.w − prev_jitter) −
(clip.xy / clip.w − jitter))`, both clip positions from that frame's jittered projection. The store takes the pixel
centre's NDC and its depth back through the current jittered projection and forward through the previous projection
without jitter, and adds the current jitter (folded into the matrix), which is the same expression evaluated on the
reconstructed point. The reprojection matrix is composed in double precision on the CPU (a static camera gives the
identity to ~1e-16 before the cast to float). Where the depth is still the clear value (nothing drawn: sky), the
velocity is 0, as stock's opaque pass leaves it.

Two stock behaviours are kept: the opaque pass alpha is not zeroed (the upscalers' path sets the pass alpha multiplier
to 0; stock's TAA path does not), and moving instances keep the "dynamic" bit of the normal-roughness buffer (the
upscalers' path drops it).

## What it changes in the image

The velocity of a static pixel is computed from its depth instead of interpolated across its triangle: the same
value up to float rounding, stored in the same RG16F target (half-float steps are ~5·10⁻⁴ relative). Two cases differ by
construction:

* surfaces that write depth but are not drawn in the opaque pass (transparent materials with `depth_draw_always`):
  stock leaves velocity 0 there (the surface behind fails the depth test), the store gives them the camera motion of
  the visible surface — what TAA should use;
* opaque surfaces that write no depth: stock writes their own velocity, the store the velocity of what is behind.

Moving surfaces are drawn after the static ones instead of in sort order; with the depth pre-pass this changes a pixel
only where two surfaces have exactly the same depth.

## Verification

* lavapipe, Stress Lab `stress_pan` (camera pan across moving boxes, a vertex-animated surface, blended and clustered
  surfaces), velocity buffers read back after the motion pass every 10th frame, toggle off vs on: **86-97 % of the
  pixels bit-identical, the rest within 0.015 px** (largest velocity 20 px; the RG16F step at 7 px is 0.004 px).
  The velocity field is stock's up to half-float rounding.
* lavapipe, final images (Stress Lab `stress_pan` and `stress_sun`, 120 frames): mean 79.3 dB, minimum 74.4 dB; 0.06-0.7 %
  of the pixels differ, almost all by 1/255, scattered over specular highlights and smooth lit areas (TAA's history
  sampling turns the rounding differences into last-bit differences). Largest difference 12/255 (two pixels in the
  first frame of the pan). Negative control (the same pass without removing the jitter): 72.6 dB mean, 66.9 minimum,
  up to 95/255.
* RTX (`fellow33`, `m3_f33_temporal`, toggle off vs on, six scenes): pending.

## Result

Pending (`m3_f33_profile`, `m3_f33_perf`).

## Decision

Pending.
