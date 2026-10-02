# 0013 — TAA motion vectors of static geometry from the depth buffer

Status: **REJECT** (`fellow33`; code kept behind the toggle) · Branch `fellow/geometry` · Toggle `rendering/fellow/postfx/taa_camera_motion_vectors`
(default **off**) · Code: `forward_clustered/render_forward_clustered.{h,cpp}` (`fellow_taa_motion_pass`),
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
* RTX 5070 Ti (`m3_f33_temporal`, 60-frame sequences, toggle off vs on; the run-to-run floor of each scene in brackets,
  from `fellow29` vs `fellow33` with the toggle off in the same session): Bistro exterior 66.4 dB (99.0, bit-identical
  run to run), Bistro interior 65.9 (67.3), Lights Lab 71.1 (94.1), PBR Lab 69.2 (73.6), Sponza 69.7 (75.5); pixels
  off by more than 2/255: 0.000-0.005 %; Blend Lab 55.7 dB, 0.23 % of the pixels > 2/255 (its `depth_draw_always`
  cards, the case that differs by construction). **Not bit-identical**: below the floor in every scene, with the
  differences as small and as scattered as on lavapipe.

## Result

Profile (`m3_f33_profile`, one run each, GPU ms): the opaque pass is faster by **0.15 ms in Bistro exterior** (1.890 →
1.744) and **0.19 ms in Sponza** (2.012 → 1.818), 0.02-0.06 ms elsewhere. The new full-screen pass costs 0.03-0.05 ms
and, in PBR Lab (moving objects), the motion pass 0.03 ms; the sums of the passes move by −0.06 to +0.09 ms (the graph
also moves work across the timestamps, e.g. Lights Lab's sky update).

Frame (`m3_f33_perf`, 3 interleaved runs, reference `fellow33` with the toggle off):

| scene | toggle off ms | on ms | Δ | p |
|---|---:|---:|---:|---:|
| Sponza | 7.065 | 6.981 | −1.2 % | 0.33 |
| Bistro exterior | 4.970 | 4.918 | −1.0 % | 0.23 |
| Bistro interior | 4.054 | 4.029 | −0.6 % | 0.14 |
| Lights Lab | 3.007 | 3.001 | −0.2 % | 0.86 |
| PBR Lab | 2.815 | 2.855 | +1.5 % | 0.16 |
| Blend Lab | 2.572 | 2.622 | +2.0 % | 0.24 |

All within noise. The opaque-pass saving is real but small next to a 5-7 ms frame, and the extra pass (plus the
motion pass where objects move) takes most of it back.

## Decision

**REJECT** as a default: no measurable frame gain, and the image is not bit-identical (velocity equal up to half-float
rounding, TAA output at 55-71 dB instead of the floor). Code kept behind `postfx/taa_camera_motion_vectors` (off). Two
ways to make it pay, not tried: computing the velocity inside the depth downsample that SSAO/SSIL already run after
the pre-pass (no separate pass; the pre-pass depth equals the final depth for surfaces drawn in both passes), and
scenes with much more static geometry per pixel than these, where the vertex work saved grows.
