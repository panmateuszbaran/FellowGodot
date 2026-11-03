# FellowEngine — milestone 38 report (drying in patches)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m38_f315`, `m38_s23`, `m38_s25`, 2026-11-03). Branch
`fellow/geometry`. Decision record `docs/decisions/0138-drying-in-patches.md`.

## 0. Result

Between dry and wet the ground is wet in patches: as a street dries after the rain the highs dry first and the dips
and puddles last, the puddles shrinking to their deepest instead of fading out — and it wets the other way round. At a
wetness of 0 or 1 nothing changes.

## 1. Development

* `FellowRain3D.drying_patches` (1) gives the surface's own water a wetness of its own: the puddles' noise as the
  ground's lows and highs, a broad noise (20 m, against the noise's 70 m repeat seen from above) and a fine one at the
  edges; the puddles' amount scales with the wetness.
* The first pattern (noises of 80 and 20 cm) mottled the ground; the puddles' noise gives patches of metres
  (lavapipe).
* Looking the puddles' noise up once for both (the drying and the puddles) changed the image at full wetness
  (lavapipe: up to 4 levels in 225 pixels — the compiler fused the reordered sums differently): dropped.

## 2. Verification

* Off is the previous image at full wetness (lavapipe, against milestone 37's build): Rain Lab without drops, the wind
  meadow under snow — 0 pixels differing.
* Lavapipe, the test scene at a wetness of 0.3, 0.6 and 0.85: mostly dry with a wet patch, large wet patches between
  dry ground, nearly all wet with the puddles — against an even damp at 0.6.

## 3. Performance

Bistro exterior half dry (wetness 0.5, the ground held), even against in patches, 1080p, 2 runs each
(`m38_f315_patches`, fellow236): the depth pre-pass +0.10 ms, the opaque pass +0.07 ms, the frame 4.677 → 4.891 ms
(+0.21 ms) while the ground is between wet and dry. The first measurement (`m38_f313`) was of a dry street: a clear
weather above freezing creates no rain node, so the ground given to the bench was dropped (FellowBench fixed).

## 4. Showcase

Bistro's square half dry (`m38_s25_bistro_drying`; `bistro_drying_patches.jpg`): wet in its dips, dry on its highs.
The street drying after the rain (`m38_s23_bistro_drying_loop`; `loop_bistro_drying.webp`): the rain stops and the
street dries in 9 s, the puddles shrinking.
