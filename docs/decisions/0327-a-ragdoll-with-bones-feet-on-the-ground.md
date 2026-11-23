# 0327 — A ragdoll with bones, feet on the ground: anatomical joints and masses, soles, grounded feet

Status: **KEEP**, on by default — `FellowPhysicalAnimation3D.anatomical_limits` (on; off: milestone 166's cones and
masses by volume), `FellowMotionMatcher3D.foot_grounding` (on) · Branch `fellow/geometry` · Code:
* `modules/fellow_animation/fellow_physical_animation_3d.{h,cpp}`: `_classify_joints()`, `_anatomical_masses()`,
  `_sole_shapes()`, `_limp_joint()`, `_hinge_flexion()`, `_hip_flexion()` (new), `_rebuild_joints()`, `build()`; `anatomical_limits`,
  `get_joint_angles()`;
* `modules/fellow_animation/fellow_motion_matcher_3d.{h,cpp}`: `foot_grounding`, `get_stats()`'s `ground_shifts_mm`;
* `modules/fellow_animation/fellow_motion_database.{h,cpp}`: `get_foot_ground_height()` (new);
* `scene/3d/physics/physical_bone_3d.h`: `get_joint_rid()` (C++, one line, marked);
* `benchmarks/project/bench/balance_check.gd` (the joints' angles and gaps), `scenes/matching_lab.gd` (the
  mannequin's soles, `--matching_grounding`).

Checks: `balance_check.gd` and `physical_check.gd` on Jolt Physics (headless), Matching Lab's crowd
(`matching_eval.gd`), the character filmed on lavapipe. No GPU.

## Problem

The owner, of milestone 184's film: "now the feet hang over the ground, and the ragdoll folds the body as if it were a
soft body with no bones".

* **No bones**: a limp body's joints were all milestone 166's cones, 50° of swing and 30° of twist about the pose it went
  limp in. The trunk's five segments could fold it double (the lower back to the neck: up to 170°); a knee or an elbow
  bent sideways and backwards (knees to −23°, elbows to −56°, a knee folded past 180° in a fall back); the pelvis's own
  short hip bones and the collarbones swung the legs and arms off their sockets. And the masses went by the capsules'
  volumes: the neck as thick as the trunk (13 kg), the head 0.19 kg, a spine segment of no length 0.08 kg, a foot 0.23 —
  bodies a hundred times lighter than their neighbours, which Jolt's joints let stretch: they opened 40-86 mm as the
  ragdoll landed.
* **Feet over the ground**: the recordings' floors differ. Of the feet the motion matcher held down in Matching Lab's
  crowd, the toe stood 1.3-7.8 cm up (5th to 95th percentile), half of them 2-5 cm higher than when they are down; the
  mannequin's foot was a bar from the ankle to the ball, its heel 4 cm over the ground even then; and the physical
  foot, a 2.5 cm capsule round that bar, stood 1-2 cm up (a ragdoll's lay on it, its ankle sunk to the ground).

## Change

* **The body's parts** (`anatomical_limits`, on), found from the skeleton: the trunk (the root to the topmost bone),
  its last two bodies the neck and the head; the limbs (the branches off the trunk), each followed down its deepest
  bodies, the two longest neighbours the upper and lower limb, the ones before them the girdle's (a pelvis's hip bone,
  a collarbone), the ones after a foot's or a hand's; a limb reaching below the root is a leg; the way it faces from the
  feet's bones.
* **Joints as the parts bend** (limp): six-degree joints about the pose it went limp in, X down the bone (twist):
  a spine segment 12° (twist 6°), the neck and the head 30° (35°), a girdle bone 6° (5°), a shoulder 80° (50°), an
  ankle 30° (12°), a wrist 35° (20°); a knee and an elbow hinges about their axis (across the upper limb and the way the
  body faces, carried by the upper limb's bone): from 3° backwards to 145° bent, measured from straight whatever they
  are bent at when it goes limp, 4° off the hinge, twist 8° (a forearm's 45°); a hip flexed from 15° back to 120°
  forward of the thigh's way at rest (about the axis across it and the way the body faces, carried by the pelvis),
  abducted 35°, twist 30°. And each joint's friction (a motor held at rest, its torque limited): 15 % of the torque of
  the bodies hanging from it held out at its length — too little to hold a limb up, enough to stop it swinging on.
  (`PhysicalBone3D` keeps its joint's RID to itself: a one-line getter, `get_joint_rid()`, marked.)
* **Masses as a person's** (de Leva 1996, after Zatsiorsky): the head 5.6 %, a thigh 14.2 %, a shank 4.3 %, a foot
  1.4 %, an upper arm 2.7 %, a forearm 1.6 %, a hand 0.6 %, a girdle bone 2 %, the trunk the rest (by its segments'
  lengths, the root three times), nothing under 1.5 %. For CMU's skeleton: the hips 10.7 kg, a thigh 9.9, a shank
  3.0, a foot 1.0, the head 3.9, the spine 2.1-3.6 a segment.
* **Soles**: a foot's body a box from 5 cm behind the ankle to 4 cm past the ball, its underside a centimetre less far
  under the ankle than the database's ankles are over the ground when down (7.1 cm of 8.1), "down" the skeleton's
  rest's.
* **Grounded feet** (`FellowMotionMatcher3D.foot_grounding`, on): a foot in contact is lowered (or raised) so that the
  lower of its ankle (where the foot is held) and its toe (the animation's, from that ankle) is as high over the root's
  ground as the database's are when the feet are down — their 5th percentile over the frames in contact, worked out once
  a database (`FellowMotionDatabase.get_foot_ground_height()`, new) — springing there in 0.04 s. Matching Lab's
  mannequin got a sole too.

## Measurements

`balance_check.gd` (Jolt, 60 Hz), milestone 184 (`--anatomical=0 --grounding=0`, its numbers to the digit) → 185:

| | milestone 184 | milestone 185 |
|---|---:|---:|
| the joints' largest gap, staggers (20-160 N·s) | 23-61 mm | **0-7 mm** |
| the same, falls (200 N·s, five) | 40-86 mm | **15-27 mm** |
| a limp knee's flexion (least … most) | folded through itself (−180 … 180°) in four falls of five | **112 … 153°** |
| a limp elbow's | −26 … 96° (bent backwards) | **17 … 155°** |
| the trunk's bend at most (the lower back's bone against the neck's) | 52-75° | **39-58°** |
| the hips at 0.3 m after the push, coming down at | 0.60-0.62 s, 2.6-2.7 m/s | 0.60-0.62 s, 2.4-2.5 m/s (falling forward, onto the knees: 0.75 s, 0.6 m/s) |

* Standing in the balance scene, the feet's undersides 0-3 mm into the floor (milestone 184: the physical feet 2-5 cm
  over it at the ball).
* Matching Lab, 16 characters, 60 s, `foot_grounding` off → on: a foot held down stands over the ground (the lower of its
  ankle and its toe over their heights when down) −3.0 / −0.2 / +3.1 cm (5th / 50th / 95th percentile) → **−0.8 / 0.0 /
  +1.6 cm**; its slide in contact 0.0834 → 0.0784 m/s, on the ground 0.0366 → 0.0312, while locked 0.0264 → 0.0211;
  popping (p99) 39.3 → 39.3 mm.
* With balance off, `physical_check.gd`: the bodies 3.4 mm from the walk on average (3.7); a 40 N·s push at the chest
  knocks it 58 mm (104), 20 N·s at the head 134 mm (226) — they are 2.1 and 3.9 kg now, not 0.08 and 0.19 — back in
  0.15-0.18 s (0.27-0.28); the ragdoll at 4.1 m/s (4.4), 30 mm into the floor (52). The drive 11.5 µs a step.

## How it got there

* The hinges' limits first let a knee bend 145° further and stopped it straightening: Jolt's six-degree joint measures
  the angle the other way round (`JoltGeneric6DOFJoint3D` flips them); the limits are given flipped.
* With the joints right the ragdoll still folded: its masses were the capsules' volumes (the neck 13 kg, the head and a
  spine segment under 0.2 kg) and the joints between bodies a hundred times apart in mass stretched 4-9 cm. Shared out as
  a person's, the gaps fell to 1-3 cm.
* A fall back landed sitting with the hips flexed 90° and rolled onto its back with its legs swinging over it: a hip's
  limits about the pose it went limp in allowed 145°. A hip's flexion is now limited from the thigh's way at rest, and
  each joint has friction.
* Grounding from the animation's own ankle missed the feet held where they landed (a lock keeps the ankle while the
  foot rolls): the planted feet's spread hardly narrowed (−2.3 … +2.3 cm). From the held ankle, with the animation's toe
  from it, it did.

## Decision

KEEP, on: a ragdoll that bends where a body bends and no further, its parts as heavy as a person's (its joints held
together), and feet that stand on the ground.

## Rejected / not done

* Anatomical asymmetry for the shoulders and the hips' abduction: symmetric about the pose it goes limp in.
* A foot's toes as a body of their own.
* A foot put into the floor at a reset (a body built in the rest pose under the ground) can stay caught 6-10 cm from
  its place: only a body 0.75 m away is put back (milestone 166's rescue).
