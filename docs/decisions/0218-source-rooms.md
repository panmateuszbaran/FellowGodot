# 0218 — Each source's own room

Status: **KEEP** (`FellowAcoustics3D.source_rooms`, 0 by default: every source sent to the listener's reverb, as
before; the FellowEngine menu's "Add Acoustics" sets 2) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_acoustics_3d.{h,cpp}`, `modules/fellow_editor/fellow_editor_plugin.cpp` (the preset) ·
Checks: `benchmarks/project/bench/acoustics_check.gd` (three lanes, `--listen=1` one more, `--many=N --rooms=N`).

## Problem

Decision 0186 sends every source to one reverb, set by the room the listener is in. A source in another room sounds as
if it were in the listener's: a church's choir heard from the square outside its open doors gets the square's reverb
(none), a radio in a closet next to a hall gets the hall's long tail. Middleware gives each room its own reverb and
sends a source to its room's (Wwise's rooms and their aux buses, Steam Audio's per-source reverb); the readiness
assessment listed "one room's reverb (the listener's)".

## Change

`source_rooms` (0-8): that many reverbs more, on buses named after `reverb_bus` ("FellowReverbRoom1", "Room2"...: made
as decision 0186 makes its bus, removed with it).

* **A source's room**: one audible source's room measured a frame, in turn — decision 0186's measurement from the
  source (`reverb_rays` rays in every direction: the enclosure, the mean distance, the nearest wall), within
  `max_rays_per_frame`.
* **Alike rooms**: two measurements are one room when they are as closed (within 0.2) and as large (mean distances
  within a factor of 1.5). A source whose room is alike to the listener's, or that has none of its own (closed over by
  less than half: the open, a roof), is sent to the listener's reverb as before.
* **The others**: each goes to a room reverb — the one it had while that still holds an alike room, else one holding
  an alike room, else a free one, else the one whose room is nearest. Each room reverb is driven by the mean of its
  sources' rooms as the listener's is by the listener's (the decay from Sabine's formula, the predelay from the nearest
  wall, the damping; eased over `reverb_response`); a source is sent to it by how closed its own room is
  (`reverb_level` × its enclosure), cut by half its occlusion's decibels, as decision 0186's sends.
* `get_source_room(player)`; `get_stats()`: `room_buses`, `room_buses_used`, `room_measures`.

## Checks (cloud: the CPU; the editor build and the release template the same; GodotPhysics3D and Jolt the same)

`acoustics_check.gd`: far to the east a hall 20 × 20 × 8 m with a doorway 2 × 3 m facing the listener 10 m away, a
closed closet 4 × 4 × 3 m 25 m away, a source in each and one in the open; `source_rooms` 2:

| lane | expected | result |
|---|---|---|
| the listener in the open | the hall's source on a room reverb (a decay of 0.8-2.5 s, sent > 0.3), the closet's on another (under 0.6 s, a smaller room size), the open one's on the listener's | Room1: RT60 1.35 s, room size 0.55, send 0.5; Room2: 0.29 s, room size 0, send 1.0 (occluded: the send cut less than the direct sound); the open one: FellowReverb |
| the listener in the hall | the hall's source on the listener's reverb; the closet's on its own; the open one's on the listener's | FellowReverb (the listener's RT60 1.25 s); Room2 0.29 s; FellowReverb |
| `source_rooms` 0 again | every source on the listener's, the room buses gone | so |
| `--listen=1`: the hall's source heard from the open (Movie Maker's mix) | its reverb's tail with source rooms, more than 6 dB weaker without | the tail 15.2 dB under the direct sound; without, 10.5 dB weaker still (the listener's own reverb: the open by the hall, a little closed) |

The earlier lanes (occlusion, materials, portals, the listener's rooms; with `--listen=1` the mix) the same values as
decision 0186-0207's. The editor check: "Add Acoustics" with `source_rooms` 2, no warning.

The laptop (job f429, the release template at 4bce0248e2): every lane the same values with GodotPhysics3D and Jolt;
`--listen=1` the same (the tail 15.2 dB under the direct sound, 10.5 dB weaker without source rooms).

## Cost

The rays of one room a frame (32), and a pass over the sources: 64 sources among walls (`--many=64`), on the cloud
(2 cores) 150-174 µs a frame without, 166-300 µs with two room reverbs (noisy); on the laptop (job f429, three
alternating runs each) 116-133 µs without, 135-137 µs with (+15 µs); the rays 322 → 354.

## Decision

Keep: with `source_rooms` a source is heard with its own room's reverb.

## Rejected / not done

* **A reverb per source**: a bus and an effect each; rooms are shared, and few at a time are heard.
* **Coupled rooms** (the hall's reverb leaking into the listener's room's through the door and reverberating again):
  each source to one reverb.
* **Rooms placed by hand** (volumes with their own reverb, Godot's `Area3D` reverb buses): measured by rays, as the
  listener's room is.
