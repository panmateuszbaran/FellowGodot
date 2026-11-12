# FellowEngine — milestone 92 report (the highs blocked apart)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0200-occlusion-of-the-highs.md`.

## 0. Result

* **Transmission by frequency**: `fellow_occlusion_highs` on a collider blocks the highs apart from the level — a
  curtain (0.1, 0.8) dulls a noise source to 0.03 of its highs while lowering it 11 dB, glass (0.3) lowers it 7.6 dB
  and keeps 0.22 of its highs.
* **Checked**: a curtain lane on GodotPhysics3D and Jolt and in the mix under Movie Maker, the release template the
  same; the earlier acoustics lanes unchanged.

## 1. Development

`modules/fellow_world/fellow_acoustics_3d.{h,cpp}`, `doc_classes/FellowAcoustics3D.xml`;
`benchmarks/project/bench/acoustics_check.gd` (a curtain lane, listened too).

## 2. Verification

Decision 0200's table. The editor check and the full regression suite ran on the builds of milestones 91 and 90 (this
change touches only the acoustics node, in no regression scene). The laptop: not run (not linked to the session).

## 3. Measurements

As decision 0199: a ray more for each partial collider on a path.

## 4. Next

Filters by material (bands), diffraction paths and portals.
