# FellowEngine — milestone 78 report (sound through the scene's geometry)

Status: **FINAL** (2026-11-12): the owner said "kontynuuj usprawnienia do poziomu AAA naszego silnika, pracuj
przynajmniej do 8 rano" (the GPU allowed). Branch `fellow/geometry`. Decision record:
`docs/decisions/0186-acoustics.md`.

## 0. Result

* **`FellowAcoustics3D`**: every 3D sound in the scene heard through its geometry with no other setup — walls between
  a source and the listener lower it and cut its highs, a path past an edge partly, the bodies a sound is in do not;
  the listener's room (how closed, how large, the nearest wall) sets a reverb every sound is sent to (more in a closed
  room, through a wall less than its direct sound).
* **A small hook in `AudioStreamPlayer3D`**: a gain, a high shelf and a send the node sets; its defaults leave the
  player stock's.
* **Checked**: eight lanes with GodotPhysics3D and Jolt, and the mix itself (Movie Maker's audio, the master bus
  captured): the open and in-body sources exactly stock's, a wall −27 dB on noise with its highs cut, a room's reverb
  tail where stock is silent; 64-1 024 sources within a ray budget.

## 1. Development

* `modules/fellow_world/fellow_acoustics_3d.{h,cpp}`; `register_types.cpp` (the class, the monitor), `config.py`,
  `doc_classes/FellowAcoustics3D.xml`, an icon.
* `scene/3d/audio_stream_player_3d.{h,cpp}`: the hook (marked).
* `modules/fellow_editor/fellow_editor_plugin.{h,cpp}`: "Add Acoustics".
* `benchmarks/project/bench/acoustics_check.gd`; the editor check: the icon, the monitor.

## 2. Verification

* The lanes on the editor build (cloud) with GodotPhysics3D and Jolt, and on the release template: all as expected
  (decision 0186's tables).
* The mix under Movie Maker (lavapipe): the node's effect on the stock mixer measured, stock's where it should be.
* The editor check: the icon, the monitor, "Add Acoustics" without a warning and undone.
* The laptop: not run (not linked to the session).

## 3. Measurements

The node's time on the main thread (release template): 6 µs a frame for 5 sources, 0.09-0.13 ms for 64, 0.18-0.25 ms
for 256, 0.40-0.50 ms for 1 024 (Jolt-GodotPhysics3D; 512 rays a frame).

## 4. Next

Materials' transmission, diffraction round corners and portals between rooms; a reverb for each source's room.
