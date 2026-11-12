# 0187 — What a vehicle leaves and sounds like: skid marks, tyre smoke, engine and tyre sounds

Status: **KEEP** (a new node, a child of a `FellowVehicle3D`; without it nothing changes) · Branch `fellow/geometry` ·
Code: `modules/fellow_world/fellow_vehicle_effects_3d.{h,cpp}` (`FellowVehicleEffects3D`),
`modules/fellow_world/fellow_vehicle_3d.{h,cpp}` (a wheel's slip speed and contact normal),
`modules/fellow_editor/fellow_editor_plugin.cpp` ("Add Car" gets one) · Checks:
`benchmarks/project/bench/vehicle_fx_check.gd` (five lanes; `--listen=1`, the sound itself; `--pictures=1`;
`--many=N`), the editor check.

## Problem

Decision 0185's cars drove silently and left nothing behind: a driving game's feel is half in its tyres' marks and
smoke and its engine's note, and its "Next" named them (surface effects from the slips and loads).

## Change

`FellowVehicleEffects3D`, a child of the vehicle, at run time:

* **When a tyre slides**: faster than `slip_min` (1.5 m/s) over the ground and past its grip — its combined slip (the
  slip ratio and the slip angle's tangent) beyond `slide_slip` (0.2), so that a tyre held at its peak by the traction
  control or the anti-lock brakes leaves nothing — its intensity grows to 1 at `slip_full` (6 m/s) and 0.2 more slip.
* **Skid marks**: a ribbon a wheel (`mark_width` 0.22 m across the tyre's axle on the ground, 2 cm above it), a segment
  each `mark_step` (0.3 m), its opacity the intensity times `mark_opacity`, joined end to end; in one mesh in the world
  (`max_mark_segments` 512, the oldest reused), each new segment written into its vertex and attribute buffers in place
  (`ARRAY_FLAG_USE_DYNAMIC_UPDATE`), an unshaded dark material blended over the ground (darkening it in light and shadow
  alike), the mesh's bounds grown as marks reach out. `clear_marks()`.
* **Smoke**: a GPU particle system at each wheel's contact, in the world, emitting `smoke_amount` (40) over
  `smoke_lifetime` (2.2 s) times the intensity: billboards rising, growing and fading (`smoke_color`).
* **Sound**, synthesised in an `AudioStreamPlayer3D` at `engine_offset` (heard through decision 0186's acoustics like
  any other) from a generator filled each frame: the engine's firing frequency (revolutions a second times `cylinders` /
  2) and four harmonics (by Chebyshev's recurrence from one sine and cosine a sample), the upper ones brighter with the
  throttle, a burst of noise at each firing, the crankshaft's rumble, low-passed from 200 Hz to 2.7 kHz with the
  throttle and revolutions and louder with both; the tyres' squeal (noise through a resonant band-pass rising from 800
  to 1 500 Hz with the sliding); the road's rumble (low-passed noise, the speed squared); a soft limit. The controls
  eased over 30 ms. Silent (stopped, nothing synthesised) farther than `sound_distance` (80 m) from the listener.
* `get_stats()`, `get_global_stats()`; the wheel's `get_slip_speed()` and `get_contact_normal()`.

## Checks (cloud: the CPU, lavapipe)

`bench/vehicle_fx_check.gd`, the editor build, cars as decision 0185's with the node; GodotPhysics3D and Jolt alike:

| lane | expected | result |
|---|---|---|
| cruising straight to 50 km/h (full throttle from rest, the traction control on) | no marks, no smoke | none |
| a handbrake turn at 50 km/h | marks and smoke from the sliding rear | 139 segments, smoke in 118 frames, the marks 2 cm above the ground |
| a burnout (the traction control off, full throttle from rest) | marks and smoke from the spinning rear | 138 segments, smoke in 88 frames |
| a stop from 80 km/h, the anti-lock brakes off | marks from the four locked wheels | 360 segments while braking |
| the same with them | fewer | none (the slip held under 0.15) |

The first version marked the cruising car too: at speed the traction control's 15 % slip is more than 1.5 m/s over the
ground; sliding is now past the tyre's grip as well as fast.

The sound (`--listen=1`: Movie Maker on lavapipe, the master bus captured, Goertzel's power at the firing frequency
against at 0.8 and 1.25 times it, the share of energy in 700-1 700 Hz):

| moment | engine | the tone over its neighbours | level | squeal band |
|---|---|---|---|---|
| standing, idling | 900 rpm, 30 Hz | +35.5 dB | −42.8 dB | 0.001 |
| 70 km/h, third | 2 998 rpm, 100 Hz | +33.8 dB | −34.6 dB | 0.019 |
| a handbrake slide | 900 rpm (the rear wheels held) | +28.6 dB | −24.8 dB | 0.151 |

Its spectrogram shows the harmonics climbing through first gear, stepping down at the shifts and the squeal's band in
the slide. The release template: the same lanes. Lavapipe: the marks and smoke filmed (the burnout's two black strips
under its smoke, the handbrake turn's arc, the locked wheels' four lines). The editor check: the icon, "Add Car" with
the node, without a warning. Decision 0185's lanes unchanged.

The node's time (editor build): the marks and smoke 0.5-1.1 µs a car a frame (64 cars sliding in circles 0.04 ms); the
synthesis 46 ns a sample, 37 µs a frame for each car within `sound_distance` (it was 66 ns before the low-pass's
coefficient was set once a block and the burst's power and the limit were made cheap).

## Results on the laptop

Not run: the laptop was not linked to the session.

## Decision

Keep: a car slides, smokes and sounds like one with a node and no assets.

## Rejected / not done

* **Sampled engine sounds** (recordings crossfaded by revolutions and load): a project can play its own in a player of
  its own; the synthesis needs no assets.
* **Marks that fade with time or wear with the surface; dust and gravel on loose ground** (by the ground's material):
  next.
* **Marks shared by all vehicles in one mesh**: one mesh a vehicle (512 segments by default).
