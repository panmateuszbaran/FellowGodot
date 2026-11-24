# 0332 — Weathering: years of weather on a scene's walls, from their shape seen from above in layers

Status: **KEEP** — `FellowWeathering3D` (new, module `fellow_fluid`; nothing changes without it); the showcase's rooftops
weather their buildings · Branch `fellow/geometry` · Code:
* `servers/rendering/renderer_rd/effects/fellow_weather.{h,cpp}` (`WeatheringState`, the weather block's six vec4s and
  each layer's centre, the layers' 2D array texture), `storage_rd/particles_storage.{h,cpp}` (a heightfield drawn into
  a layer of that array), `forward_clustered/render_forward_clustered.cpp` (the array at set 1's binding 55, in the
  camera's passes only; the weather's specialization while weathering), `shaders/forward_clustered/
  scene_forward_clustered{,_inc}.glsl` (`fellow_weathering_apply()`, before the weather's wetting, colour passes);
* `modules/fellow_fluid/fellow_weathering_3d.{h,cpp}` (new): the node, its sixteen heightfields;
  `doc_classes/FellowWeathering3D.xml`; the editor's FellowEngine menu: **Add Weathering**;
* `showcase/tools/build_rooftops.gd`, `showcase/rooftops/rooftops.tscn` (render layer 5 on the buildings, the fountain,
  the pavements and the paving; the node, its streaks darker and longer), `showcase/tools/weathering_look.gd` (new:
  the rooftops on and off, a material's parameter set, the weathering in debug colours);
* `showcase/rooftops/materials_photo/stone_rustic.tres` (the ground floors' rusticated stone: below);
* `tools/fellow_regression/project/scenes/weathering.tscn` (new scene of the suite).

Checks: lavapipe only (no GPU this milestone, the owner's choice): a test house and the rooftops before and after, the
regression suite (every other scene unchanged), the class reference. The GPU's cost is not measured.

## Problem

The owner asked for the road to AAA to go on, without the GPU. After rooms behind the windows (decision 0330) the
rooftops' buildings still read as new: every wall clean from its eaves to the street, the same plaster under a sill as
beside it. A real town's walls are drawn by the weather — dark streaks hanging from every sill, hood and cornice where
the rain runs off, damp rising from the street with a tide line and salt, a patina — and an AAA city has them (painted
masks, decals and vertex colours, an artist's days per street). FellowEngine leaves content pipelines for large teams
out (the owner's decision): the weathering should come from the geometry itself.

## Change

* **`FellowWeathering3D`**: a node (one at a time) whose `cull_mask` names the render layers weathered. In the colour
  passes (Forward+), after a material's own code, before the weather's wetting, the albedo of every opaque surface of
  those instances is multiplied (metallic ones less):
  * a **patina**: two octaves of noise over the surface's own plane (about 3 m and 0.8 m), `patina`;
  * **streaks** on walls (normals within 53° of the horizon): the nearest ledge above the point — a surface in the
    column 1.2 texels outside the wall — and the fall from it in `streak_length`s; lanes four a metre, wavy, 55 % of
    them running, each its own width (tapering down), length (0.4-1.6 times), darkness and pulses down its length, soft
    edges; a broad faint wash a metre across; the ledge's own dirt right under it; and **a ledge's ends** — within 10
    and 20 cm of where the ledge in the column stops along the wall — a darker, longer streak (the water runs off a
    sill's corners); under **a short ledge** (a sill or a hood: it stops within 80 cm either way) a mottled curtain of
    grime its width; none under a mass that rises on (a recessed window under the wall above, a jetty: sheltered);
  * **damp** at a wall's foot: the ground the lower of what lies 0.5 and 1.5 m out (a kerb is the street's; a
    balcony's floor is not — beyond its edge nothing within the damp's reach), darker towards the foot up to a tide line
    `damp_height` × 0.75-1.25 along the wall, ragged at two scales, a broken band of salt above it, the splashes' grime
    in its lowest 35 cm.
  Each pattern fades to its mean where it is finer than the pixel's footprint (TAA: decision 0331).
* **The scene's shape in layers**: one heightfield sees only the highest surface over a point, so a cornice hides every
  sill under it. The node keeps a stack of `heightfield_layers` (16) particle collision heightfields of its own (seen by
  no particles: their `cull_mask` 0; drawn from the instances in its `cull_mask`), each `heightfield_layer_height`
  (1.75 m: half a storey keeps a window's sill and the hood over it apart) tall with 10 cm of overlap below it, from the
  node's height up, `heightfield_size` (64 m) wide around the camera at `heightfield_resolution` (1024). A
  heightfield's camera is orthographic from its box's top, so each layer holds its own highest surface: the shader reads
  a texel as empty (cleared), a surface, or solid (a mass through the layer's top). A wall's point looks up from its
  own layer to the first layer that is not empty in its column (as far as five streak lengths): a surface there is its
  ledge; a mass that rises on shelters it. The ground looks down the same way.
* The layers follow the camera in sixteenths of their width (whole texels: nothing shifts), **one layer a frame** (each
  move is a depth pass over the town), each with its own centre in the weather block meanwhile; all at once when the
  camera jumps (a quarter of their width). The heightfields draw into the layers of one 2D array texture
  (`ParticlesStorage::fellow_particles_collision_set_heightfield_target()`: a slice of it instead of a texture of their
  own), owned by the weather's renderer and freed while the node is off; it is bound at set 1's binding 55, only in the
  camera's passes (the secondary passes include the heightfields' own: binding 50's feedback loop, milestone 52), and
  read with the layer as a coordinate.

## The rusticated stone (the owner, during the milestone)

"Why has this stone more shadow than texture? It doesn't look nice." The ground floors' rusticated sandstone
(`materials_photo/stone_rustic.tres`, Poly Haven's `sandstone_blocks_05`) rendered dark brown and blotched where its
photograph is a pale, calm stone. Each of its relief's terms off in turn on the rooftops (lavapipe, two views): the
parallax and the joints' darkening — nothing; the stains, the street's dirt and the tone's variation — nothing; the
normal map 0.5 → 0.2 — the blocks pale and calm. The photograph's normals are rock-faced (31° from the surface's on
average; the bricks' 15°, the paving's 7°): at 0.5, under the golden hour's low sun, over a colour softened by 0.6 (to
hide the light baked into the photograph), every block was drawn as its shading, a dark half to every bump. Now
`normal_strength` 0.22 and `albedo_soften` 0.3: the stone's colour shows, the relief stays in the joints and under a
grazing sun.

## Measurements

Lavapipe only; the GPU's cost is not measured (no GPU this milestone). What the defaults hold: sixteen layers of
1024² 32-bit depth, 64 MB; a wall's pixel reads up to about a dozen texels of them (nearest), plus the noise.

* The test house (`base/weathering`, 512x288 in the suite; 960x540 looked at): streaks under the six sills, the
  balcony and the cornice, darker under the sills' ends; damp to a tide line on the stone base and none on the wall
  over the balcony; the crate outside the weathering's layers untouched.
* The rooftops (`weathering_look.gd --lite=1`, 960x540: the square's buildings alone — the whole town outgrows
  lavapipe's 6 GB): streaks under the sills, hoods, string courses and cornices, damp at the walls' foot (seen in
  `--debug=1`'s colours); the showcase's node darker and longer (`stain_color` (0.42, 0.39, 0.35), `streak_length`
  2 m) than the defaults over its busy photographs.
* The regression suite (40 pictures, every group, FellowEngine off and on): every other scene exact.

## How it got there

* One heightfield (the rain's, or the node's own): the test house's cornice hid its sills — streaks only from the
  cornice, the same everywhere below it — and the balcony's floor counted as the ground (a damp band on the wall above
  it). Stacked heightfields instead: layers of 3 m showed the sills.
* Lanes six a metre, every one running, hard-edged: a barcode under the cornice. Four a metre, about half running, soft
  edges, a broad wash and the ledges' ends: the streaks read as a sill's.
* Layers of 1.75 m: the cornice's streaks stopped at a layer's boundary (only the layer above had been searched) and the
  balcony's damp came back (the street 1.5 m out was two layers down). The search goes up to five streak lengths and
  down through the damp's reach, and a column empty all the way down is "far below", not "unseen".
* The layers moved all in one frame: sixteen depth passes over the rooftops' town whenever the camera crossed four
  metres; one a frame now.
* On the rooftops' photographed stone the lanes alone hardly showed (the debug colours showed them there, under every
  sill, hood and cornice): a curtain under short ledges, and the showcase's streaks darker and longer.
* Sixteen textures bound as an array of descriptors, read through a `switch` of constant indices (the layer differs
  from pixel to pixel; non-uniform indexing is not required of Godot's Vulkan devices): every lookup inlined sixteen
  ways, and lavapipe's LLVM ran out of memory compiling the rooftops' pipelines. One array texture instead.

## Decision

KEEP: `FellowWeathering3D`; the rooftops weathered (their buildings, fountain, pavements and paving: render layer 5).

## Rejected / not done

* Painted masks, decals, vertex colours: an artist's work per building (out of scope: content pipelines).
* The rain's heightfield (FellowRain3D's shelter): one layer.
* Depth peeling a single heightfield (every surface over a point): the heightfield's pass draws one depth; sixteen
  layers draw the stack with the existing pass.
* Indexing sixteen textures per pixel (`nonuniformEXT`: not required of Godot's Vulkan devices, only on macOS) or
  through a `switch` (above): one array texture, the layer a coordinate.
* Weathering's normal, roughness or wetness (streaks are dirt: albedo only); the roofs' own streaks and moss; the cost on
  the GPU — measured when the GPU is back.
