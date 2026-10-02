# 0010 — Blended fragments with alpha 0 skip their lighting

Status: **KEEP** (`fellow24`, measured again as a specialization constant in `fellow29`) · Branch `fellow/geometry` · Toggle `rendering/fellow/shading/blended_alpha_zero_skip`
(default **on**) · Code: `scene_shader_forward_clustered.cpp` (`FELLOW_ALPHA_ZERO_SKIP_OK`),
`shaders/forward_clustered/scene_forward_clustered.glsl`, scene UBO flag 17 (`render_scene_data_rd.*`,
`scene_data_inc.glsl`), `fellow_settings.*`. Test scene: `benchmarks/project/scenes/blend_lab.gd` (`--scene=blend`).

## Problem

Foliage cards, fences, hair cards, decals drawn as quads and particles are often alpha-blended with textures that are
transparent over most of their area. Stock shades every fragment of such a surface completely — clustered lights,
their shadows (PCSS in the benchmark preset), reflections, fog — and the blend unit then multiplies the result by an
alpha of 0. Sponza's transparent pass (the alpha-blended plants) costs 1.3-2.1 ms (`m3_final_profile`): as much as the
opaque pass.

## Change

For materials whose fragments cannot change anything when their alpha is 0, the fragment shader stops right after the
material code (alpha, alpha scissor/hash, alpha antialiasing are all decided there) when the alpha of every fragment of
its 2×2 quad is exactly 0. The material qualifies (`FELLOW_ALPHA_ZERO_SKIP_OK`, decided when the shader is compiled):

* mix blending (`blend_mix`): colour `src·a + dst·(1 − a)`, alpha `a + dst·(1 − a)`; with `a = 0` both are `dst`;
* writes `ALPHA` without alpha scissor or hash: such a material is drawn **only** in the alpha pass (`uses_alpha_pass()`),
  never in the opaque pass, where blending is off; the alpha pass has a single colour attachment (no separate
  specular, no motion vectors: `_render_scene`);
* no depth writes in the alpha pass (`depth_draw_always` writes depth: excluded), no stencil, no alpha to coverage or
  edge antialiasing, no `discard` of its own.

The skip runs only in colour passes (not the depth, material, SDF or separate-specular variants). After the point
where it runs, alpha is only multiplied (pass alpha multiplier) or lowered (`min()` for shadow-to-opacity), so it is
still 0 at the output.

**Quad-uniform.** Discarding one fragment changes what its quad neighbours see in their derivatives, and the scene
shader takes derivatives after that point (reflection and ambient LOD, the roughness limiter, decal gradients, shadow
dithering). A first version discarded single fragments: lavapipe 109-407 pixels per image changed by up to 11/255,
all at the borders of transparent regions. The skip therefore drops a fragment only when all four fragments of its quad
(helper invocations included) have alpha 0, read through fine derivatives — Vulkan defines `dFdxFine`/`dFdyFine` as
the differences inside the quad, and `dFdyFine(dFdxFine(a)) = (a11 − a01) − (a10 − a00)` is the same for the four; with
the own value, the horizontal and the vertical difference all 0 it is exactly the diagonal neighbour. Every fragment of
the quad decides the same, so a quad is dropped whole or not at all (NaN or infinite alpha anywhere in the quad: never
dropped).

## Why it is exact

A dropped quad's fragments would each have blended with alpha 0: the target keeps its value bit for bit. No
surviving fragment has a dropped neighbour in its quad, so their derivatives are unchanged. Nothing else is written by
these fragments. (One difference by construction: a fragment whose colour is NaN or infinite and whose alpha is 0
would make the stock blend produce NaN (`inf · 0`); it is not drawn here.)

## Verification

* lavapipe, Blend Lab (cards with alpha-0 holes: linear and nearest filtering with mipmaps, an albedo alpha of 0, a
  shader writing `ALPHA = 0` over half its area, vertex-colour alpha, overlapping layers; materials that must not skip:
  `depth_draw_always`, alpha to coverage, premultiplied, additive; shadowed sun, omni and spot, fog, volumetric fog,
  SSAO, TAA), 4 fixed cameras and the 60-frame `blend_pan`: **0 differing pixels**. A debug build that paints the
  dropped quads magenta changes 38-334 k pixels per camera (about 99 % of what a per-fragment test would drop).
  PBR Lab (7 cameras) and Stress Lab (4 cameras): 0 differing pixels (their blended glass has a constant alpha: the
  skip never fires there; same debug build: 0 pixels).
* RTX 2060 (`fellow24`, toggle off vs on, `m3_f24_temporal`): Blend Lab 99 dB, max |d| 0 (bit-identical); the other
  scenes at their usual TAA/driver floor, as with the toggle off against itself.
* RTX timing:
  * Blend Lab transparent pass (`m3_f24_profile`, GPU timestamps): **0.417 → 0.275 ms (−34 %)**.
  * Blend Lab frame (`m3_f31_blend_az`, `fellow29` with and without the toggle, 5 runs each, 1920×1080):
    **2.738 → 2.519 ms (−8.0 %, p < 0.001)**.
  * Benchmark scenes (`m3_f27_az`, 5 runs): Sponza −0.4 %, Bistro interior −0.3 %, PBR Lab +1.2 %, all p > 0.25 —
    within noise. Sponza's transparent pass (its blended plants) goes 1.434 → 1.383 ms (`m3_f24_profile`, −3.5 %,
    one profile run: not separable from noise in the frame time). Glass with a constant alpha
    never meets the condition, and with `fellow29`'s specialization constants (0012) a material that does not qualify
    gets no extra code at all.

## Decision

**KEEP, on by default.** Exact (bit-identical output, verified on lavapipe and RTX), a large gain where the content
has it (alpha-blended foliage, fences, hair and particle cards with transparent regions) and no measurable cost
elsewhere.

