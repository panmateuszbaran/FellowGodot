# 0246 — Destruction under load: the pieces' bonds carry the structure's weight, and crack

Status: **KEEP** (behind `FellowDestructible3D.strength`, 0 by default: support is connectivity as in decision 0188) ·
Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_destructible_3d.{h,cpp}` (`FellowFractureResult::prepare()`:
the touching pairs' shared points; `_build_bonds()`, `_solve_stress()`, `_settle()`, `update_structure()`,
`get_bonds()`) · Checks: `benchmarks/project/bench/stress_check.gd` (six lanes), `bench/structure_check.gd` (decision
0188's four, unchanged), the regression suite, the editor check.

## Problem

Decision 0188 made a struck static object stand in its pieces and drop what no longer reaches the ground — but support
was connectivity, not strength: a 2 m stone arm held out from a column by one piece's corner stood forever, and a hole
of any width blown in a wall's foot left the wall over it hanging in the air. Its "Rejected / not done" and the
readiness assessment's physics row: "no stress, loads or sagging". AAA destruction carries loads (Frostbite's and Red
Faction Guerrilla's structural stress, NVIDIA Blast's stress solver; in engineering, the discrete element method's
bonded particles).

## Change

* **Bonds.** When a fracture's pieces are prepared, each touching pair (decision 0188's three shared points or more)
  keeps its shared points; when the object stands in its pieces, each pair becomes a bond: the points' convex outline
  in their plane (the plane through the two farthest points and the point farthest from their line, its normal from the
  first piece to the second), its area, centre and second moments about its principal axes; a face of a line or a point
  a tiny square across the centres. Each piece's mass (its volume × `density`) at its centre.
* **`strength`** (kPa, 0 by default: support by connectivity, as before). With a strength, at each blow (after the
  pieces near it are knocked out) and at `update_structure()`, the standing structure's small displacements under the
  pieces' weight are solved with the pieces on the ground fixed: each bond a layer of elastic springs over its face
  (normal stiffness E/L and shear G/L an area, L the pieces' centres' distance along its normal) — so in tension,
  compression and shear at its centre, and in bending and torsion by its second moments — K x = f for 6 unknowns a
  piece, by conjugate gradients with a block Jacobi preconditioner (each piece's 6 × 6 block, Cholesky), started from
  the last solve's displacements. Each bond's stress is read at its outline's corners: normal (E/L)(δ·n + (φ × s)·n),
  tension positive; shear (G/L)|slip|. A bond whose tension or shear passes the strength, or whose compression passes
  ten times it, is past its limits; the one most past them cracks, the stresses are solved again, until none is (or 64
  have cracked in one settling). Then, as before, what no longer reaches the ground through the bonds that hold falls,
  each part one rigid body. Where the structure is determinate (an overhang) the forces do not depend on E; where it is
  not (a span on two sides) only the bonds' ratios do. A collapse is spread over frames: a settling stops when its
  solves have done 100 000 units of work (conjugate-gradient iterations × the bonds they run over; the next round's
  cost taken as the last one's) and goes on at the next physics frame from what stands then — the same collapse on
  every machine, at most about 5 ms of solving a frame on the cloud's CPU. The pieces' weight, and the loads a game puts on it
  (`add_load(position, force)`: on the standing piece nearest, its force and moment; `clear_loads()`) — not the blow.
* **`anchor_mode`**: Lowest Point (decision 0188's: the pieces within `anchor_height` of the object's lowest point) or
  Static Bodies — the pieces whose shapes come within 2 cm of another static (or kinematic) body when the object stands
  in its pieces: the ground, a building's wall (a balcony), the pillars a panel spans (decision 0188's "not done": a
  wall on a building's frame).
* **`update_structure()`**: a prefractured structural object stood in its pieces and settled without a blow (after its
  strength is set, to see at once whether it holds); `get_bonds()` (each bond's pieces, face, whether it holds, the
  force and moment on its second piece, its greatest tension, compression and shear, its ratio); `get_stats()` adds
  `last_cracked`, `stress_usec`, `stress_iterations`, `bonds`; `get_structure()` each piece's `mass`.

The model was checked first in a few lines of Python on four 0.5 m cubes held out as a 2 m cantilever: each bond's
stress at its top corners equal to the beam's M/W (517, 291, 129 and 32 kPa from the root), in tension, and the
stiffness matrix symmetric.

## Verification

`bench/stress_check.gd` (the editor build, the cloud's CPU; every object prefractured, 2200 kg/m³), GodotPhysics3D
and Jolt alike (the same cracks and falls; the solve does not depend on the physics engine):

| lane | expected | result |
|---|---|---|
| an L, column 1.2 × 3 × 0.5 m, arm 0.5 × 0.5 m 2 m out, 36 pieces, 5 MPa, `update_structure()` | nothing cracks; the bonds hold the arm up exactly | no crack; the arm's outer half (5 pieces, 3 bonds): forces summing to 0 within 10⁻⁷ of its weight, moments 3 × 10⁻⁷; the L's top (16 pieces, 11 bonds) 2 × 10⁻⁷, 10⁻⁷; the root's greatest tension 333 kPa, the beam's 6M/bh² 517 kPa (0.64; with a 0.5 m column 508 kPa, 0.98) |
| the same L at 150 kPa | the arm cracks at its root and falls; the column stands | 4 bonds cracked, the arm's 7 pieces one body, lying on the ground (0.25 m), 29 standing |
| the same L without a strength | as decision 0188 | nothing cracks or falls |
| a pillar 0.5 × 3 × 0.5 m, 14 pieces, 5 MPa | its pieces on the ground carry the rest | the 12 free pieces' forces summing to 0 within 10⁻⁷, moments 1.3 × 10⁻⁶; the greatest compression 65.4 kPa (ρgh 64.7) |
| a wall 4 × 3 × 0.25 m, 40 pieces, 5 MPa, a hole blown in its foot's middle | spans the hole | 5 pieces out, nothing cracks, 35 standing |
| the same wall at 20 kPa | the part over the hole falls | 5 out, 11 bonds cracked, 30 pieces fallen in 2 bodies, 5 standing |
| a balcony 2 × 0.25 × 1.5 m on a building's wall (a static box), 24 pieces, anchored to the wall, 5 MPa | holds | 5 pieces anchored (those at the wall), the greatest ratio 0.135; root tension 674 kPa (a beam's 1 035), all standing |
| the same at 300 kPa | breaks off at the wall | 10 bonds cracked, 19 pieces one body on the ground, the 5 at the wall standing |
| the same at 3 MPa, then 4 t at its edge (`add_load()`) | holds itself, not the load | its own weight: the greatest ratio 0.23, nothing cracks; with the load 10 cracked, 19 fallen |
| a wall 8 × 4 × 0.3 m, 128 pieces, 20 kPa, a hole in its foot | the collapse spread over frames | 7 out; 96 bonds cracked and 103 pieces fallen over 22 frames, 18 standing; a frame's work at most 109 691 (one round alone past the budget), its solves 2.5-9 ms on the cloud's CPU |

The solves: 24-36 pieces 64-125 iterations, 0.1-0.8 ms on the cloud's CPU; a settling that cracks the 40-piece wall
1.9-3.8 ms. Decision 0188's `structure_check.gd` (strength 0): its four lanes as before on both engines (38, 11 and 0
standing; the pillar's upper part at 0.24 m on GodotPhysics3D, 0.26 m on Jolt). Lavapipe: the lanes filmed (the weak
L's arm on the ground, the strong wall spanning its hole, the weak and the loaded balconies fallen), the same results
as without a picture; the regression scene `stress` (a weak L — its 0.5 m column toppling — and a weak wall with a
hole) blessed and the same from the release template.

How it was found to work: the first weak L had a 0.5 m column and toppled whole — the column cracked at its foot, where
the arm's moment (its weight × 1.25 m) is greater than at the arm's root (× 1 m): right, and the check's column was
widened to 1.2 m to see the arm alone break off. The 128-piece wall's collapse first took 355 ms in one frame (one crack
a solve, 57 solves); cracking with the worst every bond within 75 % of its ratio brought it to 162 ms; spreading it over
frames by a time budget made the collapse depend on the machine's speed (on lavapipe a part fell a frame later than
headless) — the budget is now work, the same on every machine. The solve's tolerance, 10⁻⁶ of the weight: the forces
balance to 10⁻⁷-10⁻⁶ of it.

## Decision

Keep: a structural object with a strength carries its weight and the loads put on it, cracks where they overload it —
an overhang at its root, a span over a hole, a balcony at its wall — and drops what no longer reaches the ground; off by
default (strength 0: decision 0188's connectivity, unchanged).

## Rejected / not done

* **Support by a tree of paths to the ground** (each piece's weight passed to its parent on the shortest path, moments
  summed along it): cheap, but it splits a span on two supports into two cantilevers and cannot tell which of two
  supports carries what; the elastic solve shares the load as the bonds' stiffness does.
* **Cracking every overloaded bond at once**: the load redistributes after the first crack (the neighbours of a cracked
  root bond take its share, or the part falls and unloads them); the worst first, solved again, is what a progressive
  collapse does.
* **Loads found by themselves**: what rests on the structure is the game's to add (`add_load()`), not read from the
  contacts of bodies on it; the blow's force through the structure and dynamic loads are not counted.
* **Pieces that crack but stay** (a sagging beam, a wall that leans): a cracked part either still reaches the ground
  through the bonds that hold, unchanged, or falls whole.
