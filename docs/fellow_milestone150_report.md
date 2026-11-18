# FellowEngine — milestone 150 report (climbing past courses and plinths; the rooftops showcase)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. Runs on the CPU and lavapipe in the cloud. The owner has
still paused the GPU: `f510` and `f511` are waiting in `.tools/wip`.

## 0. Result

* **Free climbing past what stands out of the wall** (decision 0281). Two things changed:
  * An underside, such as a string course's or a cornice's, no longer stops the climb.
  * Where the capsule does not fit at its 8 cm gap, it stands out from the face as far as it must. It moves out in
    0.08 m steps up to 0.48 m, looks 0.3 m ahead along the move, and eases back in afterwards.

  Results:
  * A wall with a 0.2 m course: climbed and mantled. The old build stopped at 0.9 m.
  * A wall over a plinth 0.15 m deep: climbed and mantled. The old build stopped at 0.78 m.
  * The showcase's bell tower, from the ground into its belfry at 26.9 m: past its plinth and four courses in 16.8 s.
* **The rooftops showcase** (`showcase/rooftops/rooftops.tscn`): a Renaissance square at the golden hour, in the
  manner of Assassin's Creed. Everything in it is made by code.
  * **Buildings, modelled in Blender** (`showcase/tools/blender`, Blender's Python): six palazzi and houses and a
    38 m bell tower with an open belfry, a bell and a beam out over the square. Also a hay cart, a fountain, stalls,
    bollards, barrels, crates and grates.
  * **The city beyond**: blocks, a cathedral's dome, towers, walls and a gate.
  * **The mannequin**: 43 bones named as Godot's humanoid profile, and 20 clips made by forward kinematics and
    two-bone IK.
    * The gait cycles plant the feet and roll the heels and toes.
    * The mantle clips carry root motion, which `FellowTraversal3D` scales onto each ledge.
    * The other clips cover hanging, climbing, the lunge, the roll, the leap of faith and the way out of the hay.
  * **Materials** are procedural shaders:
    * plaster with stains and fallen patches, masonry, coppi tiles, wood and louvred shutters, glass with rooms
      behind, hay and cloth;
    * the ground by zone: slabs crossed by travertine bands, a fan of cobbles round the fountain, pavements with kerbs,
      and fish-scale cobbles in the streets, with parallax relief and puddles;
    * the Tuscan country's patchwork round the city.
  * **Trees**: about 9 400 instances — cypress avenues, olive groves, stone pines and woods on the slopes — with farms
    on the hills.
  * **Cloth and life**: banners, awnings and laundry in the wind (`FellowCloth3D`); pigeons that burst up when someone
    runs at them; swifts; dust in the low sun.
  * **The player** runs, sprints, rolls, mantles, vaults, hangs and climbs any wall it free-runs at. It can perch on
    the beam's end while the camera swirls round, take the leap of faith into the hay and climb back out.
  * **The camera** follows on damped springs, looks ahead, pulls back while sprinting and is kept out of walls by a
    sphere cast.
  * **The demo** (`-- --demo`) is a sequence of shots and is the showcase's video.

## 1. Checks (cloud)

* **`climb_check.gd`, the editor build:**
  * On GodotPhysics3D: 28 lanes. The 26 earlier ones match the build before value for value (their JSON compared).
  * On Jolt: all 28 pass.
* **Regression suite:** the scenes `climb` and `ledge_corners` are exact.
* **The rooftops' run, headless:** the whole demo, with the events of each shot:
  * the climb past the plinth and courses, into the belfry by a high mantle;
  * the walk out along the beam, the perch, the leap and the landing in the hay;
  * the way out of the hay on the clip's root motion.
* **`fellow_agent.py check` on the showcase:** the two rooftops scenes and every script load and run. The two
  failures are the existing ones: the valley's neural character, without its external files and on the Compatibility
  renderer.
* **The class reference:** `FellowTraversal3D`'s description is updated; `make_rst.py` gives no warning.

## 2. Next

The GPU, when the owner allows it: `f510` (the hang, 40 runs with the richer checkpoints) and `f511` (decision 0272's
A/B). On the CPU:

* hanging from a cornice under an eave;
* a rooftop run with jumps between the houses for the demo;
* more of the readiness assessment's gaps.
