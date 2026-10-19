# 0067 — SSAO and SSIL: the last column written, one store per texel of the depth mipmaps

Status: **MEASURING** (both on in `fellow158`) · Branch `fellow/geometry` · Settings
`rendering/fellow/postfx/ss_interleave_exact_bounds`, `rendering/fellow/postfx/ss_depth_mips_exact` (both on) · Code:
`shaders/effects/ssao_interleave.glsl`, `ssil_interleave.glsl`, `ss_effects_downsample.glsl`, `effects/ss_effects.cpp`,
`fellow_settings.*`; debug (environment variables, off otherwise): `rendering_device.cpp`
(`FELLOW_DEBUG_CLEAR_TEXTURES`, `FELLOW_DEBUG_TEXTURE_LOG`), `render_forward_clustered.cpp`
(`FELLOW_DEBUG_FORCE_UBERSHADER`: every draw with the ubershader, to compare its images with the specialized
pipelines').

## Problem

Two runs of one build do not give the same images on the RTX machine, for stock as well as for FellowEngine
(`m13_f180_floor`: PBR Lab 82.1 dB stock, 73.5 dB FellowEngine; milestone 13, section 4). On lavapipe two runs of
FellowEngine are bit for bit identical (PBR Lab's warm sequence, 60 frames: 0 differing pixels), so what differs
between runs on the GPU is what lavapipe makes deterministic: the contents of memory nothing wrote, and the order of
stores that race.

## Evidence

**Memory never written.** lavapipe fills new allocations with a constant when `LVP_POISON_MEMORY=1` (zeros otherwise).
A read of memory nothing wrote changes the image between the two; a correct frame does not.

* PBR Lab's warm sequence: all 60 frames differ (max |d| 20), with FellowEngine and with `fellow/enabled=false`
  (stock's paths). Capture of `pbr_specular` without TAA and glow: only the last column (x = 479 of 480, rows with
  geometry). SSAO off: no difference; SSAO's quality and blur passes do not matter.
* Debug hook (environment variables of the FellowEngine build, off otherwise): `FELLOW_DEBUG_CLEAR_TEXTURES=a:b` clears
  the textures created without data with an index in [a, b) at creation, `FELLOW_DEBUG_TEXTURE_LOG=1` lists them. All
  cleared: no difference. Bisected to texture 56: SSAO's output (R8, 480 × 270).
* The interleave pass, which writes SSAO's output from the four deinterleaved quarters, returns for invocations past
  `ivec2(1.0 / params.pixel_size)`. In single precision 1 / (1 / 480) = 479.99997: truncated, the bound is 479 and the
  last column is never written. Widths where this happens: 480, 960, 1680, **1920**, 3840 (not 1280, 1366, 1440, 1600,
  2560; heights 720, 1080, 1440, 2160 are exact). At 1920 × 1080 the opaque pass reads SSAO from a column the frame
  never wrote. SSIL's interleave has the same bound; its output is cleared when created, so its last column is zero (no
  indirect light) rather than undefined.

**Racing stores.** `ss_effects_downsample.glsl` (depth mipmaps for SSAO and SSIL at quality high and ultra, from
Intel's ASSAO) selects the invocations that write the third mip level with `p_gtid.x % 8 == depth_array_offset.x &&
depth_array_offset.y % 8 == depth_array_offset.y`; the second half compares a value with itself. The level above uses
`p_gtid.y % 4 == depth_array_offset.y`. So four invocations of each workgroup (y = 0, 2, 4, 6) store into each texel of
that level, the ones with y = 4 and 6 averaging shared memory past the end of their column (`depth_buffer[i][x][y +
4]`, y + 4 up to 10 of 8). On a GPU, which store lands last is not defined and changes between runs (the two halves of
an 8 × 8 workgroup are two warps on NVIDIA). The fourth level (written when one of SSAO and SSIL is half size and the
other full size, as in the benchmark presets) has the same condition, and four workgroups store into each of its
texels.

## Change

1. `postfx/ss_interleave_exact_bounds`: the interleave passes of SSAO and SSIL use the output image's size as the bound
   (`imageSize`); the last column gets its value like every other.
2. `postfx/ss_depth_mips_exact`: the third level is written by the one invocation per texel the level above uses
   (`p_gtid.y % 8`), and the fourth by the workgroup at even coordinates of the four that cover a texel.

Both change pixels where stock's value is undefined (memory never written) or depends on the order of racing stores;
everywhere else the image is stock's. With `fellow/enabled=false` (or either setting off) stock's code runs.

## Verification

lavapipe (`fellow158` candidate), captures with the demote diagnostics (`debug/scene_shader_experiments` 4194304: no
undefined derivatives after `discard`) and 60 frames of settle (no ubershader draw in the captured frame):

* Memory never written (normal against `LVP_POISON_MEMORY=1`), every lab's cameras at 480 × 270: PBR Lab's last column
  no longer changes (before: every camera, up to 20 levels, spread by TAA and glow over the right 15 % of the image).
  What remains is small and elsewhere: PBR Lab `pbr_emissive` 3-8 pixels of at most 2 levels at the tip of a shadow
  seen through the glass (bisected to the positional shadow atlas: a shadow lookup outside the texels written; stock's
  too — without TAA one pixel of 1 level with FellowEngine and with it disabled; FellowEngine's TAA keeps it over a few
  more pixels), Lights Lab 26 pixels, Blend Lab 6, Stress Lab 1, each of 1 level; City Lab, Foliage Lab and Fluid Lab
  none.
* The images against both settings off (stock's code), 480 × 270: only the last columns change where SSAO or SSIL is on
  — PBR Lab 174-1 109 pixels per camera, up to 155 levels (stock's column holds the zeros lavapipe's allocation had: no
  light there), Lights Lab 40-226 pixels of 1-2 levels, City Lab 203-316 pixels of up to 71 levels.
  `ss_depth_mips_exact` alone changes nothing at 480 × 270 (the taps' mip levels stay below the third there); at 1920 ×
  1080 (PBR Lab) 34 679 pixels of `pbr_spheres` (up to 10 levels, 69.8 dB) and 481 of `pbr_overview` (1 level): the
  close-up's SSAO taps read the third level, whose texels stock took from whichever of its four stores lavapipe ran
  last.

RTX: (to measure — two runs of each against each other on the warm sequences of `m13_f180`, and the last column against
stock at 1920 × 1080)

## Decision

(pending the RTX measurement)
