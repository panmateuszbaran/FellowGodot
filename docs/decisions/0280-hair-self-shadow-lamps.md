# 0280 — Hair shadowing itself from an omni or spot light

Status: **KEEP** (the hair's `self_shadow_light` may name one; nothing changes for a directional one) · Branch
`fellow/geometry` · Code: `modules/fellow_fluid/fellow_hair_3d.{h,cpp}` (`_find_self_shadow_light()`,
`_prepare_self_shadow()`, the self-shadowing material's light test) · Checks: `bench/hair_check.gd --lamp`, the
regression scene `hair`.

## Problem

Decision 0268's self-shadowing (`FellowHair3D.self_shadow`) dims the light of one directional light by a deep opacity
map of the guides' points made across it. A character lit by a lamp, a torch or a spotlight — indoors, at night — got
none: `self_shadow_light` took a `DirectionalLight3D` only, and the material dimmed only a directional light. The
readiness assessment listed it: "its self-shadowing from the sun alone".

## Change

* `self_shadow_light` may be an `OmniLight3D` or a `SpotLight3D` (the automatic choice stays the scene's first
  directional light).
* For such a light the map is made along the direction towards it from the hair's centre (the node's place) — the same
  map as for a directional light from there: the light is taken as far against the head's size (a lamp a metre away
  from a head 0.25 m across sees it within about 15°).
* The material (the self-shadowing variant only) is told which kind it is (`self_shadow_point`): a directional light's
  test as before (its direction within 2.6° of the map's); an omni or spot light's: a light that is not directional
  whose direction at the fragment is within about 18° of the map's (`dot > 0.95`). The plain material (no
  self-shadowing) is untouched.

## Checks (lavapipe)

`hair_check.gd` (10 000 strands, 160 guides), the self-shadowing on; `--lamp=<m>` puts the sun out and an omni light
that far away where the sun shone from:

| | the guides' points' least and mean share of the light |
|---|---|
| the sun | 0.000, 0.270 |
| a lamp 3 m away | 0.000, 0.270 |
| a lamp 1 m away | 0.000, 0.270 |

The same map (the same direction). The pictures (the whole frame's mean luminance, the hair a part of it), the
self-shadowing off → on: the sun −1.1 % (back), −1.3 % (side), −0.3 % (front); the lamp 3 m away −1.4 %, −1.4 %,
−0.3 % — the lamp's light dimmed as the sun's was (with the sun out, nothing else could darken it: the fill light is
directional and from elsewhere). The regression scene `hair` exact.

## Decision

KEEP.

## Rejected / not done

* **A perspective map from the light's position** (an omni light close to the hair seeing its sides at different
  angles): the orthographic map along the central direction holds within the 18° the material allows; a light inside
  the hair is a case for no self-shadowing.
* **Several lights at once**: one map a head (its cost, about 75 µs on a worker thread, a light).
