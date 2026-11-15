# FellowEngine — milestone 122 report (destruction under load: bonds that carry the weight, and crack)

Status: **FINAL** (2026-11-15). Branch `fellow/geometry`. Decision 0188 (milestone 80) made a struck static object
stand in its pieces and drop what no longer reaches the ground; support was connectivity, not strength — the readiness
assessment's physics row: "no stress, loads or sagging".

## 0. Result

* **Stress** (decision 0246, `FellowDestructible3D.strength`, off by default): the faces where a structural object's
  pieces touch are elastic bonds (their area and second moments: tension, compression, shear, bending, torsion); at each
  blow and `update_structure()` the structure's displacements under its weight are solved (6 unknowns a piece,
  conjugate gradients) and the bonds past the strength crack, the worst first, solved again after each round; what no
  longer reaches the ground falls whole. An overhang breaks off at its root, a span over a hole falls, a balcony gives
  way at the wall.
* **Exact where it can be checked**: the bonds' forces hold any free body of the structure up to 10⁻⁷ of its weight
  (forces and moments); a pillar's foot carries 65.4 kPa in compression (ρgh 64.7); a cantilever's root tension 508 kPa
  against a beam's 517 (0.98) — with the model checked first in Python against beam theory to the kPa.
* **Loads and anchors**: `add_load()` (4 t at a balcony's edge breaks the balcony that carries itself); `anchor_mode`
  Static Bodies — pieces touching another static body (a building's wall) stand on it.
* **Collapses over frames, the same everywhere**: a settling stops after 100 000 units of solver work (iterations ×
  bonds) and goes on at the next physics frame — a 128-piece wall's collapse over 22 frames, at most about 5-9 ms of
  solving a frame on the cloud's CPU, instead of 355 ms in one; deterministic (a time budget was not: lavapipe's slower
  frames changed when a part fell).

## 1. Stress (decision 0246)

| lane (the cloud's CPU; GodotPhysics3D and Jolt alike) | result |
|---|---|
| L (column 1.2 m, arm 2 m), 5 MPa | nothing cracks; free bodies balanced to 10⁻⁷-3 × 10⁻⁷; root tension 333 kPa (beam 517) |
| the same L, 150 kPa | 4 bonds cracked, the arm (7 pieces) on the ground, 29 standing |
| the same L, no strength | as decision 0188 |
| pillar 0.5 × 3 m, 5 MPa | compression 65.4 kPa at the foot (ρgh 64.7) |
| wall 4 × 3 m with a hole, 5 MPa / 20 kPa | spans it / 11 cracked, 30 pieces fall |
| balcony on a wall, 5 MPa / 300 kPa | holds (ratio 0.135) / breaks off at the wall (19 pieces) |
| balcony, 3 MPa, + 4 t at its edge | holds itself (0.23); with the load 10 cracked, 19 fall |
| wall 8 × 4 m, 128 pieces, 20 kPa, a hole | 96 cracked, 103 fallen over 22 frames, 18 standing |

A solve of 24-36 pieces: 64-125 iterations, 0.1-0.8 ms. Found on the way: an L with a 0.5 m column topples whole (its
foot's moment is greater than the arm's root's — right); one crack a solve made a big collapse 355 ms; cracking with
the worst all bonds within 75 % of it, then spreading the rest over frames by work, not time.

## 2. Checks

`bench/stress_check.gd` (ten lanes) on both physics engines and filmed on lavapipe (the same results); decision 0188's
`structure_check.gd` unchanged on both; the regression suite (37 scenes with the new `stress`, the release template
against the editor build); the class reference matches the binary (`--doctool`), `make_rst` without a warning; the
editor check.

## 3. Next

The parts fall whole: no breaking on landing (a wall's upper part dropping a metre stays a slab), no sagging; loads are
the game's to add, not read from the bodies resting on the structure. Rigid objects (not static) still break whole.
