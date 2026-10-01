# 0004 — Directional shadow static cache

Status: **DRAFT (measurement in progress)** · Branch `fellow/shadows` · Toggle `rendering/fellow/shadows/directional_cache`
(default **off**) · Code: `servers/rendering/renderer_scene_cull.cpp` (`_fellow_shadow_cache_*`),
`servers/rendering/renderer_rd/effects/fellow_shadow_cache.*`, `shaders/effects/fellow_shadow_cache_copy.glsl`,
`render_forward_clustered.cpp` (`_render_shadow_pass`)

## Problem

Milestone 1 measured directional shadows as the largest single pass of the geometry-heavy scene: Sponza 3.74 ms =
28 % of the frame at 1080p (up to 39 % on one fixed camera), 78 % of all draw calls. Stock Godot re-culls and
re-renders every caster into every cascade every frame (audit B.4: "no caching, no static/dynamic split"). In a
typical game almost all shadow casters are static and the sun moves slowly or not at all, so nearly all of this work
reproduces the previous frame.

## Evidence (before the change)

* Per-pass GPU timestamps (`godot-4.7.2_profile`, `fellow-m1_profile`): "Render Directional/SpotLight Shadows"
  Sponza 3.74 ms (28 %) at 1080p, 4.06 ms (22 %) at 1440p; Bistro exterior 0.59 ms (10 %) / 0.64 ms (7 %);
  PBR Lab 0.12-0.13 ms (3-4 %).
* Code (audit B.4): `RendererSceneCull::_render_scene` sends every cascade every frame; the atlas is cleared every
  frame (`RenderForwardClustered::_pre_opaque_render`); positional lights already have a version-based cache,
  directional lights do not.

## Stock implementation

Per directional light, per cascade (PSSM 4 splits): bounding-sphere fit of the camera frustum slice, light-space
rect snapped to 2-texel units at both edges, ortho projection from the sphere's far side to 20 m (pancake) beyond its
near side; casters culled with the slice extruded towards the light (`tighter_shadow_caster_culling`); all casters
drawn into the cascade's atlas rect (16-bit depth, reverse Z) every frame.

## Proposed implementation

1. **Texel lattice.** Stock's cascade rect is `N` texels of size `unit / 2` (its width `2 (r + e)` is exactly `N / 2`
   snapping units) whose origin is snapped to a 2-texel lattice fixed in light space. The cascade itself is left
   exactly as stock computes it; the cache records its lattice position so that cache and cascade differ by an
   integer (even) texel offset and cached depth can be copied texel-exactly. In the rare frames where float rounding
   snaps one edge a unit off (rect 2 texels wider or narrower) the cascade is rendered the stock way.
2. **Cache per cascade** (per viewport and light): a depth texture of `N + 2g` texels (`g` = guard band,
   default 256) holding every *static* caster of the cache column (no camera-dependent tight culling, since the
   cache outlives the camera pose), with a z range extended by `g t` on both sides.
3. **Every frame** the cascade's rect is copied from the cache into the atlas by a full-screen pass that re-bases the
   stored depth from the cache's range to the cascade's stock range (`d_atlas = clamp(d_cache * s + b, 0, 0.9999)`;
   0.9999 is exactly where the stock shadow pass pancakes casters), then only **dynamic** casters are drawn on top with
   the cascade's regular projection. Matrices, bias, normal bias, PCSS range, blend splits are the stock values.
4. **Static vs dynamic.** Every scene-cull instance records the frame of its last change (transform, AABB, materials,
   mesh/multimesh data via dependencies, visibility, layers, cast-shadow setting, transparency, instance uniforms, LOD
   bias, flags). A caster is static if unchanged for `static_frames` (default 60) frames and its shadow cannot change
   otherwise: mesh or multimesh, no skeleton/blend shapes, no visibility range or parent, not `GI_MODE_DYNAMIC`, and
   all materials pass `material_fellow_shadow_cacheable()` (no `VERTEX`/`POSITION` writes, no custom matrices, no
   `TIME` in vertex, no `TIME` with discard/alpha, no trails).
5. **Invalidation.** When an instance that could be in a cache changes or leaves the scenario, its old AABB is logged;
   caches built after it became static and whose light-space column the AABB touches are rebuilt.
6. **Rebuild policy.** A cascade is rebuilt when its rect leaves the guard band, its z range leaves the cache range,
   the light basis / layers changed, or an invalidation hits it (forced); proactively when its remaining guard margin
   drops below 50 %; and once more when casters became static after the build (promotion, at most every 30 frames).
   At most `max_rebuilds_per_frame` (1) rebuilds per frame: cascades that need one beyond the budget render the stock
   way that frame (fallback), so the worst frame costs about stock plus one cache render.
7. **When not to cache.** A rotating light disables the cache until it is still for 2 frames; a cascade whose texel
   size changes (camera FOV or near/far animation) is rendered stock until it is unchanged for 2 frames (no cache can
   match a changing lattice, and a rebuild per frame costs more than stock); a cascade that is served from its cache in
   fewer than 25 % of recent non-stock frames goes stock for 120 frames (thrash guard). **Cost gate:** every rebuild
   records the primitives and draws of the static casters it rendered, and the next frame's cull counts how many of
   those casters stock would draw (its camera-dependent tight culling); the cascade is served from the cache only if
   `(primitives + 2000 × draws) × (stock casters / cache casters) ≥ min_cost_per_texel × cascade texels`
   (`directional_cache_min_cost_per_texel`, default 0.125), i.e. only if the static casters it saves cost more than
   the per-frame copy, which writes one depth texel per cascade texel. Otherwise the cascade is rendered stock while it
   stays in the evaluated volume, for at most 600 frames, then evaluated again.
8. **LOD.** Cache renders choose mesh LODs from the cache's own texel size (orthographic LOD: the coarsest LOD whose
   error is below `directional_cache_texel_lod` = 1 shadow-map texel), since detail below a texel cannot show in the
   shadow map; stock picks shadow LODs from the camera distance. (`directional_cache_lod_travel` > 0 restores
   camera-distance LOD, as if the camera were that much closer, with invalidation after that much camera travel.)
9. **Unchanged cascades.** If every directional cascade is served from its cache, the per-frame clear of the atlas is
   skipped, and a cascade whose atlas rect already holds exactly the same copy (same cache content, offset and depth
   remap) and had no dynamic casters in this or the previous frame is neither cleared nor copied.

## Expected benefit

Directional shadow cost close to the cost of four 2048² depth copies (~0.1 ms) in static scenes and on fixed or
slowly moving cameras; during fast camera motion one cascade re-render every few frames instead of four every frame.
Fewer draw calls (CPU) for the same reason.

## Verification

(in progress — see "Actual visual quality")

## Actual performance

(pending)

## Actual visual quality

(pending)

## Regression risk

* Shadows of casters that change without any instance update (a vertex shader reading a texture that is updated
  every frame without `VERTEX` writes is impossible; but CPU mesh updates through `mesh_surface_update_*_region`
  without an AABB change do not notify dependents) would freeze until the next rebuild. Mark such objects
  `GI_MODE_DYNAMIC`.
* One directional light with shadows, PSSM 4 splits or orthogonal only (square cascade rects); other configurations
  and reflection-probe renders use the stock path.
* VRAM: 4 × (2048 + 512)² × 2 bytes = 52 MB per viewport with the default guard band.

## Decision

(pending)
