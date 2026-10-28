# FellowEngine — milestone 24 report (water: lakes and rivers)

Status: **in progress** (lavapipe). Branch `fellow/geometry`. Decision record `docs/decisions/0123-water.md`;
user-facing summary `docs/FELLOWENGINE_OPTIONS.md` ("Water bodies").

## 0. Result

The user asked (2026-10-28), after the far objects of milestone 23, for the look of water first: lakes and rivers with
reflections, refraction, foam at the shores, caustics, on the GPU shallow-water simulation. Milestone 1's water was a
64 m simulation tile, flat green-teal, the sky its only reflection, ending at the tile's edge.

`FellowWater3D` (module `fellow_fluid`) is a body of water to the horizon:

* **wind waves** simulated on the GPU after Tessendorf (JONSWAP for the wind and fetch, FFT in three cascades of
  160 / 27 / 5 m), matching NumPy to half-float precision;
* the **river's simulation** drawn as part of the lake: its surface, foam and flow, the waves carried by the flow;
* **reflections** traced in screen space (the far shore, the pier, the boat), the sky where nothing is hit;
  **refraction**, **absorption and in-scattering** (a temperate lake's green, shadows in the water's body),
  **foam** at shores and objects, **caustics** from the waves' own slopes, the sun's **glitter path**.

And an exact optimization: the screen texture's copy builds its mip chain only when a material samples it.

## 1. Development

* **Water Lab** (`--scene=water`): a lake (330 x 270 m, up to 9 m deep) in wooded hills, a sandy beach with a pier
  and a boat in the south, a rocky shore in the north, a river from a culvert past two boulders and a buoy into the
  lake (simulated, 96 m square). `--water=basic` is what a Godot project would use (the milestone-1 surface on the
  river, a StandardMaterial3D plane with refraction on the lake), `--water=none` the scene without water.
* The FFT first disagreed with NumPy in its last cascade (15 % of the height's spread): the Nyquist row and column
  have no mirror wave number on the grid, so the fields packed two per complex transform were not real. Left empty.
* First image: a swimming pool (cyan); the far water dark (rays below the horizon reflected the IBL's ground; the
  screen-space rays missed the far shore); the near surface like crumpled foil (a 128-texel test grid); the river's
  waterline a staircase (the simulation's dry cells). Fixed in turn: a lake's absorption (red and blue), rays bounced
  back above the horizon and the screen's view of distant scenery as the fallback, 256 texels, dry cells at a wet
  neighbour's level.
* The sun's glitter path vanished once the far water reflected the screen (Godot's SPECULAR scales the lights'
  highlights with the IBL): the highlight moved to the material's `light()`, independent of the IBL's scale.

* The waterline still climbed the bank cell by cell: bilinear sampling mixed the dry cells' surface (2 cm below
  their bed) into the level. The surface kernel now also writes the level extended one cell beyond the wet cells,
  and the surface interpolates that (the first try raised the solid cells, the walls and boulders at 50 m, with it:
  a wall of water; they keep their own rule).
* TAA softens moving ripples under a still camera (the bottom's velocity, zero, is the water's): visible against no
  anti-aliasing, but TAA also removes the ripples' sparkling. Left for later (a reactive mask from the depth the
  water writes against the opaque depth).

## 2. Verification

* Off: Fluid Lab (milestone 1's water: the simulation's surface kernel writes the flow now) bit for bit the images
  of `6f091cd`.
* The screen copy's mips on demand: Water Lab (FellowWater3D and Godot's water) bit for bit with the option on and
  off and against the build before; Fluid Lab the same.
* The waves against NumPy (`water_check.gd`, `waves_compare.py`): worst relative error 9 x 10⁻⁴ (half floats).

## 3. Performance

lavapipe (480 x 270): see 0123 — FellowWater3D 24-149 ms of GPU time more than Godot's water per view, the waves'
compute about 70 ms more per frame (lavapipe's compute is slow). RTX below.

## 4. Left for later

* Underwater views (the surface from below, fog), rain on the water, shore waves breaking.
