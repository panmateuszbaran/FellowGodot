# FellowEngine — milestone 124 report (hair of strands)

Status: **FINAL** (2026-11-16). Branch `fellow/geometry`. The readiness assessment's characters row said "no hair".
Milestone 124 was done without the GPU (the owner's: CPU and lavapipe only); the GPU's cost of the strands is to be
measured on the RTX.

## 0. Result

* **`FellowHair3D`** (decision 0248): under a head's `BoneAttachment3D`, hair of strands on a sphere's cap — 160 guide
  strands simulated on the CPU (lengths exact, a pull back to the styled shape fading to the tip, gravity, damping, the
  scene's `FellowWind3D`, the scalp's sphere and sphere or capsule colliders such as the shoulders), 10 000 strands
  drawn between their three nearest guides on the GPU as ribbons facing the camera, at least a pixel wide (the coverage
  of thinner ones dithered for TAA or FSR to resolve), with a hair model: Kajiya-Kay's diffuse, a narrow highlight in the
  light's colour and a broader one in the hair's, light from behind, darker roots and under-strands.
* **Motion vectors** follow the strands: a guide texture of this frame's and the last frame's halves, each strand's
  instance data naming this frame's; Godot's previous-frame pass reads the previous instance data, so the other half.
* "Add Hair" in the FellowEngine menu; the editor shows the styled shape.

## 1. Checks

`bench/hair_check.gd` on lavapipe (TAA, 960 × 540): long brown hair combed back over the crown, over the shoulders,
swinging behind the head as it turns 70° and back, moving 3.6 cm on average at its tips in a 6 m/s wind; no segment
longer or shorter than its rest (0 %), nothing nearer the head's centre than the scalp and its margin (0.110 m); the
motion vectors' debug view marks the strands as moving. 160 guides: 0.33 ms of simulation and 0.06 ms of upload a
frame on the cloud's CPU (the release template, headless; 0.73 ms before the constraint pass was made one pass with
squared distances). The regression scene `hair` (37 + 1 scenes: the same from the
release template); the class reference; the editor check.

## 2. Next

The strands' GPU cost on the RTX; a scalp from a mesh (roots on painted faces) and grooming (part lines, styles); the
hair's self-shadowing beyond the light's shadow maps (deep opacity); the guides on worker threads for crowds.
