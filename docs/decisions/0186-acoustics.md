# 0186 — Sound through the scene's geometry: occlusion and the listener's room

Status: **KEEP** (a new node; without it nothing changes — the hook in `AudioStreamPlayer3D` leaves a player stock's
until the node sets it) · Branch `fellow/geometry` · Code: `modules/fellow_world/fellow_acoustics_3d.{h,cpp}`
(`FellowAcoustics3D`), `scene/3d/audio_stream_player_3d.{h,cpp}` (the hook, marked), `modules/fellow_world/register_types.cpp`
(the monitor), `modules/fellow_editor/fellow_editor_plugin.{h,cpp}` ("Add Acoustics") · Checks:
`benchmarks/project/bench/acoustics_check.gd` (eight lanes; `--listen=1`, five of the mix itself; `--many=N`), the editor check.

## Problem

The readiness assessment's audio row was a gap: Godot's 3D sound is heard straight through walls, and its reverb comes
only from `Area3D`s placed by hand with a bus each. A game's sounds behind a wall, round a corner or in a hall need what
middleware (Wwise's obstruction and rooms, Steam Audio) gives: the geometry between the source and the listener, and
the room the listener is in.

## Change

`FellowAcoustics3D` (one works at a time; it tracks every `AudioStreamPlayer3D` that enters the tree, but those in the
group `fellow_no_acoustics`):

* **Occlusion**: each physics frame, rays from the listener (the viewport's `AudioListener3D` or camera, or
  `listener_path`) to each playing source within `max_distance` (80 m) — one to the source, four more to points
  `source_radius` (0.5 m) around it across the line, so that a path past an edge counts — on `collision_mask`; the
  bodies the listener and the source are in (their ancestors) do not block them. The share blocked, eased over
  `response_time` (0.12 s), sets the direct sound's gain (to `occluded_volume_db`, −12 dB) and a high shelf (to
  `occluded_shelf_db`, −18 dB, its cutoff moving from the player's own down to `occluded_cutoff_hz`, 1 kHz). At most
  `max_rays_per_frame` (512) rays: with more sources than they cover, each is tested in turn.
* **The listener's room**: every `reverb_interval` (0.25 s) `reverb_rays` (32) rays in every direction up to
  `reverb_max_distance` (40 m): the enclosure (the share of the directions above the horizon that meet something: the
  ground does not close a room), the mean distance to what they met, the nearest wall. A reverb on `reverb_bus`
  ("FellowReverb": made when there is none, sending to Master, with an `AudioEffectReverb` wet only; an existing bus's
  first reverb is driven) takes its decay from Sabine's formula with the mean free path (RT60 = 0.04 l / `absorption`,
  0.3) as the feedback of Godot's reverb's combs (from 0.7, room size 0, to 0.98: a decay of 0.6-8.5 s), its predelay
  from the nearest wall (2 d / 343 m/s), its damping from the absorption; eased over `reverb_response` (0.5 s). Every
  source is sent to it: `reverb_level` (0.5) times the enclosure, cut by half the occlusion's decibels (the room hears
  the source by other paths than the direct one).
* **The hook** (`AudioStreamPlayer3D::fellow_set_acoustics()`, C++ only): a gain on the direct sound, a high shelf on top
  of the distance's (its gain multiplied, its cutoff replaced while occluded), a send to a bus scaled from the direct
  sound. Its defaults (gain 1, shelf 1, no cutoff, no send) leave the player's mix exactly stock's: the node resets them
  when it is disabled or leaves the tree, and removes the bus and effect it made (the sends first: a send to a bus that
  is gone would play on Master).
* `get_occlusion(player)`, `get_room()`, `get_stats()`, `get_global_stats()`; the monitor "FellowEngine/Acoustics time";
  the FellowEngine menu's "Add Acoustics".

## Checks (cloud: the CPU, lavapipe)

`bench/acoustics_check.gd`, the editor build, white noise sources 8 m from the listener (the camera), GodotPhysics3D and
Jolt alike:

| lane | expected | result |
|---|---|---|
| in the open | not occluded | 0.0 |
| behind a wall 6 × 4 m at 4 m | occluded | 1.0 (−12 dB, the shelf −18 dB above 1 kHz) |
| past a wall's edge (the line 0.1 m clear of it) | partly | 0.2 (−2.4 dB) |
| inside a car's body (a rigid body round it) | not by its own body | 0.0 |
| going round the listener behind a wall for part of its lap | occluded, no jump | up to 1.0, at most 1.04 dB a frame |
| the listener in the open | no room | enclosure 0.001, send 0.0003 |
| in a closed room 8 × 8 × 4 m | a small room | enclosure 0.997, mean distance 3.6 m, RT60 0.48 s (the reverb's shortest, 0.6 s), predelay 14 ms, send 0.5 |
| under a roof 10 × 10 m at 4 m | half closed | enclosure 0.63, send 0.31 |

The mix itself (`--listen=1`: Movie Maker on lavapipe, its audio mixed frame by frame, the master bus captured; each
source alone with the node and with it disabled, the loudness and the share of energy above about 2 kHz):

| source | stock | with the node |
|---|---|---|
| in the open | −13.22 dB, highs 0.616 | the same |
| in the car's body | −13.40 dB | the same |
| behind the wall | −12.76 dB, highs 0.616 | −40.06 dB (−27 dB: −12 dB and the shelf on noise mostly above 1 kHz), highs 0.021 |
| past the edge | −13.23 dB, highs 0.616 | −20.22 dB (−7 dB), highs 0.453 |
| in the closed room with the listener | −12.08 dB; after it stops, silence | −11.46 dB (the reverb's +0.6 dB); 0.2-0.7 s after it stops −41.3 dB (the tail) |

Without the node, or disabled, the mix is stock's (the hook's defaults multiply by one). The release template: the same
lanes. The node's time (release template, the main thread; the walls of a grid, the sources among them, the listener
moving):

| sources | GodotPhysics3D | Jolt |
|---|---|---|
| 5 (the lanes) | 6 µs a frame | 6 µs |
| 64 (322 rays a frame) | 0.13 ms | 0.09 ms |
| 256 (512 rays: each tested every 2.5 frames) | 0.25 ms | 0.18 ms |
| 1 024 (each every 10 frames) | 0.50 ms | 0.40 ms |

The editor check: the icon, the monitor, "Add Acoustics" without a warning and undone.

## Decision

Keep. Walls and rooms change what a 3D sound sounds like with no setup but the node, through the stock player's own
filter and buses; a scene without it, or a player in `fellow_no_acoustics`, is stock's.

## Rejected / not done

* **Transmission by material and thickness, diffraction paths round corners, sound propagated room to room through
  portals**: a path is blocked or not; the points round the source stand in for diffraction near edges.
* **A reverb per source's room** (each source's own surroundings): one reverb for the listener's room.
* **HRTF, ambisonics, Doppler beyond stock's**: not addressed.
