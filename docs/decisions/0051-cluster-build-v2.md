# 0051 — The light cluster build: instanced proxy draws, a pack pass per element, no 21 MB clear

Status: **measuring** (`fellow128`) · Branch `fellow/geometry` · Toggles `rendering/fellow/lights/cluster_pack_elements`,
`rendering/fellow/lights/cluster_instanced_draws` (runtime) · Diagnostics `rendering/fellow/debug/cluster_verify`
(1 verify, 2 verify with a negative control), stats `lights_cluster_draws`, `lights_cluster_elements`,
`lights_cluster_verify_words`, `lights_cluster_verify_mismatches` · Code: `cluster_builder_rd.{h,cpp}`
(`_fellow_render_elements`, `_fellow_pack`, `fellow_render_buffer_clear`), `shaders/cluster_store.glsl`
(`FELLOW_PACK_ELEMENTS`), `shaders/cluster_render.glsl` (`FELLOW_INSTANCED`), `fellow_settings.*`.

## Problem

Forward+ builds its light clusters every frame in three steps, all before the depth pre-pass's consumers:

1. **A clear of the render buffer**: per cluster tile, one bit per element and one depth word per possible element
   (`max_clustered_elements` 512 × 5 types = 2 560): 2 040 tiles × 2 640 words = **21.5 MB** at 1080p, cleared with
   `buffer_clear` every frame in which any element is visible, whatever the number of elements.
2. **"Render 3D Cluster Elements"**: every light, decal and probe drawn as a proxy mesh (icosphere, cone or box) into a
   quarter-resolution framebuffer; the fragments set the element's bit and depth bits of their tile with atomics.
   Stock draws **one element per draw call** (its instancing loop is disabled, `#if 0` upstream): the fragment shader
   merges the invocations of a subgroup by tile only, which is correct only when a subgroup holds one element.
3. **"Pack 3D Cluster Elements"**: per tile, the element bits and, for each of the 32 depth slices and each type, the
   range of element indices present. 0014 gave each tile a workgroup with one invocation per slice, but every
   invocation still walks all of the tile's elements in turn, one dependent round of loads per element.

In Lights Lab (242 elements) the render and pack regions cost 0.112 ms each (`m9_f140_tc_ab`), plus the clear, which no
region reports (it falls between `> Bake 3D Cluster` and the first region). Bistro interior: 0.015 and 0.014 ms.

## Change

* **`lights/cluster_instanced_draws`**: consecutive elements drawn with the same proxy mesh are one instanced draw
  (`base_index + gl_InstanceIndex` was already the element index). The fragment shader variant `FELLOW_INSTANCED` groups
  a subgroup's invocations by tile **and element** (the depth word's offset, unique to both) instead of by tile, and
  merges the depth bits over the invocations of the same element (stock: over the subgroup). With one element per
  subgroup both are stock's. Only elements whose depth slices come from their conservative range (FellowEngine depth
  slices, 0008: bit 1 of `has_wide_spot_angle`) are batched: for those, the pack pass uses the rasterised depth bits
  only to know that the element reached the tile, and the set of (tile, element) pairs reached does not depend on how
  fragments are grouped. Others (stock's linear slices, area lights with a shrinking transform) keep one draw each.
* **`lights/cluster_pack_elements`**: the pack pass with one 64-invocation workgroup per tile. The tile's element bits
  are taken two words (64 candidates) at a time: each invocation handles one candidate (its loads, its slice range, its
  bit with `atomicOr`) and appends (type, slice range, index) to a list in shared memory; then each invocation takes one
  slice and half of the list and keeps per type the index range in a register, in stock's encoding. The two halves are
  combined at the end (min of minima, max of maxima). Min, max and OR do not depend on the order, so the stored words
  are stock's. The pass also **zeroes every word of the render buffer it reads** (the element bits after a barrier, each
  depth word by the invocation that read it): these are the only words the render pass sets, so the buffer is left
  all zero and the 21.5 MB clear is skipped in the next frame (`fellow_render_buffer_clear`; a new buffer, or a frame
  packed by another path, clears as before).

## Verification

`debug/cluster_verify` (1): after the build, the cluster buffer is read back; after the element pack the render buffer
is read back and every word checked to be zero; then the build is repeated with one draw per element and 0014's pack
(stock's if that is off), read back and compared word for word. 2: the same, with a deliberate error in the element
pack (bit 31 of every element word dropped).

lavapipe (480 × 270, capture mode, both toggles on):

| scene | words compared | mismatches | draws / elements |
|---|---:|---:|---:|
| Lights Lab | 35 916 480 | **0** | 2 073 / 18 740 |
| Lights Lab, negative control (2) | 35 916 480 | **31 234** | |
| Lights Lab, stock's linear depth slices | 35 916 480 | **0** | no batching (by design) |
| PBR Lab | 54 820 800 | **0** | |
| City Lab | 23 716 800 | **0** | |
| Stress Lab | 153 558 720 | **0** | |

Images, both toggles on against off (lavapipe, Lights Lab captures): **0 differing pixels**.

lavapipe never puts fragments of two primitives in one subgroup, so the instanced fragment path is verified on the RTX
(`m10_f146_cv`), where it can.

## Measured

Pending (`m10_f146`).
