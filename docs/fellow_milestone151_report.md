# FellowEngine — milestone 151 report (climbing from hold to hold; the rooftops' people, photographs and streets)

Status: **FINAL** (2026-11-19). Branch `fellow/geometry`. Everything here ran on the CPU and lavapipe in the cloud. The
owner has paused the GPU: `f510` and `f511` are still waiting in `.tools/wip`.

## 0. Result

The owner watched milestone 150's video and named five problems. This milestone answers each one:

| the owner's words | what changed |
|---|---|
| "climbs like Spider-Man instead of looking for holds to grab" | Climbing goes from hold to hold (decision 0282); there is no free climbing in the showcase |
| "materials on the buildings and roofs reflect badly" | Photographic materials; specular 0.2-0.25, rough; no screen-space reflections |
| "far from Assassin's Creed's graphics" | Poly Haven's photographs, SDFGI, people, a ring of houses round the square |
| "jumping off the cart, the character ends up under the ground" | The clip's root motion is read on the physics ticks; the body is kept on the street |
| "the world's scale, the distances, the empty streets" | About 40 people; the square closed by houses; streets lined to the city's blocks |

* **Climbing from hold to hold** (decision 0282, `FellowTraversal3D`, opt-in). Two new settings:
  * `hang_reach_up`: hanging, a ledge within reach is climbed to hand over hand (`TRANSFER_REACH_UP`, 0.75 s).
    * The hand that followed last time leads, out from the wall on its way up.
    * The body is pulled up while the other hand follows.
    * The feet step up the wall in turn.
    * A ledge further up is still leapt to.
  * `hang_brace_height`: the feet are braced on the wall found under the ledge.

  Behind those settings:
  * Where a hold below stands out more than the ledge (a corbel under the tower's floor, a string course under a
    corbel), the body hangs out from the ledge, up to 0.32 m. It keeps that distance over the ledge's top, so the
    mantle does not go through the hold below.
  * Pushing up climbs onto what the hands hold, not onto a course under it.
  * `hang_transfer_started` tells the controller which clip to play.
* **The showcase is climbed on its holds.** The holds are part of the buildings' collision:
  * the window sills, on brackets;
  * the hoods over the plain windows and the piano nobile's cornices;
  * an impost band at the arches' springing on the ground floors (2.2 m, the first hold from the street);
  * the rustication's top ledge and the string courses;
  * on the bell tower, a column of holds under its right arch: corbels in the rustication and the lower shaft, beams
    over old putlogs higher up, 1.1 m apart. A leap reaches each course.

  The mannequin has seven new clips: hanging braced (the knees bent to the wall), shimmying braced, reaching up with
  either hand, the leap up and the leap to either side. The player sprinting at a wall jumps for its first hold.
  Pushing on climbs, and where the next hold is out of reach it leaps. The demo now climbs the tower in one take, under
  three cameras: 18 reaches and 3 leaps up to the belfry in 15 s.
* **The square's people** (`scripts/rooftops_crowd.gd`, `rooftops/npc.tscn`):
  * passers-by on routes round the square both ways, across it and in and out of its four streets, stepping aside
    for the player;
  * groups talking (a new `talk` clip);
  * people sitting on the palaces' stone benches (a new `sit` clip; Blender marks the seats);
  * the stall keepers.

  Each one is the mannequin in a tunic's colour with darker joints; some wear a long robe, some a beret or a hat. About
  40 people in all.
* **The ring of houses**: nine copies of the square's houses, their meshes shared, close the square's west side and
  line the streets before the city's blocks. The Skyline model leaves that ring empty.
* **Photographic materials**: 12 sets from [Poly Haven](https://polyhaven.com) (CC0, 2k: colour, OpenGL normal map,
  ARM). They cover plaster tinted to each house's colour, the stones, the rustication, brick, roof tiles, wood,
  shutters and the ground by zone.
  * `tools/fetch_textures.py` downloads them into `rooftops/textures/`, outside the repository, and writes their
    import settings.
  * `facade_pbr.gdshader` samples each texture twice against tiling: shifted and turned for plaster, not turned for
    laid blocks, bricks, tiles and boards. A tint is matched to the photograph's mean brightness. It keeps the
    procedural weathering: drifting tone, rain streaks, dirt at the foot and fallen plaster showing the bricks.
  * The normal maps need no tangents (a cotangent frame).
  * `paving_pbr.gdshader` covers the ground by zone: the square's slabs, paler and larger, the cobbles round the
    fountain, the pavements and the streets.
  * The scene swaps the photographic materials in for the procedural ones at start-up when the textures are there
    (`scripts/rooftops.gd`). Without them it stays procedural and complete.
* **The light**: no screen-space reflections, SDFGI for the light bounced into the shade, the sun higher (24°) from
  the south-west over the new houses.
* **The hay**: the `AnimationTree` advances on the physics ticks. At 30 frames a second with 60 physics ticks, the
  root motion of the way out of the hay was applied twice, putting the body under the street.

## 1. Checks (cloud)

* **`climb_check.gd`, the editor build:**
  * The new `hold_climb` lane: caught at 3.7 m, a reach, a leap of 1.6 m, then a reach to the top with the body out
    past the corbel below. Over the top at 7.40 m. No frame of the capsule was in the wall; before the mantle kept the
    body out, 14 frames went through the corbel.
  * The 28 earlier lanes: milestone 150's script with this build gives milestone 150's results value for value.
  * The longer ground in the new script moves `climb_course` and `ladder_up` 0.02 s sooner.
  * On Jolt, all 29 lanes pass.
* **Regression suite:** the scenes `climb`, `ledge_corners` and `traversal` are exact.
* **The class reference:** `FellowTraversal3D`'s new members, signal and constant are documented. Their order matches
  `--doctool`. `xmllint` and `make_rst.py` give no warning.
* **The rooftops headless:** the whole demo (54 s) runs through:
  * the run and the jump;
  * the climb to the belfry and the walk through it out along the beam;
  * the perch, the leap and the hay;
  * the way out of the hay, on the street;
  * the walk away across the square.
* **`fellow_agent.py check` on the showcase:** every rooftops scene and script loads and runs. The two failures are
  the old ones: the valley's neural character without its external files.
  * The crowd's clothes once gave two "material is null" errors from the dummy renderer at exit. The crowd now holds
    them until the people are freed.
* **Renders** (lavapipe): the photographic plaster before and after its tint was matched to the photograph's mean
  brightness (it had come out dark brown). The rustication with and without the turned second sample: the turn put
  the blocks' joints on a slant, so it is now off for laid textures. The square with its people and its ring of houses.

## 2. Next

The GPU, when the owner allows it: `f510` and `f511`. On the CPU:

* holds for one hand (a brick, a beam's end): AC's narrow holds;
* sideways reaches between holds;
* the people's clothes as cloth (`FellowCloth3D`), and faces;
* climbing round an eave onto the roofs.
