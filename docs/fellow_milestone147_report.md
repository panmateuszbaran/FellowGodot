# FellowEngine — milestone 147 report (tracked vehicles and two-wheelers; the far cascades without instances out of range; a garment's backstop)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. Lavapipe and the CPU in the cloud; the RTX for the hang's
runs until the owner paused the GPU (jobs f508, f509). The GPU work prepared since (f510, f511) moves to milestone 148.

## 0. Result

* **Tracked vehicles and skid steering** (decision 0274): `FellowVehicle3D.steering_mode` `STEERING_SKID` — a steering
  gear holds the inner side's speed at a ratio of the outer's (straight 1, about the inner side 0 with the throttle
  held, on the spot -1 without it; at speed a share of the tightest turn 0.7 of the grip holds), the engine driving
  the outer side through it; `tracks` links each side's wheels; `skid_turn_drive` turns it on the spot with the
  steering alone. A 2 000 kg tracked vehicle: straight to 0.000° under throttle, 26°/s on the spot (moved 2 cm),
  0.43 g at 30 km/h with full steering without sliding (3° of sideslip). The FellowEngine menu's "Add Tracked Vehicle".
* **Two-wheelers** (decision 0275): `FellowVehicle3D.balance` — the rider: the steering asks for a lean (within
  `max_lean`, 0.8 of the grip and what the lock turns at the speed), held by a torque about the forward axis, the front
  wheel turned to the turn the lean balances; upright at a standstill, falling past `max_lean` + 25°. A 260 kg
  motorcycle: the lean as asked (19.35° for 19.33° at 60 km/h, 38.66° for 38.66° at 40 km/h), the turn's g the lean's
  tangent within 3 %, righted after a push that topples it without balance — on GodotPhysics3D and Jolt alike. The
  FellowEngine menu's "Add Motorcycle".
* **A garment's backstop** (decision 0273): `FellowCloth3D.skin_backstop` — a skinned garment's free particles kept
  from going more than a distance behind their skinned places along the skinned normals: a skirt without the hips'
  collider 5.9 cm into them before, none with a 5 mm backstop, at no measurable cost.
* **The far cascades without instances out of range** (decision 0272): a directional cascade that begins beyond an
  instance range's end (the terrain's foliage, HLOD's instanced proxies and impostors) leaves those instances out — per
  cascade, so the shadow cache keeps it; `shadows/instance_range_cascades` (on). Lavapipe: the same pictures, 4 710
  instance × cascade pairs left out over `terrain_check`'s run; its time on the GPU pending.
* **The hang** (decision 0270, continued): the render graph's reordering and full barriers as settings, NVIDIA's
  checkpoints before each command (`debug/gpu_checkpoints`); caught once with the reordering off (another frame) and
  once with the checkpoints — the GPU stopped inside one draw list (command 69 of its frame), everything before it
  done; a draw list's checkpoint now names its region, size and whether worker threads recorded it. 30 runs since
  without a hang (about one run in 20 hangs). Open.

## 1. Checks

* CPU (cloud): `vehicle_check.gd` — six tracked-vehicle and six motorcycle lanes on GodotPhysics3D and Jolt, every car
  lane the same as the build before (every value but the dents' timing); `--many=32 --tank=1 / --bike=1` (a tracked
  vehicle's step 15.7-17.9 µs, a motorcycle's 6.3-6.4 µs, a car's 12.8 µs); `cloth_check.gd`'s backstop lanes.
* Lavapipe: the image regression suite (48 of 48 identical after 0272; the cloth and vehicle scenes identical after
  0273-0275); `terrain_check.gd --shadow_distance --foliage_kinds --grass_range`; the editor check (the two new
  presets: no configuration warning, undone).
* The RTX: Bistro exterior streamed with releases held 10 s — the checkpoints and the reordering off (`m146_f508`, 20
  runs each: one hang in each), the richer checkpoints (`m147_f509`, 30 of 40 runs before the pause: none).

## 2. Next (milestone 148)

On the GPU when the owner allows it: `f510` (40 runs of the hang with the richer checkpoints) and `f511` (decision
0272's A/B on Frontier Lab and Slice Lab), both prepared; then the hang's fix and releases held 10 s.
