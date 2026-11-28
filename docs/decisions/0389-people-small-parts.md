# 0389 — The people's small parts: no shadows, not drawn far away

Status: **KEEP** · Branch `fellow/geometry` · Showcase: `showcase/scripts/fellow_person.gd` (`parts_lod`,
`_small_parts`), `showcase/tools/person_look.gd` (`--parts=0`, the last frame's draw calls)

## Problem

On the RTX the square's 63 people cost +1.7 ms of the render thread (decision 0374's measurement): each person is nine
meshes — body, clothes, shoes, hair, and five small parts: eyes, eyebrows, eyelashes, teeth (7 120 triangles) and
tongue —, each one a draw call in the depth pre-pass, the colour pass and every shadow pass. The small parts cast
shadows that fall inside the head or flat on the skin, and the mouth's are hidden behind the lips most of the time.

## Change

`_small_parts` (on, `FellowPerson.parts_lod`): the eyes, eyebrows, teeth and tongue cast no shadow (the lashes still do:
they shade the eyes); the teeth and tongue are not drawn beyond 8 m, the eyebrows and lashes beyond 30 m (under a
pixel at the showcase's field of view).

## Checks (lavapipe, `person_look.gd`)

* The seven people side by side (3.4 m away, a sun with shadows): **175 → 127 draw calls** a frame (−27 %), 240 162 →
  216 990 primitives.
* The image: Marco's face 51.3 dB against before (1.0 % of pixels more than 2 levels apart); the cast 53.0 dB (1.2 %).
  Within this tool's own noise between two identical runs (54.6 dB, 0.4 %: its idle animation's timing), so the change
  is not measurable apart from it; the differences sit at the eyes' corners and the hair's edges.
* No engine change.

## RTX laptop (2026-11-28; milestone 251's build against 244's, the square by day, 1920×1080, two runs each)

| | 244: without → with the people | 251: without → with the people |
|---|---|---|
| render thread | 1.69 → 3.34 ms (**+1.65**) | 1.57 → 2.99 ms (**+1.42**) |
| shadow draws a frame | 1503 → 2148 (+645) | 1492 → 1859 (**+367**) |
| GPU | 8.79 → 9.16 ms (+0.37) | 8.47 → 9.02 ms (+0.55) |

The 61 people's shadow draws −43 % (the small parts no longer cast), their render-thread cost −0.23 ms (−14 %). The
GPU's share moved the other way by 0.18 ms, inside the two builds' run-to-run spread on this view (the frame without
people itself 0.32 ms apart): with the people the frame is 9.16 → 9.02 ms. Milestone 249's skin (decision 0391) is not
in it: without its occlusion and relief the same frame is 9.09 ms.

## Decision

KEEP (the RTX's numbers above).
