# FellowEngine — milestone 76 report (cloth on bones)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0184-cloth-on-bones.md`.

## 0. Result

* **`FellowCloth3D.skeleton_path`, `pin_bones`**: a cloth's pinned particles follow a skeleton's bones — each bound to
  the nearest allowed bone when the cloth is built or reset, kept in that bone's space, interpolated between the
  frames' poses over the substeps: a cape on a character's chest turns and bends with it; the body's capsules on its
  bones (`BoneAttachment3D`s) keep the cloth out.
* **Checked**: a cape on a twisting chest of a body circling — the pins within 0.02 mm of their places on the bone, no
  particle in the body; the cloth's other ten lanes as before; a regression scene.

Also in this milestone (destruction, decision 0182): fractures are low-priority tasks on the worker pool.

## 1. Development

* `modules/fellow_fluid/fellow_cloth_3d.{h,cpp}`: the binding, the pins' places each frame, the substeps' pins, the
  configuration warnings; `doc_classes/FellowCloth3D.xml`.
* `benchmarks/project/bench/cloth_check.gd`: the lane `cape_bones`; `tools/fellow_regression`: the scene `cape`.
* `modules/fellow_world/fellow_destructible_3d.cpp`: the fracture task's priority.

## 2. Verification

* The cloth lanes on the editor build (cloud) with GodotPhysics3D and Jolt: all as expected (decision 0184).
* Lavapipe: the lanes filmed; the regression scenes `cape` and `cloth` exact, the release template's the same.
* The laptop (f395, release template): the cloth lanes with both engines as on the cloud (the pins 0.02 mm off their
  bone), filmed on the RTX; the destruction's crates measured again with the low-priority fractures (decision 0182:
  the frames while 256 crates break 8-15 ms, were 19-86 ms).

## 3. Measurements

The cape (221 particles, 13 on the bone): 0.22 ms of a worker thread a frame on the cloud (editor build); on the laptop
0.25 ms with GodotPhysics3D, 0.24 ms with Jolt (release template).

## 4. Next

Self-collision and tearing; a garment from a skinned mesh's own vertices.
