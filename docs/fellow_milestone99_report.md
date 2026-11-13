# FellowEngine — milestone 99 report (sound through openings)

Status: **FINAL** (2026-11-13): the owner said "ok kontynuuj". Branch `fellow/geometry`. Decision record:
`docs/decisions/0207-acoustic-portals.md`.

## 0. Result

* **Acoustic portals** (`FellowAcousticPortal3D`): a doorway, a window, a hatch. A source behind a wall is heard
  through the nearest open opening between it and the listener when that path is clear and louder — from the
  opening's direction at the path's length, quieter and duller the more it bends there; a door in the doorway, or the
  portal's `transmission`, closes it.
* **Checked**: six lanes on both physics engines and both builds; under Movie Maker the source behind a doorway 14 dB
  louder and 15 times brighter than through the wall, and heard from the doorway's side (+2.9 dB right).

## 1. Development

* `modules/fellow_world/fellow_acoustic_portal_3d.{h,cpp}` (new), `fellow_acoustics_3d.{h,cpp}` (the path, three
  properties, `get_portal_weight()`, `get_heard_position()`), `register_types.cpp`, `config.py`, `doc_classes/`,
  `icons/FellowAcousticPortal3D.svg`.
* `scene/3d/audio_stream_player_3d.{h,cpp}`: the hook takes a position and a weight (marked; stock's without it).
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: the gizmo, its width and height handles, "Add Acoustic Portal".
* `benchmarks/project/bench/acoustics_check.gd` (the lanes, the listening, `--portals=1`);
  `benchmarks/tools/editor_check/project` (the icon, a portal in `gizmos.tscn`, its handle dragged).

## 2. Verification

* Decision 0207's tables: the editor build and the release template, GodotPhysics3D and Jolt; the earlier lanes the
  same.
* The editor check: passes (35 icons; the only error ALSA's).
* The laptop (job f400, a release template, MSVC): the six lanes with GodotPhysics3D and Jolt and the listening as on
  the cloud, value for value (the doorway −25.8 dB against −40.0 through the wall, +2.86 dB to the right).

## 3. Measurements

Two rays an opening tried, from the node's ray budget. On the laptop with 61 openings: 256 sources 0.22 → 0.30 ms a
frame, 1 024 0.52 → 0.60 ms (the openings' planes tested for every blocked source, their rays longer), 12 sources
heard through one a frame, 68-70 tested a frame instead of 102 (the budget shared).

## 4. Next

A reverb per room heard through its openings.
