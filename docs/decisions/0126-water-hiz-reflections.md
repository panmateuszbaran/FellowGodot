# 0126 — Water's reflections traced over a Hi-Z pyramid of the depth texture

Status: **KEEP, on** (RTX: the reflections −38 %, the frame −12 %; off restores the march of 0123 exactly) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/depth_copy_nearest_mipmaps` (on) ·
Code: `servers/rendering/shader_compiler.{h,cpp}` (a mipmap filter on `hint_depth_texture`),
`renderer_rd/forward_clustered/scene_shader_forward_clustered.{h,cpp}`, `render_forward_clustered.{h,cpp}`
(`_fellow_depth_nearest_mipmaps()`, the transparent pass's binding 24), `renderer_rd/effects/ss_effects.{h,cpp}`
(`fellow_nearest_depth_mipmaps()`, SSR's Hi-Z reduction), `modules/fellow_fluid/fellow_water_shader.h` (the march).

## Problem

FellowWater3D's reflections (0123) are its largest cost: on the RTX 5070 Ti Laptop at 1080p the march is 0.93 ms of
the water's 1.6 ms (`m24_f270_water`), most of it in grazing views — the glint's and the pier's — where the rays cross
the screen towards the horizon and need all their steps. The march samples the depth texture at up to 32
quadratically spaced points along 80 m of ray (one per 10 pixels of its length on the screen), refines a hit with five
more, and offsets the samples per pixel and frame for TAA to fill the gaps. Most of the samples land on empty space:
the water's bottom below the ray, the sky above the far shore.

A material's `hint_depth_texture` is a single-level copy of the opaque depth: no way to skip empty space.

## Change

**The depth texture's nearest-depth mips.** When a material drawn this frame samples `hint_depth_texture` with a
mipmap filter written out (`filter_nearest_mipmap`, ...; the default filter does not count), the copy of the depth
buffer before the transparent pass gets a texture of its own with up to 8 levels (cells of up to 128 pixels), each
texel the nearest depth of the four (six, nine at odd sizes) below it — reverse-Z, the largest value (SSR's Hi-Z
reduction shader). The transparent pass binds it instead of the single-level copy; its base level is the same copy.
Materials sampling with an implicit level of detail at screen resolution read the base level as before. Without such a
material nothing changes; `postfx/depth_copy_nearest_mipmaps` off restores stock's single level.

**The water's march** checks the depth texture's levels (`textureQueryLevels`): with mips it traces hierarchically,
after Godot's SSR — the ray in screen space with its depth (linear there), one cell per step, a level up where nothing
in the cell is nearer than the ray, a level down where something may be; at the base level a hit counts when the ray is
behind the surface by less than its thickness (40 cm, or twice the ray's depth across the pixel). Up to twice
`reflection_steps` steps. Every pixel the ray crosses is tested: no gaps, no jitter, no refinement. A ray behind a
surface for more than four pixels (a deck, a bank) meets what the screen cannot show: it stops there, no hit, instead
of walking pixel by pixel behind it to the end of its steps (taking it as a hit on the surface in front reflected the
deck's lit top under the pier). The hierarchical ray follows the ray across the whole
screen (`reflection_distance` bounds only the linear march); a ray that hits nothing or reaches the sky falls back,
as before, to what the screen shows in its direction if that is distant, else the sky's IBL. Without mips the linear
march as before. `debug_view` 7 (`DEBUG_VIEW_REFLECTION_COST`) shows the depth lookups per pixel.

## Results

**Exactness.** Without a material sampling the depth texture with a mipmap filter nothing changes: Fluid Lab
(FellowShallowWater3D reads it with `filter_nearest`) bit for bit; Water Lab with `postfx/depth_copy_nearest_mipmaps`
off bit for bit the images of the build before (the march of 0123).

**Look** (Water Lab, lavapipe 960 x 540, against the linear march): 54.9 dB aerial, 38.2 dB river, 36.4 dB shore,
36.0 dB glint, 33.4 dB pier. The differences are in the reflections of the far shore: the linear march ends at
`reflection_distance` (80 m) and falls back to what the screen shows in the ray's direction, seen from the camera —
the trees, about a degree too high; the hierarchical ray finds the shore itself, so the grass between the waterline and
the trees is mirrored under the waterline and wave facets tilted towards the camera mirror the bank, as they would.
The pier's posts and the boat are reflected in both, without the jittered samples' noise. With a far-occlusion taken
as a hit instead of a miss, the water under the pier mirrored the deck's lit top (31.3 dB shore): now a miss there,
as before. With MSAA 4x and no TAA (`--preset=high_msaa4`) the linear march's jittered samples show as speckles in
every reflection, the hierarchical one's are clean (28.6-51.3 dB between them). At a render scale of 2 with MSAA 8x
(`--preset=reference_ssaa`: the depth texture at the internal size) the same kind of differences, 35.3-40.1 dB.

**Cost.** lavapipe's timing does not show the march: its texture filtering in software (the waves' anisotropic
lookups) makes the water's other work dominate — the transparent pass 167 / 139 / 202 / 129 / 96 / 184 ms (flythrough,
shore, pier, river, aerial, glint) with the linear march, 173 / 147 / 197 / 126 / 97 / 185 ms hierarchical, within its
spread; Water Lab without reflections is not faster either. The pyramid's build shows: the depth copy 1.7-2.1 →
4.0-4.5 ms of lavapipe (eight levels, a second copy of the base level).

What the GPU does — the depth lookups per water pixel (`debug_view` 7, Water Lab at 1920 x 1080, mean and the mean
of the maxima of 8 x 4 blocks of pixels, a warp's: a warp waits for its slowest ray):

| camera | linear: mean | hierarchical: mean | linear: warp maximum | hierarchical: warp maximum | RTX: the march (f270) |
|---|---:|---:|---:|---:|---:|
| shore | 22.0 | 19.3 (−12 %) | 26.5 | 22.7 (−14 %) | 0.53 ms |
| pier | 10.6 | 8.5 (−20 %) | 15.1 | 11.5 (−24 %) | 0.68 ms |
| river | 22.2 | 20.5 (−8 %) | 25.5 | 25.6 | 0.01 ms |
| aerial | 18.0 | 22.2 (+23 %) | 20.6 | 29.5 (+43 %) | 0.19 ms |
| glint | 23.5 | 16.4 (−30 %) | 27.9 | 19.2 (−31 %) | 1.06 ms |

At 960 x 540 the linear march takes about half the steps (one per 10 pixels) and the hierarchical one about one
fewer level: glint −24 %, pier −16 %, shore +17 %, aerial +117 %. The hierarchical traversal gains where the
march costs most — grazing views, rays crossing the screen towards the horizon over empty water and sky — and loses in
the aerial view, where rays rise towards the camera past the trees on the banks (the cells' nearest depth there is
nearer than the ray: down and up the levels), but the march costs little there. A step of it is more arithmetic than
a step of the linear march (two cell edges; at the base level three view depths), the lookups the same latency: if
the GPU's time follows the lookups, the views' reflections cost 0.1-0.3 ms less (glint, pier), the aerial 0.04 ms
more.

**RTX 5070 Ti Laptop** (`m26_f275_water`, fellow208, 1080p, autotune off, 3 runs each, pass timestamps):

| Water Lab | frame (flythrough) | GPU | transparent pass | shore | pier | river | aerial | glint |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| linear march | 3.471 ms | 3.128 ms | 1.456 ms | 0.866 | 1.422 | 0.145 | 0.329 | 1.530 |
| **hierarchical** | **3.140 ms** | **2.762 ms** | **1.078 ms** | 0.615 | 1.085 | 0.147 | 0.415 | 0.973 |
| no reflections | 2.531 ms | 2.181 ms | 0.465 ms | 0.281 | 0.662 | 0.100 | 0.194 | 0.498 |

(per camera: the transparent pass, where the water is drawn, ms.) The reflections cost 0.61 ms instead of 0.99 ms
(−38 %), the frame's GPU time −0.37 ms (−12 %): shore −29 %, pier −24 %, glint −36 % of the pass, the aerial view
+26 % (0.09 ms; as the lookups said). The pyramid's build: the depth copy 0.031 → 0.080 ms (a second copy of the base
level and seven reductions) — also with `screen_space_reflections` off, the shader's filter asking for it (since
`560d36c` the shader asks for the mips only while reflections are traced).

**Rays towards the camera bounded** (`m26_f276_water`, experiment `exp/hiz1`, fellow209h): looking down at the water
the reflected rays come towards the camera, and only what stands between the water and the camera can meet them;
ending them at `reflection_distance` as the linear march does changed the aerial view's water pass by −0.01 ms
(0.439 → 0.429 ms; the other views within the runs' spread, the frame 2.821 → 2.856 ms): not kept. The aerial view's
extra cost is in the rays climbing past the trees on the banks, not in their length.

**The single-level copy skipped** (`e99e5db`): the pyramid's base level is the same copy and is what the transparent
pass binds, so the stock copy is made only when something else reads it — an opaque or depth-pass material sampling
the depth texture (they read the frame before's copy), MSAA's depth of field. Water Lab bit for bit on lavapipe; on the
RTX (`m26_f280`, fellow210 against fellow211, 3 runs) the depth copy 0.023 ms cheaper in Water Lab and Rain Lab (the
pyramid's whole cost now about 0.055 ms at 1080p).

**In motion** (`m26_f282_water_temporal`, fellow211, 1080p, Water Lab's two sequences of 60 frames): the mean absolute
difference between consecutive frames (luminance, frames 10-59) 0.473 → 0.462 (`water_flow`) and 2.520 → 2.487
(`water_pan`, the camera panning) — a little less change from frame to frame without the jittered samples; the frames
alike (the far shore's reflection as in the stills).

## Decision

**KEEP, on**: on the RTX the reflections cost 38 % less (Water Lab's frame −0.37 ms), geometrically right at any
distance (the far shore's parallax, without jitter). `postfx/depth_copy_nearest_mipmaps` off
gives the linear march back bit for bit. A Fresnel-scaled step count for the linear march (half the steps below a
Fresnel of 0.3) was tried first: no measurable change on lavapipe (+2-9 %, its spread), reverted.
