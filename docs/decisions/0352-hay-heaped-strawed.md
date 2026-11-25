# 0352 — The cart's hay heaped and strawed

Status: **KEEP** · Branch `fellow/geometry` · Code: `showcase/scripts/rooftops_hay.gd` (new, a node on the cart);
`showcase/rooftops/rooftops.tscn`, `tools/build_rooftops.gd` (`_hay_cart`: the node); `rooftops_player.gd`
(`_enter_hay`: the burst); `tools/climb_look.gd` (`--program=hay`, `--hay_from`, `--people_side`).

## Problem

The owner: "the hay pile on the cart looks very unrealistic and off-putting." It was a smooth dome (24 by 10 faces, a
noise of straws painted on it) of one saturated yellow, its outline a balloon's.

## Change

The cart's model keeps its bed, boards, wheels and collider; its hay surface is hidden and a node on the cart
(`rooftops_hay.gd`) makes the hay at run time, seeded (the same each time):

* **The heap**: a loaf over the bed, higher than its sides (to 2.0 m), lumpy with 14 forkfuls and a fine roughness,
  spilling 0.24 m over the side and end boards and hanging down their outside to 0.2 m under their top (72 by 46
  quads); shaded as straw, darker than before (it is the shade between the straws).
* **The straws**: 26,000 thin curved blades (one `MultiMesh`, 8 triangles each — 208,000 — drawn both sides, faded out
  past 70 m), each its own length (0.12-0.34 m), width (3.5-7 mm), curl and colour (gold, pale, darker, a few
  grey-green and brown; a drier tip for some): 78 % lying along the heap every way, 22 % standing out of it, nearly
  half of those over the boards hanging down outside them as a fringe; 500 fallen round the cart.
* **The landing**: the leap of faith into it throws up 520 straws from the heap's top where the body lands.

## Measurements

| | the dome (before) | **the heap and straws** |
|---|---|---|
| geometry | 360 vertices, one surface | the heap 6,624 triangles; the straws 212,000 triangles, one draw (+ shadows) |
| built at start (the RTX laptop) | — | within the scene's load (26,500 transforms and colours) |
| the demo, the leap of faith, the cart's collider | | unchanged |

## Decision

KEEP.

## Rejected / not done

* Rebuilding the cart's model in Blender with straw cards: the straws want a seed, a count and colours tuned in the
  game, and the heap's shape its own; a node on the cart does both without rebuilding the town.
* Alpha-tested straw cards (several straws a card): the straws are thin enough to be geometry, and a blade lit as a
  stem reads better close up (the leap's landing is seen from a metre).
