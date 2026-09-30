# 0005 — Directional shadow sampling in the Forward+ scene shader

Status: **DRAFT (measurement pending)** · Branch `fellow/shadows` · Toggles (default **off**):
`rendering/fellow/shadows/skip_backfacing_directional`, `rendering/fellow/shadows/skip_unblended_cascade`,
`rendering/fellow/shadows/pcss_blend_band` · Code: `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl`
(directional shadow loop, `fellow_directional_soft_cascade()`), scene UBO flags 8-10.

## Problem

After the shadow *maps* (decision 0004), the remaining directional-shadow cost is the *lookup* in the opaque pass.
With the benchmark suns (PCSS: `light_angular_distance` 0.5°, soft shadow quality "medium" = 12 blocker-search taps +
12 filter taps, `blend_splits` on) every lit pixel runs the PCSS kernel **twice**:

* the PCSS path of stock Godot blends a cascade into the next one over the **whole** split
  (`smoothstep(0, split0, z)`, `smoothstep(split0, split1, z)`, ...), so every pixel closer than the last split
  samples two cascades (up to 48 texture fetches for one light), and the blend replaces a good part of the
  higher-resolution cascade with the next, blurrier one;
* the PCF path blends only over the last 10 % of a split but still samples the second cascade for every pixel and
  multiplies it by a blend weight of 0;
* the lookup is done for every pixel, including pixels facing away from the light, whose direct light is exactly zero
  (`cNdotL = max(A + N·L, 0) = 0` multiplies the diffuse and specular terms in `light_compute()`).

## Evidence (before the change)

(pending: profile with `--sun_blend=0`, `--sun_shadow=0`, `--sun_angular=0` overrides to size the lookup cost)

## Proposed implementation

1. `skip_backfacing_directional` (image-identical): before the shadow lookup, compute
   `NdotL = min(A + dot(hvec3(normal), hvec3(L)), 1)` exactly as `light_compute()` does; if `NdotL <= 0` the lookup
   is skipped. Compiled out for every material where the shadow can still matter with `N·L <= 0`: custom `light()`,
   rim, backlight, transmittance, clearcoat (uses the vertex normal), Lambert-wrap and toon diffuse, toon specular,
   shadow-to-opacity, vertex lighting.
2. `skip_unblended_cascade` (image-identical): PCF path, the second cascade is sampled only when its blend weight
   is > 0 (`mix(a, b, 0.0) == a`).
3. `pcss_blend_band` (look change): PCSS path samples the cascade containing the pixel and, with blend splits, blends
   towards the next cascade over the last 10 % of the split only — the same band as the PCF path.

## Actual performance / visual quality / decision

(pending)
