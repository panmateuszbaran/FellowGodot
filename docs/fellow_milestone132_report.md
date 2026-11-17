# FellowEngine — milestone 132 report (dithered fades)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. Lavapipe in the cloud; the laptop's RTX 5070 Ti Laptop GPU
(jobs f470, f471).

## 0. Result

* `rendering/fellow/geometry/dithered_fade` (off; decision 0256): fades by distance — Godot's visibility ranges with
  `visibility_range_fade_mode`, an instance's own or its visibility parent's — are drawn in the depth pre-pass and the
  opaque passes with a share of each fading instance's pixels kept by interleaved gradient noise moving with TAA's
  frames, instead of blended in the transparent pass. The level coming in keeps the other share: two levels crossing
  over cover every pixel once. Shadows as before; Forward+.
* Seen in the checks: a crossing blended is see-through (0.8 % of the picture more than 16 levels off the far level
  alone), dithered solid (0.26 %, no hole); a hand-over's brightness steps by 3.4-3.6 levels in a frame when it
  switches, 2.5 blended, 0.7-0.8 dithered.
* `FellowHLOD3D.fade` (off): the merged proxies' members fade over `margin` instead of switching;
  `bake_proxies()`'s `group_fade` (0 m): the cells' proxies fade into their group's.
* Cost: two specialization bits set only on fading instances' opaque draws — every other pipeline unchanged, the image
  regression suite exact. On the RTX, 4 000 balls all mid-fade: GPU +0.9-1.7 ms against blending (both levels through
  the pre-pass with a discard), the renderer's CPU −0.7 ms (instanced: 2 draws instead of 7 135). City Lab's
  flythrough with its HLOD fading over 10 m: 2.24 ms blended, 2.28 ms dithered, 2.15 ms switching; the GPU 1.64-1.65 ms
  in all three.

## 1. Checks

`bench/dithered_fade_check.gd` (new; lavapipe 960 × 540, and the RTX at 1080p three times with the release template and
once with the editor build, every figure the same but the timings): nothing fading — the same picture with the setting
on, exactly; the crossing; the hand-over's steps; FellowHLOD3D's fade (both drawn, no hole); the cost. City Lab's
`--city_hlod_fade`, `--city_hlod_margin` (job f471). The image regression suite (exact); the class reference; the
settings' descriptions.

## 2. Next

The hand-overs FellowEngine makes per instance on the GPU — the instanced HLOD proxies, the foliage's impostors
(decisions 0122, 0172) — still switch; a fade there needs a per-instance fade in the culling's output.
