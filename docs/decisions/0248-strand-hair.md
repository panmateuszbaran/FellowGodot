# 0248 — Hair of strands: guides simulated on the CPU, strands drawn between them on the GPU

Status: **KEEP** (a node, `FellowHair3D`: nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_fluid/fellow_hair_3d.{h,cpp}` (new), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}` ("Add Hair") ·
Checks: `benchmarks/project/bench/hair_check.gd` (lavapipe: pictures, the strands' lengths and distance from the head,
the swing of a turn, the wind, the motion vectors), the regression scene `hair`, the editor check.

## Problem

The readiness assessment's characters row: "no hair". A character in a AAA game has hair that moves with the head and in
the wind, lies on the shoulders, catches the light along its strands (Unreal's grooms: strands simulated as guides and
interpolated, rendered with a hair shading model; earlier generations' hair cards with anisotropic highlights).
FellowEngine had cloth (decision 0181) and nothing for hair; Godot has neither.

## Change

`FellowHair3D`, a `GeometryInstance3D` drawing a `MultiMesh` of strands, under a `BoneAttachment3D` on the head bone:

* **The scalp**: a sphere's cap (`scalp_radius` about `scalp_center` in the node's space, from its top down to
  `coverage`, the face's side — the node's -Z — left bare by `forehead_angle`).
* **Guides** (`guide_count`, 160): roots on a Fibonacci spiral over the cap; the styled shape combed along the scalp
  (down and back, a little out from it), then falling, relaxed at build against the sphere; `segments` (12) of `length`
  (0.32 m). Simulated on the CPU each frame in `substeps` (4): Verlet with `damping` (per second), gravity, the scene's
  `FellowWind3D` (sampled once a frame at each guide's middle) with `drag`; the roots carried by the node's transform
  interpolated across the substeps; a pull back to the styled shape in the head's space, `stiffness` × (1 − t)² (t along
  the strand); then, in one pass from the root out, each point at its segment's length from the one before, out of the
  scalp's sphere and the `colliders` (sphere and capsule `CollisionShape3D`s; squared distances first), and at its
  length again if one moved it — the lengths exact, a collider's surface left by at most a little. A move of more than 1 m in a frame (a teleport) resets the hair.
* **Strands** (`strand_count`, 10 000): roots at random on the cap, each between its three nearest guides
  (inverse-distance weights, the second and third scaled by `spread`), a share of their length (`length_variation`).
  Each strand is an instance of one strip mesh (24 quads; UV along and across); the vertex shader finds its points on a
  Catmull-Rom curve through each guide's points and blends the three, and makes a ribbon facing the camera (facing the
  light in a shadow pass), `root_width` thinning to `tip_width` but at least a pixel wide: a thinner strand's coverage
  goes to its alpha, dithered (alpha hash), which TAA or FSR resolve.
* **The look**: Kajiya-Kay's diffuse (the strand's cross-section lit from any side), a narrow highlight in the light's
  colour and a broader one in the hair's, about cones shifted along the strand (`specular_shift`: Marschner's R and TRT
  in the usual approximation), light through the hair from behind (`transmission`); darker at the roots
  (`root_occlusion`) and on a random half of the strands (the hair under the hair); the light's shadow maps on and from
  it. Colours from `root_color` to `tip_color`, varied by `color_variation`.
* **Motion vectors**: the guides' points go to the GPU in an RGBA32F texture of two halves — this frame's and the last
  one's, alternating — and each strand's instance data says which half is this frame's. Godot draws a mesh's previous
  position by running its vertex shader again with the previous frame's instance data, which names the other half: the
  strands' own motion reaches TAA and FSR.
* The editor shows the styled shape where the node is (no simulation, nothing written each frame). "Add Hair" in the
  3D toolbar's FellowEngine menu. Shaders from `FellowSharedShaders` (one per session).

## Verification

`bench/hair_check.gd` (lavapipe, 960 × 540, TAA; a head of 0.1 m on a neck, its scalp painted the hair's colour,
shoulders as a capsule collider; settled 1 s, the head turned 70° left and back over a second, then a 6 m/s wind):

* Pictures (front, side, back, mid-turn): long brown hair combed back over the crown with highlights along the strands,
  falling over the shoulders, swinging behind the head as it turns.
* Lengths: no guide segment longer or shorter than its rest by more than 0 % (first version: 5 % — the push out of the
  colliders came after the length pass; now each point is put back at its length after it).
* No point nearer the head's centre than 0.110 m (the scalp's 0.1 m and the 1 cm margin).
* The tips up to 0.23 m from their styled place during the turn; in the wind they move by 3.6 cm on average.
* The cost (headless, the cloud's CPU, 160 guides of 13 points, 4 substeps): the simulation 0.33 ms a frame in the
  release template (0.36 in the editor build), sending the guides' texture and the strands' instance data 0.06 ms. The
  first version took 0.73 ms: two constraint passes with two normalisations, a sphere test and a capsule test each
  (about nine square roots a point a substep); one pass (following the root keeps the lengths exact), squared distances
  and a second normalisation only after a collider moved the point halved it, the results the same.
* The motion vectors' debug view mid-turn: the strands marked as moving, as the head is; the shoulders and background
  not.

The regression scene `hair` (8 000 strands, the head turning) blessed, the same from the release template; the class
reference; the editor check (the preset added without a warning).

First versions: the guides started along the scalp's normal, so the crown's stood up; the highlights were near white
(the narrow lobe at 0.4, the broad one 2.5 × the colour); without TAA the dithered coverage looked sparse and noisy.

Not measured: the GPU's cost on the RTX (the GPU was not available for this milestone).

## Decision

Keep: hair that moves with the head and the wind, lies on the shoulders and is lit as hair, from one node.

## Rejected / not done

* **Hair cards** (textured strips): need authored textures per hairstyle; strands need none.
* **Simulating every strand**: the guides carry the motion; the strands are interpolated on the GPU, as Unreal's grooms.
* **The guides on the GPU**: 160 × 13 points is little work for the CPU; a GPU solver is for thousands of guides.
* **Deep opacity or occupancy shadows** (the hair's own self-shadowing): a random half of the strands darker and the
  roots darker stand in; the light's shadow maps add the rest.
* **A scalp from a mesh** (roots on painted faces): the sphere's cap fits a head; a mesh scalp is a later step, as are
  grooming tools (part lines, styles beyond combed back).
