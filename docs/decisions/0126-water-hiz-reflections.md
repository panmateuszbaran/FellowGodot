# 0126 — Water's reflections traced over a Hi-Z pyramid of the depth texture

Status: **KEEP, on** (the RTX measurement decides; off restores the march of 0123 exactly) · Branch `fellow/geometry` · Setting `rendering/fellow/postfx/depth_copy_nearest_mipmaps` (on) ·
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
more. The RTX measurement decides.

## Decision

**KEEP, on**, pending the RTX measurement: the reflections are geometrically right at any distance (the far shore's
parallax, without jitter), the lookups fewer in the views that cost most. `postfx/depth_copy_nearest_mipmaps` off
gives the linear march back bit for bit. A Fresnel-scaled step count for the linear march (half the steps below a
Fresnel of 0.3) was tried first: no measurable change on lavapipe (+2-9 %, its spread), reverted.
