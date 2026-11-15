# FellowEngine — milestone 123 report (the parts that fall break where they land)

Status: **FINAL** (2026-11-15). Branch `fellow/geometry`. Milestone 122 made a structural object's bonds carry its
weight and crack; what it dropped still landed whole — its report's first "next".

## 0. Result

* **`landing_break_speed`** (decision 0247; off by default): the parts of several pieces a structural object drops are
  watched until they rest; one whose speed changes in a step by more than the setting (less gravity's) breaks where it
  struck — the pieces near the contact each a body, the rest split along the bonds that hold, each group watched in
  turn. A toppled pillar lies in pieces where it struck the ground, a balcony fallen 3 m breaks at the edge it landed
  on; at a higher setting both lie whole. The same on GodotPhysics3D and Jolt.
* A speed, not an impulse: a part's mass does not decide whether it survives its landing.
* An emptied object's body waits (no shapes, no meshes) until the parts it dropped have landed: it used to be freed at
  once, and the node watching them with it.

## 1. Checks

`bench/landing_check.gd`: the pillar at 3 m/s — 1 landing, its 10-piece part in 5 bodies; at 0 — whole; the balcony at
3 m/s — 1 landing, 7 bodies from 19 pieces; at 10 m/s (it lands at about 7.7) — whole. Filmed on lavapipe. The
regression scenes `destruction`, `structure`, `stress` as their goldens from both builds; the class reference matches
the binary; the editor check.

## 2. Next

The landing's force is not carried through the part's bonds (the pieces near the contact break off, the rest splits
along its bonds); rigid destructibles (not static) still break whole on their first blow.
