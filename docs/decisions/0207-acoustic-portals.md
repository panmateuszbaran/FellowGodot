# 0207 — Sound through openings (acoustic portals)

Status: **KEEP** (`FellowAcousticPortal3D`, a node; `FellowAcoustics3D.portals`, on) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_acoustic_portal_3d.{h,cpp}`, `fellow_acoustics_3d.{h,cpp}`,
`scene/3d/audio_stream_player_3d.{h,cpp}` (marked), `modules/fellow_editor` (gizmo, handles, a ready-made doorway) ·
Checks: `benchmarks/project/bench/acoustics_check.gd` (six lanes, one under Movie Maker, `--portals=1` with `--many`).

## Problem

Decision 0186 hears a source through what lies on the straight line to it: a radio in the next room behind a wall is
muffled (−12 dB, its highs shelved and low-passed) whether or not the door between the rooms stands open, and it is
heard from where it is, through the wall. In a building, sound reaches the listener round the corner through the
doorway: louder and brighter than through the wall, and from the doorway's direction. Wwise's and Steam Audio's
rooms-and-portals, Unreal's audio portals do this; the readiness assessment listed "no diffraction paths or portals".

## Change

`FellowAcousticPortal3D`: an opening — a rectangle of `size` (1 × 2.1 m) in the node's XY plane, `transmission` (1
open, 0 closed). For a source decision 0186's rays find blocked, `FellowAcoustics3D` looks for a path through an
opening:

* **Candidates**: the open portals whose plane separates the listener from the source; the point where the line between
  them crosses that plane, brought 10 cm inside the opening's edges; nearest path first, at most `max_portals` (2),
  within the source's reach.
* **Clear**: two rays, listener to the point and the point to the source (a door in the doorway blocks it while it is
  there). The first clear one decides.
* **Heard that way** when it is louder than through the wall: the path's extra length (inverse distance), its bend
  through the opening — `portal_diffraction_db` (−6 dB) a quarter turn, in proportion — and its transmission, the
  highs counted half (the wall's shelf against the bend's: a bend of half a turn shelves them as a wall does).
* **Then** the source is heard from the opening's direction at the path's length: `AudioStreamPlayer3D` takes a
  position and a weight from the node (a marked addition to its hook: the panning and the distance attenuation from
  there), so the player's own attenuation model accounts for the longer path; its occlusion is the bend's and the
  transmission's, its highs' the bend's. The apparent position and its weight move with `response_time`.

Two rays an opening tried, from the same per-frame ray budget as the occlusion's.

## Checks (cloud: the CPU; lavapipe for Movie Maker)

`acoustics_check.gd`: three walls 4 m from the listener, each with a doorway 1.2 × 2.2 m 3 m to the side and a portal
in it, a source 8 m away behind each; the editor build, the release template, GodotPhysics3D and Jolt the same:

| lane | expected | result |
|---|---|---|
| the open doorway (a bend of 64°) | heard through it: occlusion 0.356 (−4.3 dB), its highs' 0.356, from 5 m to the right at 9.43 m | weight 1.0; 0.356, 0.356; from (5.0, 0, −8.0), 9.43 m |
| the closed portal (transmission 0) | as through the wall | occlusion 1.0, weight 0 |
| the doorway shut by a door (a slab) | as through the wall | occlusion 1.0, weight 0 |
| the door taken away | as the open one, from its side | weight 1.0, 0.356, from (−8.0, 0, −5.0) |
| the closed portal opened half | −6 dB more: 0.857 | weight 1.0, 0.857 |
| the open one, after | unchanged | 0.356 |

The fourteen earlier lanes: the same results. Under Movie Maker (`--listen=1`, the master bus captured) the source
behind the open doorway:

| | level | the share above 2 kHz | right over left |
|---|---|---|---|
| stock (no occlusion) | −12.8 dB | 62 % | 0.00 dB |
| the node, portals off (through the wall) | −40.0 dB | 2 % | 0.01 dB |
| the node, portals on (through the doorway) | −25.8 dB | 30 % | +2.86 dB |

The editor check: the portal's icon (35 icons), gizmo (the opening, a line through it), its width dragged with the
mouse and undone, "Add Acoustic Portal" without a warning and undone.

The laptop (job f400, a release template, MSVC): the six lanes with both engines and the listening the same, value for
value.

## Cost

`--many` (512 rays a frame), with an opening beside each of the 61 walls: 12 sources heard through one a frame. The
laptop: 256 sources 0.22 → 0.30 ms a frame, 1 024 0.52 → 0.60 ms (+0.08 ms: every blocked source's openings' planes
tested, two longer rays an opening tried); the cloud's two cores within their run-to-run spread (0.33-0.43 ms,
0.63-0.71 ms). The rays the openings take come from the budget: 68-70 sources tested a frame instead of 102.

## Decision

Keep: a doorway lets the next room's sound through, from its direction.

## Rejected / not done

* **Rooms** (a reverb for each room, a source's room's reverb heard through the opening): the listener's room only
  (decision 0186).
* **Paths through two openings** (room to room to room): one.
* **Diffraction round any edge** (a wall's end without a portal): decision 0186's rays past the source's radius only.
