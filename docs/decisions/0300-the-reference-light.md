# 0300 — The reference's light: soft, warm and bright in the shade; the stone dressed, the coppi as tiles, the glass dark

Status: **KEEP** (the showcase's: its environment, its sun, its materials, its roofs; nothing in the engine) · Branch
`fellow/geometry` · Code: `showcase/tools/build_rooftops.gd` (`_environment`, the sun, `_photo_materials`, the glass),
`showcase/rooftops/shaders/window_glass.gdshader`, `showcase/tools/blender/ac_common.py` (`Builder.coppo`),
`ac_buildings.py` (`eaves`' coppi) · Checks: the reference shot at the video's frames beside them, with the tonal
statistics of both.

## Problem

The owner, of the reference shot: "why does our lighting look so nasty, instead of soft and gentle as in the
reference?" Measured on the video's frames 25, 53, 57, 89 and 93 against FellowEngine's render of the same moments:

| | the video | ours (milestone 158) |
|---|---|---|
| luminance, median (linear) | 0.08-0.18 | 0.024-0.08 |
| the darkest twentieth | 0.017-0.026 | 0.007-0.010 |
| the fine detail's contrast (the picture less its 3 px blur, grey levels' σ) | 8.7-11.4 | 12.9-15.8 |
| saturation (HSV, of 255) | 87-90 | 106-131 |

The shade was nearly black where the video's is warm and bright with bounced light. The sun's shadows had knife edges.
Up close the walls were rock-faced and the coppi's photograph read as bark. The windows were white with the sun and
the sky mirrored in them.

## Change

* **The light**:
  * the sun 2.5° across (its penumbrae wide; 0.6° before), its energy 2.8 → 3.3;
  * the sky's and bounce's light doubled: ambient 1.55 → 3.0, SDFGI 1.25 → 2.0;
  * the exposure 1.0 → 1.3;
  * SSAO 1.3 → 0.6 (it drew every crevice black);
  * saturation 0.92 → 0.8, AgX's contrast 1.05 → 0.95.
* **The stone**:
  * the ashlar's faces flat: the parallax sinks only the height map's lowest 5 % (0.40-0.52: the joints and their
    arrises; at 0.44-0.6 the rough faces had sunk in bumps), 2 cm deep;
  * its normal map 0.18 → 0.08, its photograph's baked shading mostly blurred out (`albedo_soften` 0.55 → 0.85);
  * warmer (a honey sandstone, not a white limestone);
  * the rustication's normals 1.2 → 0.5; the dressings (quoins, frames, the balconette) at 0.35.
* **The coppi** (`Builder.coppo`): tapered cover tiles, the wide end down, each with a thick lip (the clay's
  thickness and a little of its inside) whose step over the course below reads, their edges down on the pans' rims;
  pan tiles, concave, in the channels between. The clay is smooth: its colour only, blurred to broad patches,
  terracotta.
* **The glass**:
  * no direct specular (the sun's disc mirrored in the panes had whitened whole windows);
  * the sky's reflection mostly hidden (AO 0.3: seen from below at a grazing angle it mirrored the open sky);
  * no lamps lit in the daylight.

## Checks

Frames 25, 57 and 89, ours against the video's (the mean of the three ratios):

| | before | after |
|---|---|---|
| luminance 5th / 25th / 50th / 75th / 95th percentile | 0.39 / 0.37 / 0.45 / 0.52 / 0.76 | 1.04 / 1.01 / 1.05 / 0.91 / 0.92 |
| fine detail | 1.59 | 1.31 |
| saturation | 1.27 | 0.97 |

* On frames 10, 45, 70, 84 and 93: the luminance's percentiles 0.74-1.19 of the video's, the fine detail 1.20, the
  saturation 0.99.
* What is left of the fine detail is mostly the ground (the house's street is cobbled where the video's is flagged)
  and the shutters.
* The square's opening shot rendered: soft and bright, nothing burnt out.

## Decision

KEEP: the global light is the reference's; the game's square has it too.

## Rejected / not done

* A blur over the whole picture to match the video's softness: the softness has to come from the light and the
  surfaces.
* The street before the house, cobbled where the video's is flagged; the camera's and the house's measure (decision
  0299's note): next.
