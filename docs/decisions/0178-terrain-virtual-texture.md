# 0178 — A terrain's material from a virtual texture: pages baked once, read in every fragment

Status: **KEEP** (`FellowTerrain3D.virtual_texture`, off: nothing changes without it; and a fix to the layers' colours)
· Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_terrain_vt.{h,cpp,glsl}` (the pages, the atlas, the
overview, the bake kernel), `modules/fellow_world/fellow_terrain_3d.{h,cpp}` (the shader's virtual texture variant,
the pages a camera wants each frame, the edits and changes that bake pages again; the layers' colours passed as
vectors), `servers/rendering/fellow_settings.{h,cpp}` (two settings, two counters),
`benchmarks/project/bench/terrain_vt_check.gd`, `tools/fellow_regression/project/scenes/terrain_vt.tscn` · Checks:
lavapipe only (no GPU run in this milestone, by the owner's choice).

## Problem

The readiness assessment's open-world ground stops at "next virtual texturing". `FellowTerrain3D` blends up to nine
layers in every fragment: the splat weights, each weighted layer's albedo and normal textures (three projections for a
triplanar one), the height blend, the macro variation — the same arithmetic for every pixel of the ground in every
frame, though the ground's material changes only when it is painted or sculpted. AAA terrains bake that blend into a
virtual texture once and read one or two texels per fragment.

## Change

**`virtual_texture`** (off), **`virtual_texture_texels_per_meter`** (64), **`virtual_texture_lod_bias`** (0):

* **Pages**: the terrain's map in pages of 128 × 128 texels at the finest mip, each mip's texels twice as wide. A
  compute kernel bakes a page with 4 texels of border on each side (the material beyond its edges) by the shader's own
  arithmetic — each texel's footprint on the ground as the layers' gradients, so their textures come prefiltered —
  into a slot of an atlas every terrain shares (`rendering/fellow/terrain/virtual_texture_atlas_size`, 4080: 30 × 30
  slots; two RGBA8 textures, the albedo in sRGB with the roughness, the normal's x and z in the terrain's space).
* **The page table** (per terrain, an RGBA8 texture with a level per mip): a page's slot and mip, or, while it is not
  baked, its nearest baked ancestor's. The fragment chooses its mip from its gradients (up to 4:1 anisotropic taps
  within the border), reads the table and the atlas there and at the next coarser mip, and blends the two.
* **The overview**: the coarsest mip, one page over the whole map, is not a slot but two small textures per terrain
  (128 × 128 with their own eight mips, each level baked by the kernel from its own footprint). It is read where a
  fragment covers more than the coarsest page's texels (far away, at grazing angles) and where no page is baked yet; it
  is baked first, before any page, whenever the terrain changes.
* **Which pages** (on the CPU, each frame): a quadtree from the coarsest page down, a page's children wanted where a
  pixel at the camera's distance from it covers less than its texel (outside the frustum two mips coarser); the missing
  ones baked within `rendering/fellow/terrain/virtual_texture_pages_per_frame` (16) pages a frame for every terrain,
  coarsest and nearest first; a slot taken from the page wanted longest ago when the atlas is full (its terrain's table
  updated before the slot is baked again); while more than 90 % of the slots are wanted every terrain's pages are a
  quarter of a mip coarser each frame, up to four, and finer again under 60 %.
* **Changes**: sculpting, painting, a blit, the layers, the height blend, the macro variation, a region's offset bake
  the pages over them (or all) again, in place — the old content drawn until then — and the overview.
* `debug_view` **Virtual Texture**: the mip drawn as a colour, the pages' edges darker. `get_virtual_texture_stats()`.
  Regions (decision 0173) each have their own pages in the shared atlas, the textures in the group's space. Without a
  RenderingDevice (Compatibility) the layers are drawn, with a configuration warning.

**Fixed on the way — the layers' colours.** Milestone 67 (decision 0175) found `albedo_color` (sRGB, as a material's)
"drawn as linear" and passed it linear; but a `Color` reaching a `vec4` uniform is converted from sRGB by Forward+ and
Mobile, so the colour was converted twice and layers drew darker than their colour (an sRGB 0.5 grey drew as 0.039
linear instead of 0.214). The virtual texture's pages, baked from the same values without that conversion, showed it.
The colours now reach the shader as vectors, converted once. The scenes milestone 67 had converted to keep their look
(the regression scene, the editor's terrain preset, the showcase's builder) have their colours back as they were, and
draw as before; Slice Lab and Frontier Lab, whose colours were written as linear, draw them as such again (brighter
than in milestones 67-69, and now as Frontier Lab's proxies, which took them right).

## Verification

Lavapipe (Mesa's CPU Vulkan driver; the owner asked for no GPU run this time):

* `bench/terrain_vt_check.gd` (640 × 360, a 513 m terrain at 1 m, four layers with textures — one triplanar, one
  painted along a path — and normal maps, 64 texels a metre): five views drawn from the layers and from the pages;
  a sculpt and a paint stroke with the pages on; the terrain split into four regions; a flight of 301 frames low
  across them. Run with a budget of 64 pages a frame and the default atlas, and with the default budget (16) and an
  atlas of 1632 texels (144 slots: evictions and the pressure bias).
* The regression scene `terrain_vt` (the terrain scene's terrain with the pages); the suite with the editor build and
  the release template: every picture exact but the two terrain scenes, one pixel one level off (the colours no longer
  converted to sRGB and back), blessed with the new one.
* The regression terrain against its layers at 512 × 288 and 2048 × 1152, and a single-colour layer against its
  colour (how the double conversion was found: an sRGB 0.5 grey drew as 0.216 on screen with the layers and 0.502
  with the pages, under a linear tone map).
* The editor check (every check passes; an untouched editor with a virtual-textured terrain draws no frame); the
  `terrain_vt` scene with the Compatibility and Mobile renderers (no error).

## Results

**The pictures** (the pages against the layers, the same view; PSNR, the share of pixels off by more than 8 levels):

| view | wanted pages | default atlas, 64 a frame | 144 slots, 16 a frame (pressure bias) |
|---|---|---|---|
| near the ground | 264 | 42.2 dB, 0.8 % | 42.4 dB, 0.8 % (1.25) |
| across the land | 128 | 41.0 dB, 1.7 % | 41.8 dB, 1.3 % (0.5) |
| down | 80 | 42.6 dB, 0.6 % | 42.3 dB, 0.7 % |
| grazing | 248 | 43.1 dB, 0.7 % | 41.8 dB, 0.8 % (1.0) |
| from high up | 16 | 43.3 dB, 1.5 % | 43.3 dB, 1.8 % |
| regions: across / down | | 42.2 / 43.6 dB | 43.8 / 41.3 dB |
| after a sculpt and a paint stroke (near) | | 46.6 dB (29 pages baked again) | 46.7 dB (59) |

Each view settles in 3-8 frames. The regression terrain (a filmic tone map, snow on its peaks): 36.1 dB at 512 × 288,
39.9 dB at 2048 × 1152 — the difference along the far ridges' silhouettes, where a fragment covers metres and the
pages average snow and grass that the direct shader's height blend separates.

**A frame on lavapipe** (the fragments' work in software, a proxy and not a GPU's time; 30 frames' median): near the
ground 85.4 → 69.0 ms (−19 %), across the land 126.2 → 80.4 ms (−36 %).

**The flight** (301 frames): with 64 pages a frame, one frame with pages missing (their ancestors drawn), 2 220 pages
baked, at most 64 a frame, no eviction; with 16 a frame and 144 slots, 20 frames with pages missing, 1 341 baked
(at most 16 a frame), 1 273 evicted, the pressure bias up to 1.0.

**Memory**: the atlas 127 MiB at the default size (made with the first terrain that uses it); a terrain's overview
175 KB and its page table (a 513 m map at 64 texels a metre: 256 × 256 entries with mips, 349 KB).

## Decision

Keep, off by default: a terrain chooses it (`virtual_texture`). The atlas costs its memory (127 MiB at the default
size) as soon as one terrain uses it, and the pages' bakes are GPU work in the frames that need them; whether a
fragment's two table reads and four atlas reads beat blending the layers is the GPU's question, measured on the RTX
next.

## Rejected / not done

* **A mip chain for the atlas**: neighbouring slots would bleed into a page's coarser mips; the pages' own mips are
  their coarser pages, and the overview has its own.
* **Feedback from the GPU** (the pages the fragments read, written to a buffer and read back): a frame or more of
  latency and a readback; the CPU's estimate from the camera's distance wants a few more pages than the view reads
  (17-265 a view here) but needs nothing from the GPU.
* **Gradients bounded to a pixel's footprint on the surface** (for the few pixels where a quad straddles a silhouette
  and the mip comes out far too coarse): tried, it changed nothing measurable here; those pixels are where the two
  ways differ by nature (below).
* **The same picture as the layers' everywhere**: the pages are filtered after the layers are blended, the direct
  shader blends filtered weights; with the height blend picking one layer, a far fragment covering snow and grass is
  their average in the pages and one of them in the direct shader. Close up they agree (below).
