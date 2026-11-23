# FellowEngine changelog

FellowEngine is a fork of Godot 4.7.2-stable. Its work is on the Forward+ renderer (and, from milestone 15, on
skeletal animation, physics and new nodes for smoke, fire, burning, water, rain, wind and far objects), measured on an
RTX 5070 Ti Laptop GPU at 1920x1080 against stock Godot 4.7.2. Every change sits behind a project setting under
`rendering/fellow/`, or in a node that does nothing unless a scene contains it; the settings are documented in
[docs/FELLOWENGINE_OPTIONS.md](docs/FELLOWENGINE_OPTIONS.md) and each decision in [docs/decisions/](docs/decisions/).
"Exact" below means the same pixels (or physics state) as without the change: bit for bit on lavapipe (a CPU Vulkan
driver), bit for bit or at the run-to-run floor on the RTX. Percentages against stock are frame time unless noted.
Godot's own changelog (the upstream 4.7.2 changes) is [CHANGELOG.md](CHANGELOG.md).

## Milestone 183 — balance, falls and getting up (2026-11-23)

- `FellowPhysicalAnimation3D.balance` (new, off; decision 0325) with `motion_matcher_path`: at a hit the capture point
  (Pratt 2006) decides — under `step_threshold` (0.12 m) the drive takes it back, beyond it a stagger (the motion
  matcher pushed that way, as far as the capture point), beyond `fall_threshold` (0.7 m) a fall: stiff about the feet as
  an inverted pendulum until `topple_angle`, then limp. Lying still, it gets up with the database's clip tagged
  `get_up_tag` that starts on the face it lies on and fits its pose best (a least-squares fit from above), blended from
  the ragdoll, and the motion matcher takes over where the clip ends. `knock_down()`, `get_up()`, `get_state()`,
  `get_landed_face()`, `get_balance_stats()`; signals `staggered`, `fell`, `landed`, `got_up`.
- `FellowMotionMatcher3D.push(velocity, duration)`, `is_pushed()`: a stagger's velocity, decaying, the facing held.
- With balance a hit spreads over the body by mass (the struck part up to 1.5 m/s more); the joints made anew put the
  bodies back on their joints first.
- Jolt, CMU's walks with subjects 139 and 140's nine get-up clips: pushes of 40-160 N·s stagger 0.1-0.64 m, 200 N·s
  falls; from every side it lands on its back or face, lies still within 0.9-1.3 s, picks a clip whose start fits within
  7-12 cm and stands in 3-5.4 s, the bodies 2 mm from the animation after. Off, milestone 166's numbers.
- `bench/balance_check.gd`, `bench/balance_video.gd`; Matching Lab tags subjects 139 and 140 `get_up`;
  `get_cmu_locomotion.sh` fetches their trials.

## Milestone 182 — realistic clouds (2026-11-23)

- `rendering/fellow/post/clouds/realism` (new, on; decision 0324): the clouds no longer soft, smooth blobs — their
  edges sharp (the density ramp at a rim eight times steeper at the tops), detailed down to 10 m (the detail noise
  warped; two finer octaves bumping the edges in and out as far as they are a pixel or more), their outlines rough (the
  weather field's finer octaves, the shapes warped); lit as thick clouds are (multiple scattering by two-stream
  diffusion: a sunlit side 0.8 E / π, a billow turned from the sun dimmed, the sky's light diffused through the cloud
  above); hazed with distance by the atmosphere's tables (the aerial perspective, not a fade); upsampled by
  Catmull-Rom. Off: decision 0310's clouds.
- Lavapipe, the showcase's sky: the clouds overhead with edges 2.6 times as steep, twice the texture, brighter than the
  sky behind them (dimmer before), the same cover; about a level of change from frame to frame; three times decision
  0310's cost on lavapipe (the RTX not measured yet).
- `showcase/tools/sky_look.gd` (the showcase's sky in seven views, from the ground and from 20 km up) and
  `benchmarks/tools/sky/sky_metrics.py` (the clouds' cover, edges, texture, luminance, outlines' fractal dimension).

## Milestone 181 — footsteps (2026-11-23)

- `FellowFootsteps3D` (new, a `SkeletonModifier3D` last on a character's skeleton; decision 0323): footsteps from the
  feet as drawn — each touch down (at the speed the sole came down), the sole rolling onto the ball, the toe lifting,
  scuffs — on what the ray under the foot finds; `contact_height`, `heel_offset`, `collision_mask`, `body_mass`,
  `shoe` (sneaker, boot, hard heel, bare feet), `default_ground`, `sound_*`, the `footstep` signal, `get_ground()`,
  `get_foot_state()`, `get_sound_stats()`, the static `debug_render(events, seconds[, rate, seed])`.
- Thirteen grounds: stone, wood, metal, tile, carpet (the sole's contact through the shoe's and the floor's
  resonances), gravel, sand, snow, grass, leaves, dirt (grains crushed: Cook's PhISEM), mud (squelch, suction), water
  (splash, drips).
- `FellowContactSound3D.surface` and `get_ground()`; `FellowTerrainLayer.surface` (a terrain's layers mixed by their
  weights under the foot); a collider's `fellow_surface` metadata.
- `benchmarks/tools/world_sound/footstep_sound.gd` + `footstep_sound.py`;
  `benchmarks/tools/foot_placement/footsteps_demo.gd` (a CMU walk across the grounds, `--terrain`).
- 181b, the grounds' balance (the owner's ear: mud and the steel deck far louder than the rest): a ground's level is
  what it radiates, set against walking measured on stone, wood, gravel and grass (Aletta et al. 2016) and the
  physics elsewhere — a step over a step on stone (K-weighted): metal +19.5 → +12.0 LU, mud +19.9 → +5.9, water
  +21.9 → +12.0, gravel +8.0 → +11.9, grass +3.8 → −3.0; `footstep_sound.py` measures it.

## Milestone 180 — bodies' contact sound: modal synthesis (2026-11-23)

- `FellowContactSound3D` (new, a child of a `RigidBody3D`; decision 0322): the body heard from its own vibration
  when it strikes, slides or rolls — `material` (wood, metal, stone, glass, plastic, rubber, ceramic),
  `wall_thickness` (hollow), `ground_material`, `mode_count`, `sound_*`, the `impact` signal, `get_modes()`,
  `get_sound_stats()`, `debug_render(events, seconds[, rate, seed])`. On a static body it gives its material to what
  strikes it.
- Modes from the body's shape and material (a beam's, plates', a ring's bending; decay from the loss factor); strikes
  from the contacts (the impulse over the mass, the Hertzian contact's duration, each mode's shape where it lands, the
  body's own click); sliding and rolling from the surfaces' roughness; resting damps.
- `FellowDestructible3D`: its pieces sound like its body when it has a `FellowContactSound3D`.
- `benchmarks/tools/world_sound/impact_sound.gd` + `impact_sound.py` (offline: the modes against beam theory, decay,
  where it lands, speed, hardness, sliding) and `impact_demo.gd` (a yard of falling things with Movie Maker).

## Milestone 179 — fire's sound: the flames' roar, the wood's crackle (2026-11-23)

- A fire is heard where it burns (decision 0321), synthesised on the audio thread with no samples and placed by an
  `AudioStreamPlayer3D` its node owns while it sounds (decision 0320's voice, placed): `FellowGasVolume3D` and
  `FellowBurnable3D` `sound_enabled` (on), `sound_volume_db`, `sound_bus`, `sound_unit_size`, `sound_max_distance`,
  `get_sound_stats()`; `FellowBurnable3D.sound_crackle`, `get_fuel_release()`;
  `FellowGasVolume3D.debug_render_sound(keys, seconds[, rate, seed])`.
- The flames (the gas volume): the heat release from the simulated fire (its emission, the flames' volume, about 1.2
  MW a cubic metre; read back also with the fire light off) — a roar peaking at 160 / sqrt(D) Hz with the heat's root
  as its amplitude, puffing at 1.5 / sqrt(D) Hz with the simulation's own flicker, the flames' flutter and hiss, a
  low whoomp when the fire grows fast.
- The wood (each burning object): cracks where it pyrolyses — a snap and a short ring, energies a power law, runs of
  them, pops of sap — at a rate with the area of its front; the embers' ticks while its char glows.
- `benchmarks/tools/world_sound/fire_sound.gd` + `fire_sound.py` (offline: the roar's peak and puffing against their
  predictions, loudness against heat, the cracks' rate and power law, a flare) and `fire_demo.gd` (Burn Lab's campfire
  with Movie Maker; `--timing`, `--silent`, `--probe`).

## Milestone 178 — the weather's sound: rain, wind, thunder (2026-11-23)

- `FellowRain3D`, `FellowWind3D` and `FellowLightning3D` are heard (decision 0320), synthesised on the audio thread with
  no samples (`modules/fellow_fluid/fellow_weather_sound.*`: a stereo synthesis behind an internal `AudioStream`):
  `sound_enabled` (on), `sound_volume_db`, `sound_bus`, `get_sound_stats()`; `FellowWind3D.sound_whistle`,
  `sound_leaves`.
- Rain: distant drops as a filtered bed, near drops each its own (sized by Marshall and Palmer) — on water a tick and
  the small bubble of 0.8-1.1 mm drops (13-21 kHz), on the ground a click; under a cover the drops knock on it and the
  rain outside is muffled; rays probe the cover over the listener and the water round it every 0.2 s.
- Wind: the speed with the gust where the listener is — a rumble and a rush rising with it, edges whistling at the
  Strouhal frequency (0.2 v / d) from 5 m/s, leaves rustling.
- Thunder: the bolt's channel and its run through the cloud heard from the listener at the strike — each segment's
  N-wave arriving after its distance / 343 s (broadside claps, a near strike's crackle, the highs lost over distance),
  every stroke, an outdoor reverberation; built on a worker thread, heard from its first arrival.
- Measured (`benchmarks/tools/world_sound/`, new: `weather_sound.gd` + `weather_sound.py` offline, `storm_demo.gd` with
  Movie Maker): on water half the rain's sound is the bubbles' band above 9 kHz, under a roof its centroid falls from
  5.2 to 1.1 kHz; wind −45 → −19 dBFS from 3 to 20 m/s, its whistles within 10 % of 0.2 v / d; thunder heard 23 ms after
  distance / 343, rolling 3.5-6.4 s, −11 dBFS at 300 m to −20 at 3 km. 1.6-4 ms of the audio thread a second of sound
  each. The bolt's channels made by one function for the picture and the thunder (the lightning scene's golden
  unchanged).

## Milestone 177 — water's sound synthesised from its bubbles (2026-11-23)

- Water sounds as it is struck, with no samples (decision 0319): what strikes it entrains air bubbles, each ringing at
  its Minnaert frequency (3.26 m/s over its radius), decaying by its losses and rising in pitch (van den Doel 2005); a
  splash is a slap, a cloud of up to 300 bubbles sized by what struck and how fast, a large body's cavity closing;
  floating bodies lap where waves close over them; a wake rushes and bubbles. `modules/fellow_fluid/fellow_water_sound.*`
  (an internal `AudioStream`, its synthesis on the audio thread).
- `FellowWater3D`: `sound_enabled`, `sound_volume_db`, `sound_voices` (8 3D voices, made at the first sound; splashes
  within 2.5 m share one, the quietest is taken by a louder one), `sound_max_distance` (60 m), `sound_unit_size`,
  `sound_bus`; `play_splash(position, speed, size)`, `add_wake_sound(position, level)`, `get_sound_stats()`,
  `debug_render_sound(events, seconds)`. `FellowBuoyancy3D.sound` (its splashes, laps and wake;
  `FellowDestructible3D`'s debris too).
- Measured (`benchmarks/tools/buoyancy/water_sound.gd` + `water_sound.py`, `sound_timing.gd`, new): single bubbles
  within 0.01 % of Minnaert's frequency and 0.2 % of the model's damping; splashes 29 dB louder from 1.5 to 11 m/s and
  deeper with size; 0.1-0.9 ms of the audio thread a second of sound a splash, 4 ms for a crate's twenty pieces; heard
  2-9 ms into the frame a strike is drawn in. `interaction_demo.gd` records its sound with Movie Maker.
- `AudioServer::finish` frees the playbacks still listed at exit (stopped players waiting for a mix that no longer
  comes were reported as leaked).

## Milestone 176 — water interaction: debris floats, splashes and wakes (2026-11-22)

- `FellowDestructible3D.debris_floats` (decision 0318, on by default): while the scene has a `FellowWater3D`, each
  piece thrown out gets a `FellowBuoyancy3D` (its points a third of the piece apart, at most 16). The interaction
  course (`benchmarks/tools/buoyancy/interaction.gd`, new): a wooden crate broken over still water — 24 of 24 pieces
  afloat, the pile displacing its weight of water within 0.01 %; stone sinks, reaching the bottom at 3.4 m/s at most
  (14.4 m/s through the water as before).
- Foam made by what floats: `FellowWater3D.add_foam(position, radius, amount)` and `get_foam(position)` on a 256²
  grid within `body_foam_size` (64 m) of the camera, fading by `body_foam_decay` (0.3/s), drawn through the foam's
  noise (lace, not sheets); 15-38 µs of the main thread a frame while there is any, nothing otherwise.
  `FellowBuoyancy3D.foam`: splashes where its points strike the water (faster than 1 m/s), wakes where they cross the
  waterline moving (faster than 0.4 m/s); `water_entered(speed)`, `water_exited`, `is_in_water()`. A body found under
  the surface does not splash.
- The CPU's waves made on a worker thread after the frame's process while something floats: the main thread's time
  0.33 ms → 2-10 µs an update (4 ms of other work in the frame). `FellowWater3D.get_surface_ceiling()`: bodies above
  the waves' reach skipped (0.2 µs a tick). `get_cpu_wave_stats()` adds the worker's and the main thread's times.
- Pictures (`interaction_demo.gd`, lavapipe): crates broken over a sea, their pieces splashing in and floating, a
  barrel ringed with foam, a raft's wake. The buoyancy course and the water and rain goldens unchanged.

## Milestone 175 — buoyancy: bodies floating on the waves (2026-11-22)

- `FellowWater3D`'s waves on the CPU (decision 0317): the cascades of 10 m and longer evaluated from the GPU's own modes
  (the spectrum's per-mode code now shared with the GPU's upload), two inverse FFTs a cascade on a 16-64 grid, when a
  point is asked for at a new time: `get_surface_height(position)` (the choppy displacement inverted, the GPU's
  half-texel shift included), `get_surface_velocity(position)`, `get_cpu_wave_stats()`, `debug_compare_cpu_waves()`.
  Against the GPU's displacement on lavapipe: 1.3 cm rms off in a sea of 2.3 m significant height.
- `FellowBuoyancy3D`, a child of a RigidBody3D: its shapes filled with points (boxes, spheres, capsules, cylinders,
  convex hulls exactly), each pushed up by the water it displaces and dragged towards the water's motion. The buoyancy
  course (`benchmarks/tools/buoyancy/`, new): displaced volume at rest within 0.01 % of Archimedes'; a raft rights
  itself, a post lies down, a cube of half the water's density floats edge up; a buoy follows the surface in a 2.3 m
  sea (correlation 0.997); 4 µs a body a tick, the waves 0.3 ms an update.

## Milestone 174 — foot placement: the feet on the ground under them (2026-11-22)

- `FellowFootPlacement3D` (decision 0316), a skeleton modifier for characters animated on flat ground walking on
  slopes, stairs, roofs and rubble: rays under each foot's heel and ball, each foot raised or lowered onto the ground
  under it and turned to it while down, the hips lowered as far as the lower foot goes down and as far as a leg needs
  to reach; eased in the world (a foot down stays put while the body climbs over it), following moving goals with
  their velocity, a swinging foot looking 0.05 s ahead for step edges. After a `FellowFootLock3D` the held feet are
  placed where they are held. Nothing on flat ground.
- The foot placement course (`benchmarks/tools/foot_placement/`, new): CMU's walk and run driving a CharacterBody3D
  over eight lanes. Walking up 20°: the feet down sunk 10.9 → 0.8 cm, floating 8.6 → 0.8 cm (95th percentiles); up
  stairs floating 15.8 → 4.3 cm; a swinging foot in the ground 28 % → 0 of its frames on the ramp, 13 % → 1.2 % on the
  stairs; 4-7 µs a character.
- The rooftops showcase: its player has one after the foot lock, with the lock's weight (its 38 animation gates
  unchanged); `showcase/tools/foot_placement_lab.gd` pictures the mannequin across a roof and up stairs with and
  without it.
- The leg IK shared by `FellowFootLock3D` and `FellowFootPlacement3D` (`fellow_leg_ik.h`; the lock's results the same).

## Milestone 173 — the foot lock lets go without jumps (2026-11-22)

- Decision 0315, `FellowFootLock3D` (behaviour; no new settings):
  - A held point out of the leg's reach is let go from where the foot is shown (it was let go from the point and
    pulled within reach towards the animated foot: up to 17 cm in a frame).
  - Brought within 99 % of the leg's reach straight towards the hip where that is more than 2 cm shorter than towards
    the animated foot.
  - The knee's pole from the foot's forward where the animated leg is nearly straight (its side flipped: a bone turned
    85° in a frame).
  - The held offset's velocity measured over the time since the pose changed (zero when a script advances the player
    before the skeleton's own process).
- The transitions course, the inertializer with a foot lock: sliding everywhere 0.329 → 0.309 m/s (in transitions the
  same 0.785), the legs' acceleration's 95th percentile 40 778 → 38 268, every bone's 50 398 → 45 073, the largest
  turn in a frame 85° → 52°, planted feet no faster than 5.1 m/s (were 17). The showcase's 38 animation gates
  unchanged.
- Measured and rejected (decision 0315): dragging held feet along, easing the reach in after letting go, not holding
  a foot again until it lifts, slowing a foot coming down, a quintic return.

## Milestone 172 — feet held through transitions; decision 0313 corrected (2026-11-22)

- Decision 0313 corrected: the transitions course measured each character at each of its skeleton's updates, and a
  skeleton with modifiers updates twice a frame (the second time unchanged): the inertialized character's frames were
  half measured as standing still. Measured once a frame, against the cross-fade at 0.25 s: the bones' acceleration's
  median −41 % and 95th percentile −17 %, the feet sliding −10 % (not −18 %), the largest turn in a frame not better.
- Decision 0314: `FellowFootLock3D` on mocap rigs and after the inertializer.
  - The ground at rest is the feet's lowest joint when they rest more than 0.1 m below the origin (CMU's BVH rig rests
    on its hips: the lock had held nothing, 2 foot-frames in 4 800; now 3 857).
  - A held foot's velocity is kept through modifier passes with no time (zeroed, a foot let go then started back
    from standing still).
  - `release_extension` (1.0: as before): let go before the leg straightens to hold the foot.
  - The inertializer with a foot lock after it: 26 % less sliding in transitions than the cross-fade (0.78 against
    1.06 m/s), the held legs' acceleration's 95th percentile 4.2 times the inertializer's (1.7 times with
    `release_extension` 0.98, the feet held half as long).
- The transitions course: a game rig's root bone made from the mocap's hips (root motion across the ground and about
  the vertical only), the characters moved by it, every frame measured once from the pose drawn, the legs apart.

## Milestone 171 — inertialized animation transitions (2026-11-22)

- Decision 0313: `FellowInertializer3D`, a skeleton modifier. Godot cross-fades animation transitions: both animations
  evaluated for the whole fade and their poses mixed, the feet sliding and the bones' speed jolting at the fade's ends.
  With the node, the animation switches at once and is blended from the last pose drawn, carried on by its own
  velocity, to the new one's (Holden's dead blending, an inertialization): only the new animation is evaluated. Blends
  start on `blend()`, an AnimationPlayer's or AnimationTree's `animation_started` (`animation_mixer_path`), or a
  detected jump in the pose.
  - The transitions course (new, `benchmarks/tools/animation_transitions/`): 120 random switches between 128 CMU
    locomotion clips, root motion applied. Corrected in milestone 172 (the first numbers measured the inertialized
    character's frames half as standing still): at 0.25 s against AnimationPlayer's cross-fade, the bones' angular
    acceleration's median −41 % and 95th percentile −17 %, the planted feet slide 0.95 m/s instead of 1.06 (−10 %); the
    player's time 6.2 µs instead of 9.2, the modifier 2.8 µs.

## Milestone 170 — characters push rigid bodies and weigh on them (2026-11-22)

- Decision 0312: `CharacterBody3D.rigid_push_force` and `rigid_standing_mass` (0 by default: stock bit for bit). A
  kinematic character stopped at a crate or ball as at a wall and stood on a seesaw weighing nothing. With a push force,
  each rigid body a slide was stopped by gets the impulse that brings it to the velocity asked for — along the ground,
  at its centre of mass's height, its mass and inertia at that point — at most the force; with a standing mass, the
  rigid body stood on gets the character's weight.
  - The push course (new, `run_course.py --course push`, both engines; 500 N, 80 kg): 5 and 20 kg crates pushed at
    walking pace (1.47-1.50 m/s), 80 kg at 0.47-0.50 m/s against its 470 N of friction, 300 kg not at all, none
    tipped; a ball run into kicked ahead at 4.9-5.0 m/s (stock: stepped over, or shot to 8.9 m/s); a seesaw walked
    over tips ±12-14° (stock: level).
  - A step up (decision 0311) never lands on a rigid body while pushing: it is pushed instead.

## Milestone 169 — characters up and down steps (2026-11-22)

- Decision 0311: `CharacterBody3D.floor_step_height` (0 by default: stock bit for bit). Stock's capsule climbs only
  what its rounded bottom touches under the floor angle (8.8 cm at a 30 cm radius): every stair, kerb and doorstep
  above stopped it, and every step down was a fall. Above 0, a move stopped by a riser or a step's edge steps up onto
  the step (cast up, along, down; kept on a floor within the step height of the floor it stood on; `get_last_step()`
  for the camera to ease it), the floor snap reaches a step down, and a step's edge under the body counts as floor.
  - The character course (new, `benchmarks/tools/character_course/`, headless, both physics engines): stairs of
    10-30 cm walked and run up and down in 2 % more time than flat ground, never off the floor; rubble crossed; 40 cm
    stairs, a 50° slope and a ceiling too low for the first step still stop it; a 60 cm ledge is still fallen off;
    standing on the stairs the body stays (0-5.5 mm while stopping). Stock: stopped at the first riser or stone,
    9-14 ticks in the air off a 20-30 cm ledge.
  - `move_and_slide()` as fast on stairs as on flat ground; a failed step is not tried again from the same place.
- The editor's mantling character preset gets a 0.35 m step height.

## Milestone 168 — screen probes that keep their place; the clouds' detail (2026-11-22)

- Decision 0309: `sdfgi/screen_gather_persistent` (on). The screen gathers' probes keep their world point and their
  history while that point is still seen in their tile, instead of being placed again at their tile's centre pixel
  every frame. There the TAA's jitter moved an edge across that pixel on folds, leaves and slats, the probe landed on
  another surface and started over: the ray-traced gather's flicker on Sponza's curtains was white in time (lag-1
  autocorrelation −0.03 on the RTX, DDGI's +0.39).
  - GI Arcade on lavapipe: the flicker the gathers add 0.71 → 0.07 % (ray-traced) and 0.73 → 0.17 % (SDF) of a
    still view's calm pixels, the same as DDGI's (0.04 %) and SDFGI's (0.15 %); the light unchanged (0.01 stops).
- `raytracing/gi_screen_gather_feedback` (on): the ray-traced gather's hits take their further bounces from the
  previous frame's screen probes where they saw them, DDGI's probes elsewhere: GI Lab's room +0.35 → +0.25 stops
  against the path tracer on lavapipe (DDGI +0.86). In GI Arcade the ray-traced gather is 0.08 stops off a pixel.
- On the RTX (job f532, Sponza's curtains): the flicker the SDF gather adds 0.58 → 0.10 % of a still view's pixels
  (SDFGI's 0.10 %), the ray-traced gather's 7.62 → 3.44 % (DDGI's 1.55 %), its probes now keeping their history
  (lag-1 +0.38). Against the path tracer the further bounces from the probes take GI Lab's room +1.22 → +0.73 stops;
  the ray-traced gather is 0.26 stops off a pixel over 23 views (SDFGI 0.39, DDGI 1.05).
- Found: in shade (GI Lab's room, GI Arcade's gallery) the RTX's real-time GI is 0.4-1.4 stops brighter than
  lavapipe's (SDFGI, the gathers and DDGI alike) while their path tracers agree to 0.01 stops; not the resolution,
  the autotuner or FellowEngine (stock SDFGI the same on lavapipe). A GPU job is prepared (f533).
- GI Arcade (`--scene=gi_arcade`): a colonnade with folded double-sided curtains, a tree of 600 leaves and a shutter's
  slats, for the gathers' stability without Sponza's assets.
- Decision 0310: `post/clouds/detail` (on). The clouds' detail noise had eroded every edge alike (its octaves' sum
  varies by a tenth: now stretched to [0, 1]) at features of a pixel or less (now 150-600 m), under a march of 64
  even steps of several hundred metres: now coarse steps through clear air and fine ones inside the clouds (25 m near
  the camera), the billows shading one another and Schneider's powder term away from the sun.
- The showcase's look beside the owner's reference: the light's balance (the sky's own ambient light, a stronger sun,
  a quarter of the haze); a thin layer of stratocumulus (the owner's choice); a sky lit by a sun 10 degrees up over a
  square lit from 27 (a light only the sky sees: the golden hour's sky without the climb's wall falling into shade),
  four times as bright as the sun's energy made it (its light on the scene, its sun's disk and the light shafts as
  before), the square's sunlight coloured by the same air.

## Milestone 167 — diffuse light gathered per screen probe (2026-11-21)

- Every diffuse GI option measured against a path tracer (`debug/raytracing_gi_reference`) in Sponza, Bistro and the
  new GI Lab (`benchmarks/tools/gi_reference_compare.py`: linear EXR captures, the luminance's log ratio in stops).
  No GI is 1.6 stops too bright, SSAO and SSIL take back a tenth of it, ray-traced DDGI is a stop too bright (its 2 m
  probes interpolate light they see and the shaded point does not), SDFGI is right on average and 0.41 stops off a
  pixel.
- Decision 0308: `sdfgi/screen_gather`. Where the environment enables SDFGI, its diffuse light is gathered per screen
  probe instead of interpolated from its world probes:
  - a probe every 16 x 16 pixels traces 16 rays a frame from its surface through SDFGI's distance field cascades, and
    a hit takes the light of the voxel it reaches;
  - the probes are averaged over frames where their surface stays in view and over their neighbours on the same
    surface, and written into SDFGI's diffuse buffer (its reflections stay).
  - 0.34 stops off a pixel from SDFGI's 0.41; in GI Lab 0.10 from 0.41 (its room, lit through a window, 0.18 from
    0.76); a quarter of SDFGI's leak through 10 cm walls. +0.3 ms at 1080p; no ray tracing needed.
- `raytracing/gi_screen_gather`: with ray-traced DDGI, the same probes trace their rays against the ray tracing scene,
  the hits lit as DDGI's (DDGI's probes for the further bounces), without the sky leak. 0.29 stops off a pixel from
  0.94-1.10, +0.7 ms at 1080p; it still flickers on Sponza's normal-mapped curtains (a temporal filter of its resolved
  light is next).
- Tried and removed: DDGI's probes traced through SDFGI's fields (as bright as ray-traced DDGI).
- GI Lab (`--scene=gi`): a sunlit room with coloured walls, a sealed box, colour bleeding outdoors, a pan through the
  room.

## Milestone 166 — physical animation (2026-11-21)

- Decision 0307: `FellowPhysicalAnimation3D`, a new node. Placed on a skeleton after the modifiers that animate it, it
  builds the bones' rigid bodies at run time on the stock `PhysicalBoneSimulator3D` and drives them every physics step
  towards the animation: the animation's motion fed forward, half the error taken back, the gravity allowed for, the
  strength cubed as the step's share.
  - With nothing pushing, the bodies follow a walk within 3-4 mm.
  - `apply_hit()` knocks the struck part (10 cm for a 40 N·s push at the chest, 21 cm for 20 N·s at the head) and
    weakens it and its neighbours. The hips barely move, and every body is back within 3 cm in 0.3 s.
  - Strength 0 is a ragdoll that falls and lies still on Jolt Physics. Godot Physics' joints jitter: the node warns.
  - The joints are pins while driven and cones about the pose it went limp in. The bodies start at the first animated
    pose, and one held far from its bone is put back.
- `bench/physical_check.gd` (tracking, hits, ragdoll, cost), `bench/physical_video.gd` (a film).

## Milestone 165 — motion matching's orientation warping (2026-11-20)

- Decision 0306: `FellowMotionMatcher3D.orientation_warping`, on by default. When the clip found goes another way
  than the simulation (a strafe between the database's, a walk at an angle to the facing), the hips turn towards the
  simulation's way and the spine turns back over its bones, so the chest keeps the facing; the root motion turns with
  them. Matching Lab:
  - walkers strafing (facing the centre): the feet's sliding 0.158 → 0.127 m/s, the trajectory error 9.1 → 7.7 cm;
  - walking as before: the trajectory error 4.3 → 3.8 cm, the lean in turns 67 → 71 % of balance's.
- Matching Lab's `--matching_orient`, `--matching_orient_max`, `--matching_strafe=1`; `matching_video.gd
  --follow=walker`.

## Milestone 164 — motion matching's AAA layer (2026-11-20)

- The owner turned from the showcase to the engine ("the animations are not nice … we must work on the engine
  itself"), starting with animation, without the GPU. Decision 0305: `FellowMotionMatcher3D` gets what makes motion
  capture look grounded, on by default. Matching Lab, 16 characters on the CMU database:
  - transitions only when they are worth their blend (`transition_cost_bias`, `transition_jump_threshold`):
    4.9 → 2.4 a second;
  - the stride warped to the character's speed, the hips lowered to reach (`stride_warping`): the feet's sliding in
    contact 0.109 → 0.062 m/s, halved on slopes, stairs and uneven ground too;
  - the body leaning into turns and starts as balance asks, over planted feet (`lean_amount`): its lean 37 → 67 % of
    `atan(a / g)`, error 3.7 → 2.8°.
- The body's lean against balance's in the stats; Matching Lab's and `ground_check.gd`'s options; `matching_video.gd`
  films a character. The popping measure no longer counts a return from a lower LOD as a frame's jump.

## Milestone 163 — the reference's camera smooth over the eave; the tiles smooth clay (2026-11-20)

- Decision 0304, on the owner's word: "why are the tiles not smooth like the reference's? why does the camera jump
  while he pulls himself onto the roof?"
  - The camera: where it rises over the eave (frames 76-85) the matchmove's tracks hand over from the wall to the
    roof, and its free solve had jittered and jumped (8 m/s, 83°/s for a frame). It is re-solved against its own
    points, held still, its path held smooth. Those frames move onto the even path the video's background takes; the
    others stay. The background's motion now changes from frame to frame by at most 8.3 px, the video's 8.4 (51.8
    before). The climber is refitted through it, the scene's scale held.
  - The tiles: smooth fired clay (`clay_tile.gdshader`), with no photograph. Each tile has its own shade (its id in its
    UVs, from `Builder.coppo`) and a soft sheen. The valleys and the crevices under each course are dark, and the lower
    ends are rounded over. A coppo's lip is kept sharp against its arch.
  - 38 of 38 animation gates.

## Milestone 162 — the reference's ground, tiles and feet (2026-11-20)

- Decision 0303, on the owner's word from the reference's frames:
  - The ground: the square and the reference's street laid with oblong setts in running courses (Poly Haven's
    `square_cobblestone`, fetched as `setts`): warm, their courses straight, their grain softened, no travertine bands,
    running to the reference house's wall.
  - The tiles on every roof: wide, low rounded covers in columns with dark valleys and stepped courses, each tile one of
    three firings (`Builder.coppo`'s `flat`, `coppo_b`, `coppo_c`). Their geometry is halved where nothing shows.
  - The mannequin's feet: a slim slipper (9 cm across, the forefoot 6 cm high), the ankle smaller and half sunk.
  - 38 of 38 animation gates.

## Milestone 161 — the reference's surroundings opened (2026-11-20)

- Decision 0302: behind the reference shot's runner the open square, not the stall and its barrels (moved to the
  square's north-west side); south of the reference's house a low house, so that the roof's view goes over it. The
  video's low, grazing sun was tried from four directions: in our square its south side shades the house's front, so
  ours is kept. 38 of 38 animation gates.

## Milestone 160 — the reference matchmoved: its camera, its house, its climber (2026-11-20)

- Decision 0301: the reference video's camera solved from its own background — structure from motion (in the cloud):
  11 722 points over all 96 frames, 0.50 px. It cranes straight up 2.1 m out from the house's wall, where decision
  0297's camera had moved in along it; on the roof its turn is 30-40° off.
- The house measured through it, on its front rectified onto a metric grid: 6.64 m wide, an arched door under a tall
  shuttered window, the climbed window's sill 2.01 m up over a balconette on two corbels with a mask, the gutter
  3.85 m up. `casa_riferimento()` is built from those measures (`reference_performance.json`'s `"house"`).
- The climber fitted in the world through that camera, its holds on the replica's surfaces, the scene's scale against
  the mannequin solved. The street before the house is flagged, with no posts and no raised pavement.
- Beside the video, with its edges drawn over FellowEngine's frames: the windows, the balconette, the eave and the
  climber lie on the video's, frame by frame.

## Milestone 159 — the reference's light (2026-11-20)

- Decision 0300: the rooftops scene's light as the owner's reference video's — soft, warm, bright in the shade.
  - Measured against the video's frames: our luminance's percentiles 0.37-0.76 of the video's → 0.91-1.05, the fine
    detail's contrast 1.59 → 1.31 times, the saturation 1.27 → 0.97 times.
  - The light: a soft sun (2.5° across), the sky's and bounce's light doubled, exposure 1.3, less SSAO, contrast and
    saturation.
  - The ashlar dressed and warm (only its joints sunk by the parallax). The coppi are tapered tiles with lips over
    pan tiles, in smooth terracotta (`Builder.coppo`). The glass is dark (no direct specular, the sky's reflection
    mostly hidden, no lamps by day).

## Milestone 158 — the reference's house finished as the video's; its pelvis (2026-11-20)

- Decision 0298: the replica of the reference's house, and the square's houses, beside the video's frames.
  - Stone in relief: parallax occlusion mapping in `facade_pbr.gdshader` from Poly Haven's height maps
    (`fetch_textures.py` fetches them), with the blocks at the video's size.
  - Louvred shutters and casements on the windows; real coppi over every roof face, with their collision.
  - Carved scroll corbels under the balconette; a thin zinc gutter; dark glass.
  - A softer grade and the video's depth of field on its camera.
  - `tools/reference_look.gd` sets the shot beside the video's frames.
- Decision 0299: the reference's pelvis.
  - The fit's pelvis had drifted up to 52° from the trunk, since its two landmarks cannot tell its tip. Now the
    spine's bend and the hips' flexion are held to a body's reach: the bend is -7…+21° over the 96 frames, the hips at
    most 129°, and the landmarks fit as before.
  - The mannequin's pelvis is shallower behind, with smaller buttocks and the briefs cut higher.
  - 38 of 38 animation gates.

## Milestone 157 — the owner's reference performed 1:1 (2026-11-19)

- Decision 0297: the reference video's motion measured, not drawn by eye. In the cloud (MediaPipe Pose, OpenCV, SciPy;
  nothing of them in the repository): 33 landmarks a frame on its 96 frames; the camera's turn and focal length from
  the city's lines (1300 px, 52.4° across); the mannequin's own skeleton fitted to every frame at once through that
  camera (40 parameters a frame, the joints 7.2 px off on average); which hand or foot holds still from the optical flow
  around it; the hips' path from the holds (the run 3.4-3.6 m/s, the balconette 1.70 m up, the eave's lip 3.25 m).
- In the repository: `showcase/tools/blender/reference_performance.json` (the measured performance: every segment's
  rotation and the hips' place per frame, the camera, the holds) and `mannequin_reference.py` (the clip `reference` at
  60 Hz and the camera's path, `ReferenceShot.glb`: the holds kept still by IK smoothed over a twentieth of a second,
  the fastest joint 1517°/s); `ac_buildings.casa_riferimento()`, the video's house to its measures, closing the
  square's west street; the demo's `reference` shot plays it through its camera (`--demo_only=<shot>` plays one shot).
  The shot rendered by FellowEngine beside the video, frame for frame.

## Milestone 156 — the owner's reference video: the mannequin, its run, its scramble up the tiles; no pops (2026-11-19)

- Decision 0296: the rooftops showcase after a 4-second video the owner chose (a grey articulated mannequin running
  across a stone square, up a house's front, over the eave and up the tiles on hands and feet), compared frame by
  frame. The mannequin rebuilt on the same skeleton (`tools/blender/mannequin.py`): a breastplate, the abdomen and a
  pelvis cut like briefs with thin dark seams over a dark core, a large egg of a head with a face's plane, a dark neck
  and shoulder sockets, a grey ball for the knee between dark rings, heavier limbs, shoes, mid-grey. The run leaning
  as the reference's (the trunk 19.7° running, 5.8° before), the forearms in front of their own shoulders (the forward
  hand had been up by the face, across the chest); a new clip and controller state: up a roof on hands and feet. The
  houses of sandstone ashlar, the shade lighter. The demo's house filmed as the reference is (`orbit`: behind the
  runner as it comes at the wall, below the climber, beside the body on the tiles).
- No pops, measured: the quality loop reads the fastest joint each frame and the hips' acceleration; six new gates
  (the climbing's segments ≤ 2200°/s; the mantle's body ≤ 400 m/s²). The pops found and their causes fixed: the clips'
  rig blends a limb from FK to IK (it swapped halfway); `FellowTraversalPose3D.clip_blend` (a mantle fades in from the
  braced pose), `pole_follow` (the bend follows the clip's where the pole's turns over), `max_joint_speed` (no bone
  turned faster than this while the traversal poses the body) — all off by default; `FellowTraversal3D` lets a mantle's
  body go from the wall over 18 % of it (it had jumped 0.65 m in two frames over an eave); `FellowFootLock3D` never
  eases a foot back from beyond the leg's reach. The mantle's fastest joint 8863 → 2000°/s, hold to hold 4428 → 2087;
  38 of 38 gates pass (milestone 155: 29 of 32). `climb_check.gd` and `traversal_check.gd` value for value; the
  regression scenes exact.

## Milestone 155 — legs that bear weight; the showcase's animation measured in a loop (2026-11-19)

- A quality loop for the showcase's animation (`docs/showcase_animation_quality.md`): `showcase/tools/anim_quality.gd`
  reads the bones after the modifiers through fixed programs (the gaits and the changes between them, a stop, a turn,
  the climb up CasaCrema) and `anim_quality.py` holds 32 gates from gait analysis — the knee's, hip's and ankle's
  flexion over the stride, straight knees, the planted foot's skate, the feet in the ground, the joints' speed through
  a change, the climbing's knees — against a baseline build.
- Decision 0295: the owner's "over-straightened legs" measured (the walking knee within 5° of straight in 57 % of the
  frames, running 31 %, mantling 25 %) and their cause found (the stance ankle asked to go 1.0 m from the hip on a
  0.86 m leg); the gaits rebuilt from the supporting leg, soft IK, and `FellowFootLock3D` (a new node: each foot held
  where its sole comes down, only its slide taken out); `FellowTraversalPose3D.soft_ik` (off by default). Straight
  knees 0 % in every gait and in the mantle; the feet dragged 1.7-2.1 cm through a change of gait (7.7-20.6). 29 of the
  32 gates pass (14). `climb_check.gd`: events, places and times value for value on GodotPhysics3D and Jolt; the
  regression scenes exact.

## Milestone 154 — a camera that does not shake; gaits in step; no step off a roof; the eyes on the next hold (2026-11-19)

- Decision 0292: `FellowTraversalPose3D` places the hands and feet relative to the skeleton where it is drawn (its
  interpolated transform; with physics interpolation off, the same as before). The rooftops showcase interpolates its
  player between the physics ticks and advances its animation in the idle frames: at 144 Hz over 60 Hz physics the
  body and the camera move 3.06 cm every frame running at 4.4 m/s (before: 0 to 7.3 cm — the camera shook).
- Decision 0293: the traversal's hints say where the climber looks (`look`, `look_weight` in `get_pose_targets()`):
  the next hold from a move's start, along the ledge or up the wall the way the controller wishes while hanging, over
  the top through a mantle. The showcase turns the head to it with Godot's `LookAtModifier3D`, and on the ground to
  where the controller steers. `climb_check.gd`: `leap_style` checks the look; the 37 lanes value for value on
  GodotPhysics3D and Jolt; the regression scenes exact.
- Decision 0294 (the showcase): the gaits' cycles normalized and blended at the same moment of the stride, played at
  the cycle's rate for the body's speed — the planted foot slides half as much or less between gaits (walk to run 0.95
  → 0.50 m/s, run to sprint 1.29 → 0.35, stopping 1.44 → 0.73); the body leans into turns and with its speed's changes;
  walking or running it no longer steps off an edge (stopped at it, or along it at a slant; a beam walked along;
  sprinting still goes over).

## Milestone 153 — the body's weight while climbing; the city's houses built (2026-11-19)

- Decision 0290 (opt-in): a leap from a hold winds up, springs from rest and is caught with its speed, its hands thrown
  to the ledge ahead of the body at full IK weight and its feet pushing off the wall and braced again
  (`FellowTraversal3D.hang_leap_windup`); the hips on a damped spring behind the body — they swing on when a leap is
  caught and lag when it starts, never lower than the arms reach — and the limbs' targets eased
  (`FellowTraversalPose3D.body_sway`, `sway_frequency`, `sway_damping`, `sway_max`, `limb_smoothing`); a clip of its own
  for a mantle from a hang (`hang_clip`). `climb_check.gd`: a new lane `leap_style`; the other 36 value for value on
  GodotPhysics3D and Jolt; the regression scenes exact.
- Decision 0291 (opt-in): the hands on the stone — the traversal's hints say which hand grips its hold (`hand_grips`);
  `FellowTraversalPose3D.hang_fit` moves the hanging body until the shoulders are within reach of the gripped holds (the
  showcase's hands had been 20-35 cm short, floating before the stone). The showcase's demo no longer turns the climber
  round in the air, its back to the wall.
- The rooftops showcase: the hang braced lower (the feet under the hips, not drawn up to the chest) and its clips remade;
  leaps that wind up, fly with the legs together and swing in to the wall when caught, scaled to each move's time; a
  climb up from a hang (pulled up, a knee onto the top, standing up); a jump at a wall with the arms reaching for the
  hold; eased cross-fades. The city beyond the square rebuilt: 794 houses (30 prototypes, a near and a far set, as
  instances; 1.1 million triangles) with windows in their openings, shutters, sills, string courses, cornices, eaves on
  rafters, tiled hip and gable roofs with their ridges' tiles, terraces with altane, loggias, balconies and chimneys,
  each its own plaster's colour — in place of boxes with windows drawn on them; the golden hour's haze over the distance.

## Milestone 152 — climbing across, on narrow holds, over eaves and parapets onto the roofs; the RTX's rare failure narrowed (2026-11-19)

- Decision 0284: the hot workers' gate forced (`debug/gate_open_every`: one frame in N, 1 always open, -1 always
  closed) and its last 64 frames in a late fence's report. The nine failures of decision 0270 read again: each failing
  frame had the gate closed — its transparent pass replayed whole into the primary command buffer, not into secondary
  ones as decision 0283 had read it. `f516` (the gate held open): no failure in 30 runs. `f517` (stopped after 33
  runs each when the owner needed the laptop): held closed, 4 failures in 33, in runs where the gate never opened;
  as it is, none in 33. The failure is the closed frame's own path (cold workers, the whole replay, the render
  thread's submission); `f518` splits them when the GPU is free again.
- Decisions 0285-0289, climbing from hold to hold (`FellowTraversal3D`, opt-in): across a gap to a ledge to the side, or
  up and to the side, hand over hand (`hang_reach_side`, `TRANSFER_REACH_SIDE`); narrow holds held with the hands
  together (`hold_min_width`, `spread`); a hold under an eave caught and the eave's lip reached, near or far out, and over
  it onto the roof (the mantle's room on a sloping top had been wrong over 23°); a hold up and beside the hands reached
  across; holds close over each other found; a hold needs room for the hands; over a parapet from a hang onto the
  terrace behind it. `climb_check.gd`: 36 lanes (seven new), the 29 of milestone 151 value for value on GodotPhysics3D
  and Jolt; `--only` and `--times` for picture strips.
- The rooftops showcase: the pediments' bases and the eaves' fascia in the houses' collision (models and scene
  rebuilt); CasaCrema climbed from the street to its roof in 5 s; the player reaches across 0.8 m.

## Milestone 151 — climbing from hold to hold; the rooftops' people, photographs and ring of houses (2026-11-19)

- Decision 0282: climbing from hold to hold, as in Assassin's Creed (opt-in: `FellowTraversal3D.hang_reach_up`,
  `hang_brace_height`) — hanging, a ledge within `hang_reach_up` is climbed to hand over hand (`TRANSFER_REACH_UP`,
  0.75 s: the hands in turn, the body pulled up, the feet stepping up the wall) by pushing up or at the wall, one further
  up still leapt to; the body stands out from a ledge past a hold below it that stands out more (0.08 m steps to
  0.32 m) and stays out over its top; pushing up climbs onto what the hands hold; the feet braced on the wall found
  under the ledge; `hang_transfer_started` for the move's clip. `climb_check.gd`'s new `hold_climb` lane: caught, a
  reach, a leap, a reach past a corbel, over the top at 7.4 m, never in the wall (GodotPhysics3D and Jolt); the other
  lanes value for value; the regression scenes `climb`, `ledge_corners` and `traversal` exact.
- The rooftops showcase: the buildings climbed on their holds (sills and hoods, impost bands, the rustication's top,
  string courses, the bell tower's corbels and beams) with braced hanging, reaching and leaping clips — not free
  climbing; the square's people (passers-by, groups, people on the stone benches, stall keepers: tinted mannequins,
  robes, hats); a ring of houses round the square and along its streets; photographic materials from Poly Haven
  (CC0, `tools/fetch_textures.py`, outside the repository) in place of the procedural ones when fetched; the hay's exit
  on the physics ticks (no longer under the street), no screen-space reflections, SDFGI.

## Milestone 150 — climbing past courses and plinths; the rooftops showcase (2026-11-18)

- Decision 0281: free climbing past what stands out of the wall — an underside (a string course's, a cornice's) is no
  longer a face in the way, and the capsule stands out from the face as far as it must to be free (0.08 m steps up to
  0.48 m, for what is 0.3 m ahead too), eased back in. `climb_check.gd`'s `climb_course` and `climb_plinth` climb and
  mantle (they stopped at 0.9 m and 0.78 m); the other lanes value for value, on Jolt all pass; the regression scenes
  `climb` and `ledge_corners` exact.
- The showcase's rooftops (`showcase/rooftops/rooftops.tscn`): a Renaissance square at the golden hour in the manner of
  Assassin's Creed — its buildings, props and city modelled by code in Blender (`showcase/tools/blender`), a mannequin
  with 20 clips made there (gait cycles with IK-planted feet, mantles with root motion that `FellowTraversal3D`
  scales onto the ledge, hanging, climbing, the leap of faith), procedural materials, paving by zone, the Tuscan
  country with its trees, cloth, birds; the player on `FellowTraversal3D` climbing the 38 m bell tower to its belfry,
  a damped third-person camera, and a demo of shots (`-- --demo`).

## Milestone 149 — the head following the gaze; a jump round a corner; hair shadowed by lamps (2026-11-18)

- Decision 0278: `FellowHeadMotion3D.follow_gaze` (opt-in) — the head turns towards what its face looks at (past
  `gaze_dead_zone`, `gaze_follow` of the rest, up to `max_head_yaw` / `max_head_pitch`, eased over
  `gaze_follow_time`) and tells the face (`FellowFaceAnimator.set_head_turn()`), whose eyes turn the rest: a target
  60° to the left, the head 34.9° and the eyes 25.1° (without it the eyes stop at 30°). `face_check.gd`'s
  `head_gaze`; the earlier lanes and the regression scene `face` as before.
- Decision 0279: `FellowTraversal3D.hang_jump()` round a corner — hanging, the wish along and no edge along it: a ledge
  on the side of the wall past the corner, the body round it in the air (`TRANSFER_JUMP_CORNER`), turning a quarter,
  the wish held going on along the new edge. `climb_check.gd`'s `hang_jump_corner`; the other lanes and the regression
  scenes `climb` and `ledge_corners` as before.
- Decision 0280: `FellowHair3D.self_shadow_light` may be an omni or spot light — the hair's deep opacity map along the
  direction towards it, the material dimming that light: a lamp where the sun was, the same map and the hair dimmed as
  by the sun. `hair_check.gd --lamp=<m>`; the regression scene `hair` exact.

## Milestone 148 — free climbing; sound round edges (2026-11-18)

- Decision 0276: `FellowTraversal3D.free_climb` — a body pushing at a climbable face (`climb_mask`) with nothing to
  mantle onto climbs it: up and down, along it (the wish held turning with the face), round towers and corners, over
  its top (a mantle, or a hang with no room there), down to the floor, lunging over it with `hang_jump()`
  (`climb_jump_distance`, as far as the face goes); `climb_speed`, `climb_max_overhang`, `try_climb()`,
  `is_climbing()`, `climb_started` / `climb_finished` / `climb_jumped`. The mantle's path now keeps the capsule off a
  slab's face and on a sloping top's plane. `climb_check.gd`'s ten new lanes on both physics engines; the earlier
  lanes, `traversal_check.gd` and the regression scenes `traversal` and `climb` as before; 18 µs a climbing body a
  frame.
- Decision 0277: `FellowAcoustics3D.edge_diffraction` (opt-in) — a blocked source with no opening is heard round the
  edge of what blocks it (its side or its top) when louder that way, from the edge's direction; `edge_reach`,
  `edge_diffraction_db`. A source behind a 2 m pillar −21.3 dB with 46 % of its highs, from the right, instead of
  −40 dB through it; 64 sources +45 µs a frame.

## Milestone 147 — tracked vehicles and two-wheelers; the far shadow cascades without instances out of range; a garment's backstop (2026-11-18)

- Decision 0272: a directional cascade that begins beyond an instance range's end (the terrain's foliage, HLOD's
  instanced proxies and impostors) leaves those instances out — none of them is drawn that far (Frontier's and Slice's
  grass, to 80 and 90 m, no longer drawn into the cascades out to 400 and 300 m). Per cascade, so the shadow cache keeps
  it; `shadows/instance_range_cascades` (on); the statistic `shadow_range_skips`. Lavapipe: the same pictures; GPU
  timing pending. `terrain_check.gd --shadow_distance --foliage_kinds --grass_range`.
- Decision 0273: `FellowCloth3D.skin_backstop` / `skin_backstop_distance` — a skinned garment's free particles kept
  from going more than a distance behind their skinned places along the skinned normals: `cloth_check.gd`'s skirt
  without the hips' collider 5.9 cm into the hips, with the backstop (5 mm) none, at no measurable cost.
- Decision 0274: `FellowVehicle3D.steering_mode` `STEERING_SKID` (a steering gear holding the inner side's speed at a
  ratio of the outer's: straight, about the inner side, on the spot; at speed a share of the tightest turn the grip
  holds), `tracks` (each side's wheels turning together), `skid_turn_drive`; the FellowEngine menu's "Add Tracked
  Vehicle". `vehicle_check.gd`'s tracked vehicle: straight to 0.000°, 26°/s on the spot, 0.43 g at 30 km/h with full
  steering; the car's lanes as before.
- Decision 0275: `FellowVehicle3D.balance` / `max_lean` / `is_fallen()` — a two-wheeler balanced by its rider: the
  steering asks for a lean (within the grip and the lock), held by a torque about the forward axis, the front wheel
  turned to the turn the lean balances; upright at a standstill. The FellowEngine menu's "Add Motorcycle".
  `vehicle_check.gd`'s motorcycle: the lean as asked (19.35° for 19.33°, 38.66° for 38.66°), the turn's g the lean's
  tangent within 3 %, righted after a push that topples it without balance; GodotPhysics3D and Jolt alike.
- Decision 0270 continued: GPU checkpoints (`debug/gpu_checkpoints`) and the render graph's reordering and full
  barriers as settings (`debug/graph_reorder`, `debug/graph_full_barriers`); the hang caught again with the reordering
  off and with checkpoints — the GPU stopped inside one draw list (command 69 of its frame); a draw list's checkpoint
  now names its region, size and whether worker threads recorded it. The GPU paused before the next catch.

## Milestone 146 — a late fence reported; the terrain holes on Intel fixed (2026-11-18)

- Decision 0271: the terrain's holes' material discards at the end of its fragment, after its texture fetches and
  derivatives (undefined in a quad with a discarded fragment) — the holes' rims on the Intel GPU differed by 3-5
  levels from frame to frame and run to run; now every frame is the same. `terrain_holes`' golden blessed again (the
  rims, 0.08 % of the picture).
- Decision 0270: a frame's fence not signalled within `debug/late_fence_report_s` (2 s) is reported — the frame slots,
  the present and submit thread's jobs, the transfer workers, the Vulkan driver's swap chain semaphores, which of the
  frame's pass timestamps the GPU wrote — then waited for as before; a fence wait that finds the device lost prints
  `VK_EXT_device_fault`'s report. Diagnostics, off: `debug/sync_log` (the Vulkan driver records every submission,
  presentation and acquisition in order and checks each semaphore wait against the signals before it and each fence
  wait against the submissions; acquisitions get a fence that says whether the image was released; `regression.py run
  --sync-log`), `debug/gpu_checkpoints` (NVIDIA's markers before each command of the frame), `debug/graph_reorder` and
  `debug/graph_full_barriers` (Godot's compile-time switches of the render graph). FellowEngine's statistics count
  `late_fences`, `sync_log_anomalies`. Milestone 145's hang caught three times: at the same frame of Bistro's flight,
  every wait signalled, the GPU stopped inside the frame's work; none without the present thread or with the pass
  timestamps (too few runs to tell). Open.
- Decision 0268 on the RTX: the hair's self-shadowing costs the GPU nothing measurable (24 heads), +0.23 ms on the
  main thread's hair group. `hair_check.gd --scalp_mesh=1 --sun_turn=<degrees>`; `hair_crowd_check.gd` times the GPU.
- FellowBench: the stats' differences no longer fail on a boolean statistic (`memory_budget_from_driver`).

## Milestone 145 — Bistro streamed: the longer rare frames, and a hang (2026-11-18)

- Decision 0269: Bistro exterior's streamed textures' churn (4 800 loads and 2 200 releases in a 30-second flight,
  each swap remaking its materials' uniform sets) made its rare frames longer; releases held 10 s instead of 2 s:
  p99 7.3 → 5.9 ms, the render thread's 3.6 → 2.7 ms, 50 MB more video memory. Not the swaps' bursts (a per-frame cap
  tried and removed) nor the evaluation. The default stays 2 s: three of 28 runs held 10 s hung (a frame's fence never
  signalled, the GPU idle), under investigation. FellowEngine's statistics count `material_uniform_sets_created`;
  `benchmarks/tools/crash/minidump_stacks.py` reads every thread's call chain from a crash or hang report's minidump.
- The terrain holes' differences on Intel are from frame to frame within a run too, with FellowEngine on only.

## Milestone 144 — hair shadowing itself (2026-11-18)

- Decision 0268: `FellowHair3D.self_shadow` (opt-in; on in the "Add Hair" preset) — the hair shadows itself from a
  directional light: each frame its guides' points splatted into a deep opacity map across the light (cells twice the
  guides' spacing, 32 slices along it; what each point's strands cover, by the segment's sine to the light), each
  point's share of the light `exp(-self_shadow_density × the strands in front)` in the guides' texture, the strands
  dimming that light's diffuse, specular and transmitted light by their guides' shares. On the worker threads with the
  simulation, about 75 µs a head; without it the material and the pictures as before (the regression suite exact).
  `hair_check.gd --self_shadow=1`, `hair_crowd_check.gd --self_shadow=1`.

## Milestone 143 — milestones 137-142 on the laptop's GPUs (2026-11-18)

- On the RTX: Bistro exterior's DDS textures streamed (decision 0264) load in 1.5-1.7 s instead of 2.9-3.4 s with
  1.70 GB of video memory instead of 3.08 GB, the frame +2 % (the rare long frames 2 ms longer: next); Crowd Lab with
  `animation/update_lod` (0265) −32 % frame time; Frontier Lab's groups in two levels (0262) the same draws in its
  flight (the second level shows from outside the world); milestone 33's device loss (0133, 0267) no longer happens
  with the clouds' shadow map written and read in one frame — with a build from before milestone 142's fixes too: the
  double buffer kept.
- On the Intel GPU: City Lab 13 % faster than stock once the GPU's clock has risen (milestone 136's +7 % was its ramp);
  `run_benchmarks.py`'s heat soak runs the first build as it will be measured (it had heated the RTX). The terrain
  holes' run-to-run differences there narrowed to FellowEngine's renderer and the patches over the holes, none of 23
  settings off alone removing them.

## Milestone 142 — the frame under Vulkan's validation (2026-11-18)

- Decision 0267: the image regression suite, PBR Lab and five labs run under the Khronos validation layer's core and
  synchronization validation on lavapipe; four invalid synchronizations fixed, the same pictures bit for bit — the
  bloom's mips' first transition each frame without the stage of the clear that preceded it, the terrain virtual
  texture's sRGB view of an atlas created without it, an upload's transition to the shaders' read layout with no
  destination access (stock Godot's code: a read-after-write hazard), and a buffer's read-to-read barrier with a
  shader's read as source access and a copy's stage (the graph's code is stock's: a MultiMesh copied into the culling's
  pool). Afterwards nothing but the layer's own message. `debug/cloud_shadow_same_frame` (diagnostics) for milestone
  33's device loss: no hazard on lavapipe, the double buffer stays. The suite's README: its run under validation.

## Milestone 141 — streamed meshes in three levels (2026-11-17)

- Decision 0266: `bake_proxies()`'s `mesh_middle_ratio` (0: none) saves a middle level of each streamed mesh;
  `FellowWorldStreamer3D.mesh_middle_distance` shows it between `mesh_distance` and that distance, loaded and swapped
  as the full mesh is. World Lab with curved props, seen over the roofs: the full meshes within 20 m and the middle
  level within 70 m 25.1 MB of buffers instead of 33.3 MB (full within 70 m), 91 more of 518 400 pixels off the world
  all loaded. `world_proxies_check.gd --mesh_middle_ratio --mesh_middle_distance --rocks --ground`.

## Milestone 140 — far characters animate less often (2026-11-17)

- Decision 0265: `animation/update_lod` (opt-in) — an AnimationPlayer or AnimationTree in the idle or physics process
  evaluates every frame within `update_lod_distance` (20 m) of its camera, every 2nd, 4th, … frame farther (at most
  every `update_lod_max_interval`-th, 8), every `update_lod_offscreen_interval`-th out of view, each on its own frames,
  a skipped frame's time added to its next evaluation. Crowd Lab's 256 characters on a 120 m field: 50 evaluations a
  frame instead of 256, the main thread's process step 3.4 → 1.2 ms; the near characters unchanged.
  `bench/animation_lod_check.gd`, Crowd Lab `--crowd_process=1`.

## Milestone 139 — DDS textures streamed (2026-11-17)

- Decision 0264: with `streaming/textures`, a 2D `.dds` file of a compressed format with its full mip chain (loaded by
  Godot itself, as Bistro's) reads only its tail and registers its chain with the RenderingServer, streamed like an
  imported texture. Streaming Lab as DDS: the same residency as its `.ctex` files at every camera (13.5-42.9 MiB of 384
  MiB at street level), loaded in 11 ms instead of 1 077 ms. Streaming Lab `--streaming_dds=1`,
  `world_proxies_check.gd --dds=1`.

## Milestone 138 — proxies' textures streamed (2026-11-17)

- Decision 0263: `bake_proxies()`'s `texture_files` (on) writes each textured proxy's, group's and level's texture
  beside it as `<proxy>_albedo.ctex`, a `CompressedTexture2D` its material reads — streamed with `streaming/textures`
  like any imported texture. World Lab's 16 × 16 cells from outside: 6.9 MB of the proxies' 24.2 MB of textures
  resident, the same picture; every lane bit for bit the same as with the textures whole (without TAA).
  `world_proxies_check.gd --texture_files`, `--taa=0`, `proxy_textures` with streaming on.

## Milestone 137 — levels of proxy groups (2026-11-17)

- Decision 0262: `bake_proxies()`'s `group_levels` (1: as before; at most 4) groups the groups' proxies again, level by
  level, each set with one coarser proxy baked from the proxies below it (their meshes and textures; a terrain's edges
  kept), drawn from where all of them would be, their visibility parent, hidden while any of them is. Its objects keep
  at least what an error of `terrain_error` pixels needs there, its texels take the surfaces facing them. World Lab's
  16 × 16 cells seen from outside with three levels: 8 draws and 31 k triangles instead of 128 and 67 k, as close to
  the world all loaded. Every proxy's faces now face the right way (a simplified triangle's normal had taken its side
  from one of its welded corners' faces: walls lit from behind) — World Lab's streamed view 1.05 → 0.42 % of the
  picture off the world all loaded. The editor's dialog has the levels; `world_proxies_check.gd --group_levels
  --far=1` (the far views without TAA), Frontier Lab `--frontier_group_levels`.

## Milestone 136 — a second GPU: the laptop's Intel GPU (2026-11-17)

- FellowEngine's first runs on another vendor's GPU and driver (the Core Ultra 9 275HX's Intel GPU, `--gpu-index 1`).
  A crash found and fixed (decision 0261): Intel's driver crashed reading the Vulkan pipeline cache for its periodic
  save while other threads created pipelines — stock Godot 4.7.2 too; `frame/pipeline_cache_exclusive` (on for Intel
  GPUs) reads it only while none is being created (`bench/pipeline_cache_stress.gd`: 10 of 10 runs crashed without, 0
  of 10 with). The regression suite's 38 scenes on Intel within a level of lavapipe's on average; FellowEngine's
  defaults faster than stock in 8 of 9 scenes at 720p (Sponza −49 %), City Lab 7 % slower. `debug/wait_pipelines` and
  the suite's `--wait-pipelines`; `FELLOW_GODOT_ARGS` for the agent toolkit.

## Milestone 135 — fading instances' shadows fade (2026-11-17)

- Decision 0260: the shadow passes dither the fades the view does — by distance with `geometry/dithered_fade`, and the
  shares of decision 0258's cross-overs — by the main camera, with the same noise and side: a fading caster's shadow
  fades with it and two handing over cast one shadow (directional cascades, spot lights, omni lights' dual paraboloids;
  an omni light's cube map whole while fading). Fading casters leave the directional caches meanwhile and redraw their
  positional lights' shadows; a caster faded out entirely is not drawn. A streamed mesh's swap keeps one shadow. The
  image regression suite exact. On the RTX: 8 000 casters mid-fade the same GPU time (+0.6 ms of the renderer's CPU);
  Frontier Lab crossing over 4.15 ms against 4.24 ms. `bench/dithered_fade_check.gd` section 8 (`--only=shadows`).

## Milestone 134 — streamed cells and meshes cross over (2026-11-17)

- `FellowWorldStreamer3D.crossfade` (decisions 0258, 0259): a streamed cell's content and its proxy cross over when it
  loads and unloads (the content kept as a ghost fading out, then freed), terrain regions included, and its streamed
  meshes' swaps between stand-in and full mesh too — dithered (`fellow_instance_set_dither_alpha()`), no hole halfway.
  Frontier Lab crossing over in 0.5 s: the same frame time as switching. `bench/dithered_fade_check.gd` sections 6-7;
  Frontier Lab `--frontier_crossfade`.

## Milestone 133 — hand-overs per instance dithered (2026-11-17)

- `FellowTerrainFoliage.impostor_fade` and `FellowHLOD3D.fade` for the instanced proxies (decision 0257): the hand-overs
  made per instance on the GPU — a tree to its impostor, a repeated mesh to its HLOD proxy — dithered over a band
  instead of switching. The MultiMesh culling writes each instance's fade after its data
  (`fellow_instance_set_instance_range_margins()`), the scene shader dithers it as decision 0256's fades, the side
  coming in keeping the other pixels: no hole (the check's foliage and HLOD). Frontier Lab's forests faded over 20 m:
  4.21 ms against 4.25 ms switching.

## Milestone 132 — dithered fades (2026-11-17)

- `rendering/fellow/geometry/dithered_fade` (off; decision 0256): an opaque instance fading at its visibility range
  (its own fade or its visibility parent's) is drawn in the depth pre-pass and the opaque passes with a share of its
  pixels kept by a dither moving with TAA's frames, instead of blended in the transparent pass — solid instead of
  see-through, instanced, lit as opaque; the level coming in keeps the other share, so two levels crossing over cover
  every pixel once. Two specialization bits, set only on fading instances' draws (the regression suite exact).
  `FellowHLOD3D.fade` and `bake_proxies()`'s `group_fade` fade their hand-overs instead of popping. On the RTX: 4 000
  balls all mid-fade +0.9-1.7 ms of GPU against blending, −0.7 ms of the renderer's CPU; City Lab's HLOD fading the
  same GPU time either way. `bench/dithered_fade_check.gd`; City Lab `--city_hlod_fade`, `--city_hlod_margin`.

## Milestone 131 — milestones 124-130 on the RTX; terrain proxies by error (2026-11-17)

- `bake_proxies()`'s `terrain_error` (decision 0255): a terrain's streaming proxies are simplified apart from the rest
  as far as 2 pixels of error allow where they are first drawn, not to `ratio` — 5 % of a region's triangles had left
  a fan from its locked edges over the valleys, and Frontier Lab's far groups buried a town. World Lab with ±60 m
  hills: the groups' ground 9.4 → 0.5 m off on average. The editor's dialog has the error; Frontier Lab bakes at the
  distances it streams at (version 8).
- The laptop's measurements of milestones 124-130 in their decisions (0248-0254): the hair's GPU cost and level of
  detail, its crowd on 24 threads (10.4×), World Lab's groups (347 → 107 draws, a tenth of a millisecond), the textured
  proxies' bakes, the HLOD bakes (the town's build 1.7 s → 7 ms, the forest's impostors at once).

## Milestone 130 — HLOD merged proxies baked into files (2026-11-16)

- `FellowHLOD3D.bake_merged_proxies()` (decision 0254): the merged proxies and their detail levels baked into files
  under a signature of each cell's members; `build()` takes a cell's mesh when its members are as baked, else merges
  it (a stale bake can only miss). A town of 1 600 houses builds in 9 ms instead of 2.5 s. Project > Tools > Bake HLOD
  (was Bake HLOD Impostors) bakes the merged proxies and the impostors. `bench/hlod_merged_files_check.gd`.

## Milestone 129 — HLOD impostors baked into files (2026-11-16)

- `FellowHLOD3D.bake_impostors()` and Project > Tools > Bake HLOD Impostors (decision 0253): the impostors baked ahead
  into files beside the scene, kept in the node's `impostors` (by their meshes' files) and taken by `build()` at once —
  a forest's impostors from the first frame instead of after 68 frames of baking; the picture as the run-time bake's
  (within lavapipe's frame-to-frame floor). The impostor baker asks for frames while it runs (in the editor it waited
  for a redraw between views). `bench/hlod_impostor_files_check.gd`; the editor check's HLOD step.

## Milestone 128 — one proxy for a group of far cells (2026-11-16)

- `bake_proxies()` groups the cells 4 × 4 (`group_cells`) and bakes one proxy of each group's content, drawn beyond the
  distance at which none of its cells can be loaded as its cells' proxies' visibility parent and hidden while one of
  them is loaded (decision 0252): World Lab's view 347 → 107 draws, 0.43 → 0.65 % of the picture off. A terrain's
  outer edges stay locked across a group. The editor's dialog has the group size; `stream_back.gd` checks the groups.

## Milestone 127 — textured streaming proxies (2026-11-16)

- `bake_proxies()` bakes a texture for a cell's proxy where its materials have textures (decision 0251): the proxy
  unwrapped (xatlas), each texel the colour the cell's surface under it draws (material colour, vertex colours, the
  albedo texture at the level of the texel's size), BC1 with mipmaps in the proxy's file; `texel_size` 0.5 m,
  `max_texture_size` 512. A village of atlas-textured houses from 300 m: 50 → 11 % of the picture more than 16 levels
  off, about 30 KB a cell. Proxies are now simplified by meshoptimizer without locked borders or pruning (Godot's LODs
  had deleted nearly all of the village's 144 houses), vertex-coloured ones coloured per corner. The editor's dialog
  has the texel; `bench/proxy_texture_check.gd`.

## Milestone 126 — hair on the worker threads (2026-11-16)

- Every `FellowHair3D` of a frame stepped on the worker threads at once (decision 0250): 24 heads' 5.8 ms of steps in
  3.5 ms of the main thread on two cores (1.9×); `get_global_stats()`. `bench/hair_crowd_check.gd`.

## Milestone 125 — hair on a head's mesh; fewer, wider strands far away (2026-11-16)

- `FellowHair3D.scalp_mesh` (decision 0249): roots on a head mesh's painted faces (an egg-shaped head: within 0.4 % of
  its surface, none on the face). `lod_distance`: beyond it a share of the strands by the square of the distance, each
  wider in proportion (at 8 m 2 398 strands, lavapipe's frame 773 → 259 ms, the hair looking the same);
  `simulation_distance`. `bench/hair_lod_check.gd`.

## Milestone 124 — hair of strands (2026-11-16)

- `FellowHair3D` (decision 0248): guide strands on a head's scalp simulated on the CPU (lengths exact, a pull to the
  styled shape, gravity, damping, the scene's wind, the scalp's sphere and sphere or capsule colliders), and many more
  strands drawn between them on the GPU as camera-facing ribbons at least a pixel wide with a hair shading model
  (Kajiya-Kay, shifted highlights, light from behind); motion vectors follow the strands. "Add Hair" in the
  FellowEngine menu; `bench/hair_check.gd`, the regression scene `hair`.

## Milestone 123 — the parts that fall break where they land (2026-11-15)

- `FellowDestructible3D.landing_break_speed` (decision 0247; 0 by default): a part a structural object drops breaks
  where it lands hard enough — the pieces near the contact each a body, the rest split along the bonds that hold (a
  toppled pillar in pieces where it struck, a balcony fallen 3 m broken at its edge). `bench/landing_check.gd`.

## Milestone 122 — destruction under load: bonds that carry the weight, and crack (2026-11-15)

- `FellowDestructible3D.strength` (decision 0246; 0 by default): a structural object's touching faces are elastic
  bonds; at each blow and `update_structure()` the structure's stresses under its weight (and `add_load()`'s loads) are
  solved and the bonds past the strength crack, the worst first — an overhang breaks off at its root, a span over a hole
  falls, a balcony gives way at the wall; the forces balance to 10⁻⁷ of the weight, the root stress of a cantilever
  near a beam's. A collapse goes on over frames within a deterministic work budget (a 128-piece wall: 22 frames).
- `anchor_mode` Static Bodies: the pieces touching another static body stand on it (a balcony on a building's wall).
- `bench/stress_check.gd` (ten lanes, both physics engines), the regression scene `stress`.

## Milestone 121 — any frame's GPU passes in the editor; a span of frames as a graph (2026-11-15)

- The debugger's FellowEngine Hitches tab's **Capture GPU Frame** (decision 0243): the running game's next frame's GPU
  passes, longest first and on the time line, a row per queue, with the hitch sampler on or off; the game captures its
  pass timestamps only until that frame is read back.
- **Record GPU Frames** (decision 0244): every frame of the next seconds as a graph (the time since the frame before,
  the GPU's passes), any frame's passes on the time line.
- The agent toolkit's `run --passes` (decision 0245): the GPU's time by render pass, longest first, in `report.json`.

## Milestone 120 — asynchronous compute off by default; the driver's video memory budget (2026-11-15)

- `rendering/fellow/frame/async_compute` off by default (decision 0241; it was on for NVIDIA GPUs): measured again on
  four scenes with the passes timed by queue — 1.5-3.7 % of the GPU's time in Bistro and Frontier Lab, none in Sponza,
  and about once in two to five minutes the main queue waiting 13-18 ms at its join (a missed frame at 60 Hz; Sponza 8
  in 30 minutes on, none in 15 off).
- The operating system's video memory budget and the process's usage read from the driver every frame
  (`VK_EXT_memory_budget`, decision 0240): FellowEngine's statistics, Performance monitors; `streaming/texture_budget_auto`
  (off) makes the streamed textures' budget follow it.
- The hitch sampler's GPU hitches say when the asynchronous part began into the frame and how long it took; in the
  debugger's FellowEngine Hitches tab a GPU hitch's passes are drawn on a time line, a row per queue (decision 0242).

## Milestone 119 — downloads returned whole; the GPU's hitches, scripts' frames and hitches in the editor's debugger (2026-11-15)

- `RenderingDevice.buffer_get_data_async()` and `texture_get_data_async()` return every byte with FellowEngine on
  (decision 0236): stock left a texture that is not square without its last mips, and dropped the parts of a download
  copied before the download staging ring's wait (a 144 MiB buffer came back 89 % wrong). `bench/readback_check.gd`
  checks uploads and downloads byte for byte.
- The hitch sampler's GPU side (decision 0237): the renderer's pass timestamps captured every frame; a GPU frame whose
  passes, or the gap before them, exceed the threshold is reported with the passes longest past their averages.
- The debugger's FellowEngine Hitches tab (decision 0238): a game run from the editor lists its hitches as they happen,
  named, with the exit report's text; Record starts the sampler in a running game.
- Script frames (decision 0239): in GDScript's debug builds a hitch names the script functions and lines it ran.
- Frontier Lab (the 4 km open world streamed at 45 m/s) on the RTX with the sampler: no frame step and one GPU frame
  over 16.7 ms in three 90-second flights (3.9 ms frames, p99.9 6.0 ms).
- The GPU's rare 21-23 ms frames placed (job f460): about one in five minutes of Sponza streamed, all with asynchronous
  compute on (the GPU stalled about 15 ms where its part runs); none in 15 minutes with it off.

## Milestone 118 — compressed uploads staged as they are copied; cloth edges kept apart (2026-11-15)

- `rendering/fellow/frame/transfer_tight_staging` (decision 0234, on): a block-compressed texture's upload reserves
  staging for its rows of blocks — stock reserved its rows of pixels, 4 times the data with 4x4 blocks. Sponza streamed's
  transfer workers end with 160-192 MiB of staging instead of 512; the same copies, pictures and frames.
- Decision 0233 confirmed on an idle GPU; the optional staging trim now frees on the render thread (a run with it on had
  hung at exit waiting for its free task).
- `FellowCloth3D.self_collision_edges` (decision 0235): the cloth's edges kept off its other layers' edges — a towel
  dropped across a bar of the same cloth between their particles held 3 cm above it instead of passing through.

## Milestone 117 — a hitch sampler, and the rare long frames it found; a garment's distance shaped (2026-11-14)

- `rendering/fellow/debug/hitch_sampler`, `hitch_sampler_ms` (decision 0231): a watchdog thread samples the stack of a
  frame step that runs past half the threshold (about every millisecond, with the thread's CPU time, the other threads
  in turn) and reports the steps over it, named, when the engine exits — in a release template too (Windows x86-64:
  names from a PDB or a linker map, `linkflags=/MAP`; Linux: the executable's symbol table, `FELLOW_KEEP_SYMBOLS=1`).
- Sponza streamed's rare 25-46 ms frames found: the render thread in `vkQueueSubmit()` waiting for a lock the NVIDIA
  driver held while a worker freed a 256 MiB block of the memory allocator (30-34 ms), after decision 0225's staging
  trim had emptied it. `rendering/fellow/frame/transfer_staging_trim` is now off by default (decision 0233): no such
  frame in eight flythroughs (three of 32-36 ms in eight with it on); it keeps up to 512 MiB of host memory.
- FellowEngine's statistics count the driver's device memory allocations and frees with their time (`memory_*`).
- `FellowCloth3D.skin_distance_mode` (decision 0232): a garment's distance from the skin a share a particle, growing
  along the cloth from its pins (a skirt's waist on the hips, its hem free) or painted in the vertex colours' green.
- FellowBench `--vsync=mailbox` (diagnostics); the present thread named in thread lists.

## Milestone 116 — cloth following the skin; no frame waits for a large copy (2026-11-14)

- `FellowCloth3D.follow_skin`, `skin_max_distance`, `skin` (decision 0230): a source mesh with bone weights skins each
  particle with the skeleton as Godot skins the mesh; the pinned particles follow their skinned places (a skirt's
  waistband 51.5 mm → 0 from the body's skin), the free ones within a distance of theirs; past the simulation distance
  the cloth is skinned.
- `rendering/fellow/frame/transfer_large_own_submit` (decision 0227) measured on the RTX and on by default: Sponza
  streamed no longer makes the frame wait for copies over 4 MiB (22-24 a flythrough, 54-60 with a 256 MiB budget, then
  0-1); the GPU's frame the same. New statistics: `transfer_frame_large_submits`, the render thread's worker acquisitions.
- The build without a warning (`FellowTerrain3D`'s locals no longer shadow `Node::data`).

## Milestone 115 — the frame's waits on uploads; cloth layers thin and with friction (2026-11-14)

- The frame's end counts the transfer workers it found held by another thread's upload and its time waiting
  (`transfer_submit_waits`, `transfer_submit_wait_usec`). `rendering/fellow/frame/transfer_large_own_submit` (decision
  0227, off until measured on the RTX): uploads above 4 MiB off the render thread submit their worker's earlier work and
  their own copy themselves; lavapipe's Streaming Lab: 73 → 17 ms of waits in a flythrough, the same pictures.
- `FellowCloth3D.self_collision_triangles` (decision 0228): particles kept off the other layers' triangles too, layers a
  quarter of an edge apart without passing through each other (a heap's particles through a layer 71 → 0 at 2.8 cm).
- `FellowCloth3D.self_collision_friction` (decision 0229): friction between a cloth's layers (a heap on a 25° slope held
  by its own layers).
- The regression suite's captures with Godot's separate render thread read the frame drawn last (`render_thread/rain`
  differed under load: a frame early or late).

## Milestone 114 — the upload workers' staging; FSR 3.1's accumulation stays in full precision (2026-11-14)

- Video memory as the driver counts it (every heap) split in FellowEngine's statistics and the debugger's Monitors tab:
  `FellowEngine/Video memory (device)` (the GPU's own heaps) and `Upload staging` (RenderingDevice's transfer workers'
  staging buffers, host memory); FellowBench samples them every 30 frames along a run. Sponza streamed ended with four
  workers of 128 MiB each — 512 MiB of the 2 854 MiB reported — the device's heaps 127 MiB lower than without streaming.
- The workers' staging released when uploads pause (decision 0225, `rendering/fellow/frame/transfer_staging_trim`, on;
  `transfer_staging_trim_idle_s`, 5 s; the largest buffer kept, the frees on a worker thread): the same frames, their
  times within the runs' spread. Shorter idle times made frames wait for fresh buffers (p99.9 +3-8 ms): rejected.
- Rejected after measurement: FSR 3.1's accumulation in half precision on NVIDIA GPUs (decision 0224: no faster on the
  RTX at 4K, its pictures 42-50 dB from full precision's); large uploads reusing a big worker (decision 0226).

## Milestone 113 — Sponza streamed on the RTX; FSR 3.1's passes (2026-11-14)

- Texture streaming measured in Sponza on the RTX (milestone 45 measured Streaming Lab only): with a 256 MiB budget video
  memory's peak 1 626 MiB against 2 597 without streaming, the GPU's frame the same (6.17 against 6.16 ms), the first
  frame 20-25 % sooner, the cameras' captures 36-47 dB from streaming off.
- A streamed texture's swap no longer invalidates the directional shadow caches through materials whose shadows cannot
  read their textures (decision 0222, `rendering/fellow/streaming/texture_swaps_keep_shadows`, on, exact): Sponza's
  invalidations 71 → 32 a run (the alpha-tested materials' left), the cascades built 262 → 214 times.
- The streaming budget's bias coarsens the level each texture would use, not its estimate under magnification, and the
  budget is then filled, levels first to the textures furthest from what the cameras need (decision 0223,
  `rendering/fellow/streaming/texture_budget_from_used_level`, on; no change without a budget): Sponza's 256 MiB budget
  held 255 MiB instead of 129, half the swaps, camera 03 43.2 → 46.8 dB.
- FSR 3.1's passes timed one by one in the profiler (`FSR 3.1: accumulate and sharpen`, …): at 4K (50 %) the
  accumulation is 1.40-1.45 ms of the upscaler's 2.31-2.38 on the RTX.

## Milestone 112 — cloth that tears and keeps off itself (2026-11-14)

- `FellowCloth3D.tear(position, radius)` and `tear_strain` (decision 0220, 0: off): edges break — cut, or stretched past
  that fraction of their length; their triangles go (a hole), the tethers are found again (a part cut from its pins
  falls); the signal `torn`, `reset()` makes it whole. A ball through a sheet held at its corners tears it open; a flag
  cut off its pole falls to the ground.
- `FellowCloth3D.self_collision` (decision 0221, off): no two particles closer than `self_collision_distance` (0: three
  quarters of the mean edge) but neighbours at rest — a sheet falling in a heap lies in folds (the nearest two 8.5 cm
  apart, against 0.65 cm without); +22 % of the cloth's worker time for flags in the wind.
- FellowEngine's stats split GodotPhysics3D's broadphase update (the tree's optimisation, the culls, the pairs' loop) and
  count the pairs made and freed.

## Milestone 111 — FSR 3.1 at 1440p and 4K; each source's own room; the broadphase's pairs (2026-11-14)

- FSR 3.1 measured at 2560 × 1440 and 3840 × 2160 on the RTX (FellowBench's new `--render_size=WxH`): at 50 % the
  frame 44-48 % shorter than TAA's at native resolution (Bistro 4K 13.50 → 7.24 ms, Sponza 18.43 → 9.57 ms), at 33 %
  55-61 %; the pass 0.92-0.98 ms at 1440p and 2.31-2.36 ms at 4K at 50 %; half precision gains nothing on NVIDIA.
- `FellowAcoustics3D.source_rooms` (decision 0218, 0 by default; "Add Acoustics" sets 2): a source in another room
  than the listener's is sent to a reverb of its own room's (a hall heard from the open keeps its 1.35 s decay, a
  closet its 0.29 s); +15 µs a frame for 64 sources on the laptop.
- GodotPhysics3D's broadphase drops on the worker threads the culls' hits that are pairs already (decision 0219,
  `rendering/fellow/physics/pair_prefilter`, on): the pile −9.6 % at 2 000 bodies, −13.3 % at 5 000, −14.0 % at 10 000,
  the same hashes as stock's; FellowEngine's GodotPhysics3D 3.3 × stock's at 5 000 and 10 000 bodies.
- "Add Car" gives the car two doors on hinges (opening outwards, 70° at most), detachable with the bumper.
- Thirty minutes of the slice on the RTX: 239 fps, 0.21 hitches a minute, none compiling, nothing growing.

## Milestone 110 — rigid bodies by the thousand (a measurement, 2026-11-13)

- Physics Lab's pile at 2 000, 5 000 and 10 000 bodies on the laptop, stock against FellowEngine (the same results):
  GodotPhysics3D 26.5 → 12.2, 106.8 → 40.0, 395 → 144 ms a tick (× 2.2-2.75); Jolt the same as stock (3.1, 10.3-10.5,
  24.3-24.4 ms). At Godot's default Jolt limits 10 000 bodies in contact overflow its buffers (contacts ignored, the
  runs no longer repeatable, in stock too): `physics/jolt_physics_3d/limits/*` raised, they repeat.
- The regression suite's scene `vehicle_damage`: a car dented, a wheel bent, its bumper hanging, its mirror off.
- `FellowVehicleDamage3D.engine_smoke` (on): with `engine_power_loss` set, a hurt engine smokes — dark smoke from
  above `engine_position`, as much as the dent there is deep (from a tenth of `max_depth`); `is_engine_smoking()`.
- `FellowVehicleDamage3D.suspension_damage` (m, 0: off): a dent reaching a wheel shortens its suspension's travel (a
  bent strut): 0.06 m at a side hit by the front-left wheel took 3.0 cm, that corner 2.3 cm lower, the diagonal one
  0.8 cm higher; `get_wheel_sag()`; `repair()` gives it back.
- Hinged parts: a detachable part with the metadata `fellow_hinge_axis` hangs on a `HingeJoint3D` along it, limited by
  `fellow_hinge_limits` (a door hit at its rear end hangs from its front edge and swings open in a right turn at
  25 km/h: 95° without a limit, 74° with (0, 70)).
- The FellowEngine menu's "Add Car" makes a car that takes damage: its body subdivided for dents, a bumper, and a
  `FellowVehicleDamage3D` (`wheel_bend` 4°, `engine_power_loss` 0.4, the bumper hanging at 6 cm and off at 16 cm).

## Milestone 109 — parts that hang before they come off (2026-11-13)

- `FellowVehicleDamage3D.hang_depth` (decision 0217): a part dented that deep hangs from the vehicle on a `PinJoint3D` at
  its end farthest from the hit (a bumper hit at its right end swings down from its left end), then comes off at
  `detach_depth`; `part_hanging(part, body)`, `is_part_hanging()`; 0 by default.

## Milestone 108 — parts that come off (2026-11-13)

- `FellowVehicleDamage3D.detachable_paths` (decision 0216): a part dented `detach_depth` deep (0.12 m, added up over
  impacts at its point nearest each) falls away as a `RigidBody3D` of `detach_mass` (8 kg) that does not collide with
  the vehicle; the signal `part_detached(part, body)`, `is_part_detached()`; `repair()` puts it back. A bumper kept
  after a light hit, off in a 30 km/h crash, lying still by the wall.

## Milestone 107 — dents that reach the running gear (2026-11-13)

- `FellowVehicleDamage3D` (decision 0215): `wheel_bend` — a dent reaching a wheel bends its toe the way the push turns
  it (a side hit ahead of the front-left wheel: +2.5°, the car pulls 24.5° right in 6 s at 40 km/h); `engine_power_loss`
  at `engine_position` — a front-end hit takes the engine's torque (74 % left: 0-100 km/h 6.27 → 7.63 s); both 0 by
  default, `repair()` puts them back; `get_engine_power()`, `get_wheel_toe_change()`.
- `FellowWheel3D.toe`: the wheel's alignment (degrees, + in).

## Milestone 106 — Windows: a refused connection seen at once (2026-11-13)

- A stock bug fixed in `drivers/windows/net_socket_winsock.cpp` (decision 0214): Godot polled a connection in progress
  by calling `connect()` again, which starts another attempt after a refusal, so a refused TCP connection was reported
  only at the 30 s timeout; the socket's error is now checked first. `StreamPeerTCP` to a closed port: 30 s → 2.05 s;
  crash reports to a collector that is down, or to `localhost` whose IPv6 address refuses: 30 s → 2.05-2.08 s.

## Milestone 105 — vehicles dented where they hit (2026-11-13)

- `FellowVehicleDamage3D` (decision 0213): a child of a `FellowVehicle3D` reading its body's contacts; an impact past
  `impulse_threshold` (2 500 N·s in a step) pushes the vertices of the vehicle's meshes (not the wheels') near it into
  the body, `softness` deep for the excess, falling off over `radius` (0.7 m), at most `max_depth` (0.3 m); the meshes
  copied at the first impact, their normals recomputed; `repair()`, `apply_impact()`, `get_damage()`, `impacted`. A wall
  at 30 km/h: the front 9.3 cm, the rear untouched; at 5 km/h and over ten bumps nothing.

## Milestone 104 — the head moving with what the face says (2026-11-13)

- `FellowHeadMotion3D` (decision 0212): a skeleton modifier following a `FellowFaceAnimator` — a nod at each of the
  voice's stressed syllables (6°, its lowest point 0.12 s after it, a critically damped pulse), a sway of the three
  axes (0.5° at rest, up to 2° more while the face speaks), shared between the neck and the head, over whatever the
  animation plays. `FellowFaceAnimator.get_emphasis_count()`, `get_last_emphasis_strength()`, `get_speech_activity()`.
  Three lanes (six nods at six sounds, the bones as the angles say; the sway at rest and speaking; untouched at 0); a
  head 0.38 µs a frame.

## Milestone 103 — far from the origin: double precision (2026-11-13)

- FellowEngine builds and runs with `precision=double` (decision 0211): GCC and MSVC; 103.68 km out the terrain and
  lights identical to the near picture, the fire within one level, characters and cars within 0.1-0.2 % of the pixels
  (single precision: 6.6-7.2 %). Fixed: the camera's position kept negated for shaders in double builds (stock: the
  water vanished, the snow and the rain's wetness wrong even near the origin); the terrain's collision heights in
  double builds; the gas volumes, clouds, atmosphere and rainbow traced relative to the camera (single precision too);
  the clouds' camera kept on top of the planet (it rose 1.7 km at 100 km). `regression.py far` checks it. Cost: Frontier
  Lab +0.8 %, City Lab +3.8 %, Crowd Lab +13 % (the CPU). Six regression goldens within 1-4 levels or changed by the
  cape scene's script.

## Milestone 102 — crash reports sent to a collector (2026-11-13)

- `FellowCrashReporter.upload_reports()`, `debug/fellow/crash_reports/upload_url`, `upload_automatically`,
  `upload_minidumps` (decision 0210): the reports waiting on the machine sent on a thread, oldest first, as
  `multipart/form-data` with the minidump in `upload_file_minidump` (Sentry's, BugSplat's and Backtrace's form) and
  the kind, reason, project, version and commit as fields; accepted ones moved into `sent/`, refused or unanswered ones
  kept; `report_uploaded`, `uploads_finished`; HTTPS with the certificate checked. Six lanes against a collector on the
  machine (refused, unreachable, accepted, TLS trusted and not, on their own).
- A stock GDScript bug fixed (marked in `modules/gdscript/gdscript.cpp`): a lambda connected to a module singleton's
  signal (`NavigationServer3D`'s too) corrupted the heap at exit; `GDScript::~GDScript` now lets go of the lambdas
  that outlive it.

## Milestone 101 — wheels that meet the ground with their shape (2026-11-13)

- `FellowWheel3D.contact_shape` and `width` (decision 0209): a wheel's ground found by sweeping a sphere or a cylinder
  of the tyre down its suspension instead of a ray — a curb met by the tyre's front and climbed over several steps
  (0.12 → 0.05 m a step, the body's jolt 0.70 → 0.47-0.53 g), a trench narrower than the tyre bridged (0.085 →
  0.015-0.021 m). Rays by default; a car's step 9 → 32 µs (GodotPhysics3D), 11.5 → 87 µs (Jolt) with cylinders.

## Milestone 100 — faces with expressions (2026-11-13)

- `FellowFaceAnimator.set_expression()`, `clear_expressions()`, `expressions`, `speech_brows` (decision 0208):
  expressions faded in and out and layered — happy, sad, angry, surprised, afraid, disgusted built in after FACS's
  action units, and the game's own — on seventeen new channels (the brows, the mouth's corners, the cheeks, the eyes'
  squint and widening, the nose; ARKit's shapes by default); the brows raised on the voice's stressed syllables, the
  signal `emphasized` for the game's nods and gestures. Two lanes; a face's frame 0.37-0.43 µs.

## Milestone 99 — sound through openings (2026-11-13)

- `FellowAcousticPortal3D` and `FellowAcoustics3D.portals` (decision 0207): a source behind a wall is heard through
  the nearest open doorway or window between it and the listener when that path is clear and louder — from the
  opening's direction at the path's length (`AudioStreamPlayer3D`'s hook takes a position), its level and highs
  lowered by the bend there (`portal_diffraction_db`) and the portal's `transmission`; a door in the doorway closes it.
  Six lanes on both physics engines, under Movie Maker 14 dB louder and brighter than through the wall and from the
  doorway's side; the gizmo, handles and a ready-made doorway in the editor.

## Milestone 98 — hang reports (2026-11-13)

- `debug/fellow/crash_reports/hang_seconds` (decision 0206, 10 s with the crash reports on): a watchdog writes a
  `hang_*.json` report when the main thread makes no progress — its frames (on Linux and macOS its GDScript frames,
  taken by the main thread in a signal; on Windows from its suspended context, and a minidump) — without stopping the
  game, and how long the stall lasted when it goes on. Not with a debugger attached or while the machine sleeps.
- Milestone 97 on Windows (job f397): every crash's report and minidump as expected but a stack overflow's; fixed (a
  64 KiB stack guarantee for the filter, its frame kept small) and a worker thread's unwinding (jobs f398-f399: every
  crash and hang lane passes on Windows).

## Milestone 97 — crash reports (2026-11-13)

- `FellowCrashReporter` and `debug/fellow/crash_reports/*` (decision 0205, a new module `fellow_diagnostics`): a crash
  — in a release template too, where Godot's own handler is compiled out — writes a JSON report (the reason, the
  faulting thread's frames as module and offset, the last lines of the log, the game's breadcrumbs and values, the
  machine, GPU and scene, GDScript's backtrace) before the process ends, written without the heap; a minidump beside it
  on Windows; the next run finds it pending. `tools/fellow_crash/`: the check (six kinds of crash and a late one, on
  the editor build and the release template), a symbolizer. Off by default.
- `FellowAudioDirector`: a virtual voice's tests in the order of their cost (decision 0204's follow-up): 4 400 virtual
  voices 204-224 → 57-73 µs a frame.
- Milestones 77-96 on the laptop (job f396): every check passed on the RTX with both physics engines.

## Milestone 96 — voice limits by bus (2026-11-12)

- `FellowAudioDirector.bus_voice_limits` (decision 0204): a bus's own budget of voices, handled as the director's
  before it (the bus's least important, quietest giving way, or the new sound virtual or refused). One lane under
  Movie Maker, the earlier thirteen unchanged.

## Milestone 95 — a jump back to the wall behind (2026-11-12)

- `FellowTraversal3D.hang_jump()` with the wish away from the wall (decision 0203): a ledge on the wall behind is
  jumped to, turning round (`TRANSFER_JUMP_BACK`). One lane on both physics engines, the earlier fourteen unchanged.

## Milestone 94 — eyes that converge (2026-11-12)

- `FellowFaceAnimator.eye_spacing` (decision 0202): each eye turns in by half the angle a target subtends between
  them — the eyes converge on a near target (0.6 m: 15.8° each with the test face's 0.34 m). 0 by default (parallel).

## Milestone 93 — diagonal jumps (2026-11-12)

- `FellowTraversal3D.hang_jump()` with the wish along and up or down (decision 0201): the nearest edge past this one's
  end at another height is jumped to and caught (`TRANSFER_JUMP_DIAGONAL`). One lane on both physics engines (across
  a gap and up), the earlier thirteen unchanged.

## Milestone 92 — the highs blocked apart (2026-11-12)

- `FellowAcoustics3D` (decision 0200): a collider's `fellow_occlusion_highs` blocks a path's highs (the shelf and the
  cutoff) apart from its level — a curtain dulls a sound without silencing it. A curtain lane on both physics engines
  and in the mix; the earlier lanes unchanged.

## Milestone 91 — occlusion through several things (2026-11-12)

- `FellowAcoustics3D` (decision 0199): a path through glass goes on to what lies behind it — up to three colliders
  add their `fellow_occlusion` (at most a full block); a plain wall ends the path at once as before. Two panes 0.6, a
  pane and a wall 1.0, on both physics engines; the earlier lanes and the listened mix unchanged.

## Milestone 90 — occlusion by material (2026-11-12)

- `FellowAcoustics3D` (decision 0198): a collider's metadata `fellow_occlusion` (0 to 1) is how much it blocks a path —
  glass, curtains and thin walls muffle less than a wall. A glass lane on both physics engines and in the mix under
  Movie Maker; decision 0186's lanes unchanged.

## Milestone 89 — eyes that look (2026-11-12)

- `FellowFaceAnimator` (decision 0197): the eyes look at `look_target_path` through eight gaze channels on ARKit's
  `eyeLook*` shapes (clamped to their range, eased), with saccades at random intervals. A target in five directions
  read as expected, decision 0191's lanes unchanged; the regression scene `face` re-blessed (pupils, a gaze).

## Milestone 88 — vehicles on loose ground, marks that fade (2026-11-12)

- `FellowVehicleEffects3D` (decision 0196): on colliders in `loose_ground_group`, dust from the rolling wheels (more
  with the speed and the sliding; its colour the ground's metadata) and ruts as they roll, no tyre smoke there;
  `mark_lifetime` fades skid marks out. Two lanes on both physics engines, decision 0187's five unchanged.

## Milestone 87 — low-passes in mix snapshots (2026-11-12)

- `FellowMixSnapshot.bus_lowpass` (decision 0195): a snapshot muffles buses — the director's own low-pass at the end
  of each, its cutoff faded from 20.5 kHz in octaves with the snapshot's weight, turned off when no snapshot filters
  the bus. Measured on the master bus under Movie Maker (a 4 kHz tone −36.6 dB under a 500 Hz cutoff and back).

## Milestone 86 — down to a ledge below (2026-11-12)

- `FellowTraversal3D.hang_jump()` with the wish down (decision 0194): a ledge within `hang_jump_depth` (2.6 m) under
  the edge — both hands on it, room for the body — is dropped to and caught, the body going out to its face first (a
  ledge standing out of the wall passed); none found, the wish down lets go as before. One lane on both physics
  engines, the earlier twelve unchanged.

## Milestone 85 — virtual voices (2026-11-12)

- `FellowSoundEvent.virtual_voice` and `steal_oldest` (decision 0193): a sound with no voice for it, giving its voice
  up or out of its reach goes on unheard as a virtual voice and is resumed where it would be when within reach and a
  voice is free (a one-shot that ends meanwhile is let go); an event's oldest instance can give way to a new one. The
  director keeps its count of voices as they change (4 400 virtual voices 62 µs a frame). Four lanes under Movie
  Maker, decision 0190's eight unchanged.

## Milestone 84 — over an edge into a hang (2026-11-12)

- `FellowTraversal3D` (decision 0192): `try_drop()` and `drop_to_hang` (off by default) — walking slowly over the edge
  of the floor, the body goes out over it and down into a hang from it, facing back (both hands on the edge, room
  below); the wish that walked it there does nothing until it changes. Two lanes on both physics engines (into the
  hang, along it and down; running over without one), the earlier ten unchanged.

## Milestone 83 — faces that speak and blink (2026-11-12)

- `FellowFaceAnimator` (decision 0191): the mouth's blend shapes (ARKit's names by default) from the voice a player
  plays — analysed as it plays (five bands every 10 ms, shared by every face playing it) into the jaw open, the mouth
  wide, round or closed and the lower lip under the teeth, read 40 ms ahead of what is heard and eased — or from a
  timeline (`voice_stream` at `voice_time`); blinks at random intervals. Synthesised vowels read as expected at two
  pitches, the face following a played voice under Movie Maker, a regression scene (`face`); a face 0.24 µs a frame.

## Milestone 82 — sound events, voices and mix snapshots (2026-11-12)

- `FellowAudioDirector`, `FellowSoundEvent`, `FellowMixSnapshot` (decision 0190): sounds played as events — a
  variation picked (at random without repeats, at random, in turn), the level and pitch varied, instance limits,
  cooldowns, sounds beyond their distance not started — from a pool of players within a budget of voices, the least
  important and quietest at the listener giving its voice to a more audible new one (faded out); voices that follow
  nodes; mix snapshots fading buses' levels in and out, pushed by the game or while a voice plays on their trigger bus
  (ducking). Eight lanes under Movie Maker (the duck measured on the master bus), the release template the same; 64
  voices 12-16 µs a frame.

## Milestone 81 — round corners and across gaps (2026-11-12)

- `FellowTraversal3D` (decision 0189): a hanging body goes round the corner where its ledge ends (`wrap_corners`, on)
  — an outside corner onto the wall's side, swinging wide of it, or an inside one onto a wall meeting the ledge — when
  both hands fit on the new edge and the body has room; the wish held round the corner goes on along the new edge.
  `hang_jump()`: across a gap to the next ledge along the edge (`hang_jump_distance` 1.6 m) or up to a ledge over it
  (`hang_jump_height` 1.3 m), the hands letting go and catching the new edge. `is_transferring()`, the signal
  `hang_transferred`. Four lanes on both physics engines (round a block, round an inside corner, across a gap, up to a
  beam), decision 0183's six unchanged; a regression scene (`ledge_corners`).

## Milestone 80 — destruction that holds (2026-11-12)

- `FellowDestructible3D.structural` (decision 0188), with `break_radius` and `anchor_height`: a static object stands in
  its pieces after its first break; each blow knocks out the pieces near it (thrown out, passing through their
  neighbours while they leave), and each part that no longer reaches the ground through the pieces that touch (found
  from their shared surface points when the fracture is prepared) falls as one rigid body; its own debris does not
  strike it. `get_standing_pieces()`, `get_structure()`. Four lanes on both physics engines (a hole punched in a wall,
  a pillar falling whole, a wall cut across, the whole break unchanged), a regression scene (`structure`), "Add
  Destructible Wall".

## Milestone 79 — vehicles' marks, smoke and sounds (2026-11-12)

- `FellowVehicleEffects3D` (decision 0187), a child of a `FellowVehicle3D`: skid marks where a tyre slides past its
  grip (a ribbon a wheel in one mesh in the world, each segment written into its buffers in place, the oldest reused),
  tyre smoke (GPU particles at the sliding wheels), and its sound synthesised in a 3D player from its state — the
  engine's firing frequency and harmonics with the revolutions and throttle, the tyres' squeal, the road's rumble —
  silent beyond `sound_distance`. Five lanes on both physics engines (no marks cruising or with the anti-lock brakes;
  marks and smoke in a handbrake turn, a burnout and with locked wheels), the sound measured under Movie Maker (the
  tone at the firing frequency at idle and at 70 km/h, the squeal in a slide); "Add Car" includes it. The wheel's
  `get_slip_speed()`, `get_contact_normal()`.

## Milestone 78 — sound through the scene's geometry (2026-11-12)

- `FellowAcoustics3D` (decision 0186): every `AudioStreamPlayer3D` heard through the physics world — rays from the
  listener to each playing source and to points round it give the share of its paths blocked (walls, past an edge,
  not the bodies it is in), which lowers its direct sound and cuts its highs (eased); rays round the listener measure
  its room (enclosure, the walls' distance, the nearest) for a reverb (Sabine's decay, the nearest wall's predelay) on
  a bus it makes, every source sent to it. Through a small hook in `AudioStreamPlayer3D` (its defaults stock's). Eight
  lanes on both physics engines and the mix itself measured (a wall −27 dB on noise and its highs cut; a room's
  reverb tail); 256 sources 0.18-0.25 ms a frame; the monitor "FellowEngine/Acoustics time", "Add Acoustics".

## Milestone 77 — vehicles (2026-11-12)

- `FellowVehicle3D` and `FellowWheel3D` (decision 0185): cars and trucks on GodotPhysics3D and Jolt — a ray per wheel,
  springs, dampers, anti-roll bars and bump stops; tyres with a combined-slip curve times the ground's friction; the
  wheels' spin solved implicitly (stable at walking pace); a hold at a standstill (slopes, turntables, moving
  platforms) and moving or rigid ground; an engine with its torque curve, limiter, braking and inertia; an automatic
  gearbox (earlier shifts at part throttle, kickdown, reverse from the brake), open or locked differentials, traction
  control, anti-lock brakes, eased steering, drag and downforce. Fifteen lanes (0-100 km/h 6.3 s, 100-0 in 40 m,
  0.96 g, a jump, a turntable, a slab pushed back, slopes, ice, coasting), many cars (64 in 0.65-0.80 ms a frame), a
  regression scene (`vehicle`); the FellowEngine menu's "Add Car", the wheel's gizmo.

## Milestone 76 — cloth on bones (2026-11-12)

- `FellowCloth3D` (decision 0184): `skeleton_path` and `pin_bones` — the pinned particles follow a skeleton's bones
  (each bound to the nearest allowed bone when built or reset, kept in its space, interpolated over the substeps): a
  cape on a character's chest turns and bends with it; capsules on the bones (`BoneAttachment3D`s) in `colliders` keep
  the cloth out of the body. Checked on a cape on a twisting chest of a circling body (the pins within 0.02 mm, nothing
  in the body), a regression scene (`cape`).
- Destruction (decision 0182): fractures are low-priority tasks on the worker pool (high-priority ones delayed the
  engine's own work: 19-86 ms frames while 64-256 crates broke on the laptop; 8-15 ms as low-priority ones, job f395).

## Milestone 75 — hanging and ladders (2026-11-12)

- `FellowTraversal3D` (decision 0183): in the air, a ledge the body cannot climb (no room on its top) — or with
  `hang_first` any it could — is hung from when its edge is about `hang_reach` (2.05 m) over the feet (`hang`,
  `try_hang()`). Hanging, the body moves along the edge as the intent asks (`shimmy_speed`), the ledge found again a
  step along: round walls are followed, its end, a gap or something in the way stops it; pushing at the wall again,
  up or `climb_up()` climbs (a high mantle from the hang), pulling away, down or `release()` lets go. The pose: hands on
  the edge stepping along it, feet on the wall.
- `FellowLadder3D`: a ladder marker (`height`, `width`, `rung_spacing`, `rung_offset`, `exit_at_top`; the editor draws
  its rails, rungs and climbing side). A traversal with `ladders` takes one the body walks at, or walks off its top
  over it; climbs at `ladder_speed` as asked (`set_climb_input()`, or the intent), steps off at its foot, mantles off at
  its top. The pose: hands and feet on the rungs. Signals `hang_started`, `hang_finished`, `ladder_started`,
  `ladder_finished`; the FellowEngine menu's "Add Ladder".
- Checked on six lanes with GodotPhysics3D and Jolt (`bench/climb_check.gd`: a hang moved to a ledge's end and let go,
  a climb from a hang, a round tower's rim followed 120°, a gap stopped at, up and down a ladder; no frame of the capsule
  in the world), milestone 72's course unchanged, a regression scene, the editor.
- Destruction (decision 0182): a break's meshes and shapes are built within the frame's budget of pieces; the bench's
  `--many=N` waits until every crate has broken.

## Milestone 74 — destruction (2026-11-12)

- `FellowDestructible3D` (decision 0182): a child of a body (static, rigid or animatable) that breaks its mesh into
  rigid pieces — on `break_apart()`, when `damage()` takes `health` to zero, or on a blow of `break_impulse` N·s (a
  rigid body's change of velocity in a step less gravity's, times its mass; a rigid body striking a static one). The
  fracture runs on a worker thread: `piece_count` Voronoi sites inside the solid (`impact_focus` of them around the
  blow; a thin object cut across), each cell's share of the surface clipped out and its faces capped with the mesh's
  own cross-sections (holes and all), so every piece is closed and the volumes add up to the source's. Pieces: the
  mesh's materials outside, `interior_material` on the cuts (planar UVs), mass from volume and `density`, convex hulls
  (a piece far from convex: its parts'), the body's velocity plus a burst and the blow's impulse. `prefracture`
  (computed at ready, shared by the same mesh, seed and count: breaks at once); budgets of pieces spawned a frame (128)
  and of debris kept (256, the oldest shrinking away; `debris_lifetime`); the monitor "FellowEngine/Debris pieces"; the
  FellowEngine menu's "Add Destructible Crate".
- Checked on thirteen lanes with GodotPhysics3D and Jolt (`bench/destruction_check.gd`): a crate, a pillar knocked
  apart by a 20 kg ball (and one a 2 kg ball leaves standing), a crate shattering when dropped (and a 600 kg one under
  a 900 kg one that stays), a pane of glass, a wall with a window, a ring, a 4 000-triangle sphere in 32 pieces — every
  piece closed, the volumes exact; a regression scene; the editor.

## Milestone 73 — cloth (2026-11-12)

- `FellowCloth3D` (decision 0181): flags, banners, curtains, awnings and capes — a generated rectangle or a source
  mesh, pinned by an edge, corners or vertex colours — simulated on the CPU by position-based dynamics in fixed
  substeps (480 Hz, two stretch passes, bending, tethers to the pins), blown by the active `FellowWind3D` (drag along
  each triangle's normal, lift across it, turbulence), kept out of spheres, capsules, boxes, cylinders, planes and
  height maps (`colliders` followed as they move, the physics world's bodies under the cloth), with friction. Every
  cloth of a frame on the worker threads at once; drawn with motion vectors (a blend shape gives each instance vertex
  buffers swapped every frame). `lod_distance` (half the substeps), `simulation_distance` (carried, not simulated),
  `apply_impulse()`, `set_particle_pinned()`, the monitor "FellowEngine/Cloth time"; the FellowEngine menu's "Add
  Flag" and "Add Curtain".
- Checked on ten lanes (`bench/cloth_check.gd`: a curtain hangs 2.40 m below its rail, sheets rest on a sphere, a box
  and a height map with nothing inside them, a cape stays out of the capsule it is pinned on), a regression scene, the
  editor. On the laptop a 400-particle flag costs 0.4 ms of a thread; 64 flags in the wind 1.4-1.6 ms of the frame,
  256 5.6-6.0 ms (`--many=N`).

## Milestone 72 — mantling and vaulting (2026-11-11)

- `FellowTraversal3D` (decision 0180): a character's body finds ledges in its way with the physics server's queries —
  a wall in reach facing the way it goes, a top within `min_height`-`max_height` (0.5-2.3 m; 0.5 m more in the air),
  room for its capsule on the top, above the body and over the edge — and climbs them: a low mantle (to 1.3 m), a high
  one pulled up from a hang, or a vault over a thin obstacle. The body moves along a path built from that room (the
  capsule never enters the world); moving platforms carry the ledge; ledges are grabbed in the air; a ceiling bounds the
  search and a beam the body could walk under is left to `try_mantle()`. Clips per kind (`low_clip`, `high_clip`,
  `vault_clip`) set the mantle's time and, with `clip_root_bone`, their root motion warped onto the ledge is the path.
  `find_ledge()`, `set_intent()`, `get_pose_targets()`, signals for an `AnimationTree`, `get_global_stats()` and the
  monitor "FellowEngine/Traversal time", `check_interval`, `debug_draw`.
- `FellowTraversalPose3D`: the pose over the character's animation — the torso leaned in, the hips lowered until the
  arms reach the edge, the hands and feet placed by two-bone IK (bones found by name in Godot's humanoid, Mixamo,
  Unreal and Blender rigs) — or the traversal's clip played with its root's motion out.
- Checked on nineteen obstacles with GodotPhysics3D and Jolt (`bench/traversal_check.gd`), a regression scene, the
  editor (a gizmo, icons, "Add Mantling Character"). On the laptop: 200 characters mantling and vaulting all the way
  cost 0.32 ms of the CPU a frame with Jolt, 0.49 ms with GodotPhysics3D (`--crowd=N`).

## Milestone 71 — the terrain's virtual texture on the RTX (2026-11-11)

- Measured in Frontier Lab on the RTX 5070 Ti Laptop GPU (decision 0179, jobs f386-f389): with every terrain region
  drawn from its pages the GPU's frame is 5-10 % shorter at the still cameras (1080p) and 5 % in the 4K flight (92 →
  98 fps, 1 % low 62 → 67); the bakes cost the GPU 0.015 ms a frame. Where the frame is the CPU's (1080p, 230 fps) the
  pages first cost more than they saved (the flight's frames over 7 ms 2-10 → 45-75); now about even.
- The bakes reuse each terrain's uniform sets, overview views and a buffer updated in place (the render thread's time
  18 → 3.6 µs a page); a terrain keeps its page choice while its view barely changes, far ones revisit less often (the
  main thread's choice 0.32 → 0.14 ms a frame in the fast flight, 0.27 → 0.08 ms in play); regions enter with their
  pages set up, without a second material update.
- `rendering/fellow/terrain/virtual_texture_all`: every terrain drawn from its pages. Frontier Lab: `--frontier_vt=1`.
  Counters `terrain_vt_update_usec`, `terrain_vt_setup_usec`; GPU timestamps around the bakes;
  `get_virtual_texture_stats()` gains `updates`, `visits`, and the atlas's count is `atlas_wanted`.

## Milestone 70 — terrain virtual texturing (2026-11-11)

- `FellowTerrain3D.virtual_texture` (off; decision 0178): the terrain's material baked once into pages of a virtual
  texture — 128 × 128 texels, `virtual_texture_texels_per_meter` (64) at the finest mip — by a compute kernel with the
  layers' own arithmetic, into an atlas every terrain shares (`rendering/fellow/terrain/virtual_texture_atlas_size`,
  4080: 900 slots), found through a page table per terrain; the coarsest mip is the terrain's overview, with its own
  mips, read far away and until a page is baked. The pages a camera wants are chosen on the CPU each frame and the
  missing ones baked within `rendering/fellow/terrain/virtual_texture_pages_per_frame` (16), coarsest and nearest
  first; the least recently wanted give their slots up, and a nearly full atlas makes every terrain's pages coarser.
  Edits and layer changes bake the pages over them again; regions have their own pages; `debug_view` Virtual Texture.
  Lavapipe (no GPU run yet): five views of a 513 m terrain within 41.0-43.6 dB of the layers' pictures, a frame in
  software 19-36 % shorter (the fragments' work: a proxy, not a GPU's time); a flight of 301 frames with one frame
  missing pages.
- Fixed: the terrain layers' colours were converted from sRGB twice since milestone 67 (a `Color` in a `vec4` uniform
  is converted by the renderer): layers drew darker than their colour. They reach the shader as vectors now; the
  regression scene, the editor's terrain preset and the showcase's builder have their colours back as before
  milestone 67 (the same look), Slice Lab and Frontier Lab draw their linear colours as written (brighter than in
  milestones 67-69, as their proxies).

## Milestone 69 — the rare frames attributed, an hour of play (2026-11-11)

- Counted per frame (decision 0177): the RenderingServer's queue where it runs (`rs_queue_flush_usec`), the main
  loop's sync, the draw by phase (`draw_*_usec`), the RenderingDevice's pending frees and uploads, mesh surfaces and
  MultiMesh buffers; `FellowWorldStreamer3D.get_stats()`: its update by step. An hour of Frontier Lab with them: of
  fourteen frames over 15 ms, eight were terrain regions entering.
- Terrain regions enter cheaply: a region's neighbours found once per border refresh (not once per border texel), and
  a neighbour refreshes only the border facing it — an entry on the RTX p50 2.6 → 0.78 ms, p99 4.4 → 1.5 ms; the
  flight at 1080p p99 6.5 → 5.3 ms, 1 % low 135 → 175 fps. An hour of play: 254 fps, p99.9 10.2 → 6.5 ms, frames
  over 15 ms 0.24 → 0.15 a minute, none from a region (the rest the GPU, its driver, or a lost scheduling quantum).
- Fixed: terrain regions instanced on pool threads waited there for their normals' group task (a hang with every pool
  thread waiting); the rows are built inline on pool threads.

## Milestone 68 — what Frontier Lab found (2026-11-10)

- `FellowGasVolume3D.sleep_distance` (decision 0176, 0: never): beyond it from the camera a gas volume takes no steps —
  its smoke and fire kept and drawn — and its burning objects wait with it. Frontier Lab's campfire sleeps past 250 m:
  the gas simulation over the flight 0.93 → 0.18 ms of GPU; the flight at 1080p 5.06 → 3.9-4.3 ms (198 → 234-259 fps
  over seven flights, with the laptop's power state).
- `rendering/fellow/frame/lazy_shader_variants_fill` (on): the variants a shader-cache load leaves for later (decision
  0061) made one by one on a worker thread after it, outside the version's lock; the snow tracks' kernel made when the
  rain node is set up. Twenty minutes of play: no shader variant or kernel made in play (10 variants and a kernel
  before), the largest frame 92 → 21.5 ms, frames over 15 ms 9 → 5 (none in the first minute).
- Fixed: "Attempting to use an uninitialized RID" (MultiMesh), printed in some flights: MultiMesh culling's pool looked
  up members freed since (the result right, the error false); in three of seven flights before, none of nine after.
- `FELLOW_LOG_COMPILES=1` prints each shader variant made at its first use and each kernel compiled from GLSL, with
  its frame.

## Milestone 67 — Frontier Lab: an open world at AAA scale on the RTX (2026-11-10)

- Frontier Lab (`--scene=frontier`, decision 0175): a 4 km world generated once into `external/frontier_<size>/` —
  terrain at 1 m in 256 streamed regions with forests (impostors) and meadows, 1 024 streamed cells of towns
  (buildings, 145 142 windows of a modular kit as instances, 2 123 street lights, props), villages and the wild (17 354
  rocks), proxies for what is not loaded, the slice's fire, crowd, lake and weather in the first town. About 64 M
  triangles and 13 000 nodes; generated in 81 s on the laptop (291 MB).
- On the RTX: its 154 s flight at 198 fps at 1080p (GPU 3.70 ms), 162 at 1440p, 186 at 1440p with FSR 3.1 at 67 %, 94
  with the 3D frame at 3840 × 2160; 761-1 354 MB of video memory. Twenty minutes of play: 248 fps, p99 6.3 ms, 0.4
  frames over 15 ms a minute, nothing growing.
- Fixed: `FellowTerrainLayer.albedo_color` is sRGB in the shader and the proxies, as documented (layers drew brighter
  than their colour; the editor's preset, the showcase and the regression scene keep their look); baked proxies, their
  material and foliage impostors are referenced by their files in saved scenes (they were copied in: a level file
  5.7 MB → 32 KB); a bake's texture averages keyed by file (a region's bake 2.3 s → 14 ms on lavapipe).
- Found for milestone 68: the gas simulation's 0.93 ms of GPU at any distance, shaders and a kernel made in play, main
  thread and GPU spikes.

## Milestone 66 — proxies for unloaded cells, streamed meshes (2026-11-10)

- `FellowWorldStreamer3D.bake_proxies(directory, options)` (decision 0174): each cell's meshes, MultiMesh instances (up
  to 256 a node) and terrain regions (`FellowTerrain3D.make_proxy_arrays()`) merged, simplified to 5 % with
  meshoptimizer and coloured per vertex by their materials' mean albedo, saved and added as the cell's `FellowProxy` —
  shown while the cell is unloaded, hidden in the editor while the cell's scene is shown. Lavapipe, 12 × 12 cells of
  64 m streamed to 160 m: 7.8 % of the picture off the whole world's (the horizon empty) → 2.0 %, for 2 MB of meshes
  and 63 k triangles; 1.7 M triangles baked into 58 k in 4.7 s.
- Streamed meshes: with the bake's `stream_meshes`, a cell's meshes of 2000+ triangles become stand-ins (15 % of their
  triangles, their materials kept) in its scene, their full meshes saved apart and loaded on the ResourceLoader's
  threads within the streamer's `mesh_distance` (`max_mesh_loads` at once), freed beyond it. A flight across the world:
  mesh memory 42.2 → 24.6 MB on average, 51.4 → 28.2 MB at most. On the RTX (job f373): 62.0 → 44.4 MB, frames 1.8 →
  1.6 ms at the 99th percentile, a swap 18 µs on average; the picture 7.6 → 1.9 % off with proxies.
- Project > Tools > Bake Streaming Proxies; the editor check bakes the split level's proxies and a game shows them.
- Milestones 64-65 measured on the RTX (job f372): a terrain region enters in 1.07 ms of the main thread (2.67 at most),
  a flight across a 4 km map at 1.91 ms a frame at the 99th percentile.

## Milestone 65 — terrain regions streamed with the world's cells (2026-11-10)

- `FellowTerrain3D.region_group` (decision 0173): terrains of one group are tiles of one height field, joined without a
  seam — the same levels of detail along an edge, the border vertices from the exact heights, the border normals from
  the neighbours' heights, the layers' textures and macro variation in the group's space, the foliage on the group's
  grid; the brushes reach the regions under them and make the shared edges equal, and the editor's undo restores every
  region a stroke changed. Lavapipe: along 474 shared edges of a 4 km map both sides' heights within 7.6 µm, no crack.
- `split_into_regions(region_quads, directory, streamer)`: a terrain cut into regions, each a streaming cell;
  `FellowWorldStreamer3D.split_into_cells()`'s option `split_terrains` (the editor's Split into Streaming Cells). A 4 km
  map at 1 m keeps 17.6 MB of terrain textures around the camera instead of 213 MB.
- A streaming cell's terrains build their data (heights, mips, normals, splat maps, textures) on the worker thread that
  instances the cell: a region enters the world in 2.4 ms of the main thread (6.2 at most) instead of 46-60. On the
  RTX (job f372): 1.07 ms on average, 2.67 at most; a flight across the 4 km map at 1.91 ms a frame at the 99th
  percentile.

## Milestone 64 — the terrain's holes, foliage fading out, tree impostors in files (2026-11-10)

- Holes in `FellowTerrain3D` (decision 0172): `hole_map` (an image of the heightmap's size, a texel above half marks a
  vertex). A triangle touching a marked vertex is not drawn — the patches over holes are drawn with a second material
  that discards it, the rest of the terrain keeps its material and early depth test — does not collide (NaN heights in
  the height map shapes; Godot Physics and Jolt) and has no foliage; `get_height()` is NaN there, `is_hole()`.
  `paint_holes()` and `blit_hole_region()` update only their region; the editor's terrain toolbar has a Holes tool
  (Shift fills, undo). Lavapipe: a ball over a hole falls through, one beside it rests; no crack.
- `FellowTerrainFoliage.fade_distance`: plants shrink into the ground over their range's last metres instead of popping
  out — each instance scaled by the GPU's MultiMesh culling (`fellow_instance_set_instance_range_fade()`), meshes and
  materials unchanged. 0 (the default): as before.
- Tree impostors baked into files: `FellowTerrainFoliage.impostor` and `impostor_distance` — the meshes to the distance,
  the impostors from it, per instance on the GPU, the shadows per directional cascade;
  `FellowTerrain3D.bake_foliage_impostors()` (and the toolbar's Bake Impostors) renders decision 0122's octahedral
  impostors and saves them as `.res` with lossless atlases. Lavapipe: 323 trees to 400 m, 78.9 → 49.2 ms a frame (the
  RTX: 0.60 → 0.57 ms of GPU time, little to save with 323 trees).
- The regression suite's `terrain_holes` scene; `bench/terrain_holes_check.gd`.

## Milestone 63 — recorded pipelines, the burning material baked (2026-11-10)

- Recorded pipelines (decision 0171, `rendering/fellow/pipelines/record`, `replay`, `path`): a run with `record` on
  writes every pipeline the Forward+ scene shaders compile to `res://fellow_pipelines.json` — each under its material
  shader's code hash, with its key and its vertex and framebuffer formats described (their IDs depend on the run) —
  merged with what the file holds, written three seconds after something new and at the end (a game stopped from the
  editor is killed); `user://` when `res://` is read-only, nothing in the editor itself. With the file (`replay` is on
  by default) a shader's recorded pipelines are compiled in the background when its code is set at load, and again
  after the default specialization clears them. The file is tied to the scene shader's source and the project's
  defines, not to the engine's commit. The states check: 28 pipelines after the warm-up → 0, the 21 pictures the same
  bit for bit; Slice Lab's 8-minute soak on lavapipe: 18 pipelines compiled in play → 0, ubershader draws 268 → 7;
  on the RTX (20 minutes): load 1251 → 947 ms, the warm-up's 82 pipelines → 0, 67 → 33 compiled after it, the first
  frame's ubershader draws 22 → 0. Without the file nothing changes.
- The burning material baked into exports: Godot's shader baker now asks each node of an exported scene for the
  shaders it will make from its setup (`FellowBakeShaders::register_object_provider()`); `FellowBurnable3D` gives its
  burning materials' codes. A baked export with a burnable compiles no shader at its first start (1 before), also for a
  burnable a script adds with the same mesh's materials.
- `RenderingServer.fellow_get_stats()`: `pipelines_recorded`, `pipelines_replayed`.

## Milestone 62 — milestone 61's open topics, without a GPU (2026-11-09)

- The pipelines the slice still compiled after its warm-up, named (decision 0167): Slice Lab's soak on the RTX's
  timeline on lavapipe with `FELLOW_SPECIALIZATION_LOG=1` — of milestone 61's 20 draws that asked for a pipeline not
  compiled yet, 15 were a lightning's flash at night (the only directional light, without an angular size: Godot's
  soft-shadow bit fell), 2 a burnable's ash heap at its first ash, 3 autotune's evaluations. `frame/stable_scene_bits`
  keeps the soft-shadow bit with directional lights without a size (their soft path is the hard one: the same image;
  `pipeline_states_check.gd --flash=1`: the flash at night 3 → 0 pipelines, 27 pictures identical), and the warmer's
  shelf copies hidden meshes their nodes mark with the metadata `_fellow_shelf` (FellowBurnable3D's ash heap,
  FellowLightning3D's bolt — a degenerate triangle in its format until the first strike — and glow). After the warm-up
  37 → 20 pipelines (autotune's evaluations, prepared ahead), 2 with autotune off; draws that asked 20 → 3.
- The rain's particle process materials baked into exports (decision 0169): `FellowRain3D`'s baking provider gives
  the particles shaders of its four process materials (made by one function for the node and the provider); a baked
  rain export compiles no shader at its first start (3 before).
- A playable showcase, `showcase/` (README.md): a 512 m valley built by code — `FellowTerrain3D` with six painted
  layers and its foliage (grass, flowers, bushes, broadleaf trees, pines, rocks), a lake (`FellowWater3D`), a village
  of six houses round a campfire (`FellowGasVolume3D`, a log burning with `FellowBurnable3D`), the weather from sun to
  blizzard (`FellowWeather3D`, keys 1-7), a 12-minute day with the atmosphere's sky, clouds and stars, lamps and lit
  windows at night, `FellowPipelineWarmer3D` at the start — and a third-person character: NVIDIA's SOMA mannequin moved
  by `FellowMotionCharacter3D` (MotionBricks on the GPU, no clips) when its weights and mannequin are in
  `showcase/external` (not part of the repository), else a humanoid made in code with blended clips.
- `FellowMotionCharacter3D.set_ground_height()` / `get_ground_height()`: the network's motion is on a plane; a game
  sets the ground's height under the character every frame to walk it over uneven ground (the showcase's terrain).
- `FellowGasVolume3D.edge_fade` (metres, 0 by default: the same pictures): the smoke drawn faded out near the box's
  faces (all but a closed floor) — where it leaves through an open face or piles against a wall the box no longer
  shows as a hard edge. Only the drawing and the smoke's shadows; the simulation is unchanged.
- The bench's and the showcase's atmosphere sky shaders draw the sun's disk only outside the radiance pass
  (`AT_CUBEMAP_PASS`): in the radiance map the moving disk was caught or missed by its roughest layer's filtering, and
  with the day's clock running the ambient light jumped by about half every second (decision 0108).
- `FellowGasVolume3D.fire_light_smoothing` (seconds, 0 by default: as before): the fire light eased towards the
  simulated emission, its range four times slower, instead of following every step's sum of the turbulent flames.
- `FellowGasVolume3D.prewarm_steps` no longer overflows the GPU's queue of steps when several physics ticks come
  before a frame (a slow first frame, Movie Maker below the physics rate): a tick prewarms what the queue still holds.
- The showcase after a first play (report 62, section 6): the campfire's smoke rises 6.5 m through open sides and
  top, thinning out and faded at the box's faces, burning from the start; the cameras keep 0.9 m over the terrain (looking
  up had swung them into the grass: one-frame dark flashes); F3 turns the effects off one at a time; MotionBricks'
  character plays the network's motion 1.3 times faster while it moves (walking about 2 m/s, running 4.3 m/s), jumps
  (Space) and rolls (C) — both drawn over the network's motion, which has neither — and the README tells where its
  files come from.

## Milestone 61 — what the CPU could still do without a GPU, then the RTX's measurements (2026-11-09)

- Fewer pipelines compiled after the warm-up (decision 0167). `frame/stable_scene_bits` also keeps Godot's directional
  soft-shadow bit and the sun's traced and contact shadow masks' bits in frames without a directional light (the sun
  set: the directional loops run no iteration), and light tile culling's kept bit only while tile culling is on
  (autotune's choice off: the frames with and without omni lights share pipelines). `FellowPipelineWarmer3D.shelf`
  (on): after its six directions the warmer draws a copy of each distinct mesh and material set of the world's
  visible MeshInstance3D, MultiMeshInstance3D and terrain foliage from a view 4 km above the camera, 36 a page, the
  copies hidden from every other camera by a 40 m visibility range and following their sources' meshes and materials
  while shown. Slice Lab's 4-minute soak on lavapipe (weather every 10 s, fire every 15 s, an 80 s day): the draws
  that waited for a pipeline after the warm-up 9 → 1, pipelines compiled after it 23 → 9 (8 of them autotune's,
  prepared ahead), the same image (decision 0161's 21 states with soft and contact shadows: 21 of 21 identical). On
  the RTX laptop the slice's 20-minute soak compiles 88 pipelines after its warm-up instead of 151 (f364).
- The terrain's edits upload only the texels they changed (decision 0168): `RenderingDevice::
  fellow_texture_update_region()` and `RenderingServer::fellow_texture_2d_update_region()` write one mip's rectangle;
  `FellowTerrain3D` uploads the edited region of its heights, normals and splat maps and the mips over it
  (`streaming/terrain_region_uploads`, on). A 2049² map's update after a stroke 8.78 → 0.36 ms (heights and normals),
  4.62 → 0.03 ms (a splat map) on lavapipe; every texture bit-exact with a rebuilt terrain's. On the RTX laptop a
  4097² map's frame while sculpting 63.3 → 0.9 ms, while painting 29.2 → 1.0 ms.
- Compiling less at load (decision 0169): FellowEngine nodes' compute kernels (water, rain, snow tracks, shallow
  water) kept as SPIR-V in `user://fellow_kernel_cache` after their first compilation (`frame/kernel_spirv_cache`):
  glslang runs once per machine and build, not once per session (28 → 0.5 ms on the cloud's CPU, on the thread adding
  the first such node); the same pictures. And Godot's shader baker now finds FellowEngine's effects, which are made
  when first used, and the materials its nodes make from code (water, rain, snow, lightning, shallow water, ash,
  terrain): an export starts by making them (`RendererSceneRenderRD::fellow_create_versions_for_baking()`,
  `fellow_bake_shaders.h`), so baked exports of the fire and water scenes compile no shader at their first start
  instead of 3 and 1 (70-72 without the baker). The rainbow, the one effect first used in the middle of a game, is
  made as soon as a rain node that may show one is active, its shader compiled on the worker threads
  (`ShaderRD::fellow_version_compile_start()`): compiled at the first bow, it was the RTX soak's one long frame (89.6
  ms at the first rain of a new build, whose shaders miss Godot's shader cache).
- ThreadSanitizer over milestone 60's threading (decision 0170; a debug template with `use_tsan`, lavapipe): the crowd in
  process groups with the separate render thread, water, rain and fire with it, Jolt's and GodotPhysics' piles, the
  slice. Three data races, all in stock Godot code, fixed — the RenderingServer's change counter (now atomic),
  ProjectSettings' flag setters (now under the settings' lock), Jolt's job timings (reset under their lock); none in
  FellowEngine's own threading.
- `fluid/gas_mg_fine_sweeps` (2, as before): the campfire's multigrid with one sweep at its finest level keeps the
  flame within 0.2 % of the reference after 180 steps (lavapipe), its residual 20 times decision 0117's; on the RTX
  the pressure solve 0.654 → 0.572 ms, under 2 % of the GPU's frame — the default stays 2. FellowEngine's stats report
  the driver's pipeline cache (`pipeline_cache_bytes`), the soak its size a second: on the RTX it stays flat (370.7 →
  371.2 MB in 20 minutes) while the process grows by about 2.7 MB a pipeline compiled, level without — the soaks'
  memory growth is the pipelines compiled, not a leak (decision 0166).
- The image regression suite: a skinned crowd, the separate render thread's group (lights, water, rain, fire, the
  crowd) and the crowd in process groups, each the same picture as the default's; 24 pictures exact.
- On the RTX laptop the slice's frame with milestone 61's template is milestone 60's (f365, interleaved: 5.05 against
  4.99 ms, inside the runs' noise).

## Milestone 60 — the main thread's CPU, and the hitches it waited through (2026-11-07)

- `rendering/fellow/animation/shared_no_override_pose`, `animation/lazy_simulator_poses` (on, exact), decision 0163:
  a skeleton without overrides copies each bone's global pose into the deprecated one without overrides instead of
  multiplying again; the compatibility PhysicalBoneSimulator3D of a skeleton without physical bones copies the bones'
  poses only when one is bound or read. Crowd Lab headless (128 characters, release template): the main thread's
  instructions −10.4 % (AnimationTrees blending −6.4 %), its CPU time a frame −7.3 % (−4.4 %), the same bone poses bit
  for bit. Rejected: the keys' part of each rotation track's slerp kept per thread — fewer instructions, but more CPU
  time (its table's cache misses cost more than the `acosf()` it saved).
- The main loop's frame step split in FellowEngine's stats (`main_frames`, `main_process_usec`, `main_draw_usec`):
  `Performance.TIME_PROCESS`, which the bench reported as the main thread's process time, is the largest of each
  second's steps with the RenderingServer's sync and draw — the frame's rendering and its waits for the GPU with the
  default thread model. `run_benchmarks.py` reports `scene_process_ms` and `main_draw_ms`.
- Crowds on worker threads: Crowd Lab `--crowd_groups=N` (Slice Lab `--slice_crowd_groups`) puts the characters in N
  sub-thread process groups (Godot's `Node.process_thread_group`), each posing its characters and updating their
  skeletons on a worker thread. The same bone poses bit for bit as on the main thread (AnimationPlayers and blending
  AnimationTrees, every FellowEngine animation shortcut on; `animation_check.gd --groups=N`), the same pictures on
  lavapipe, no error in a debug build.
- With the separate render thread (decision 0165): `FellowWater3D`, `FellowRain3D` and `FellowShallowWater3D` free their
  RenderingDevice resources on the render thread (before: 80 errors and leaked textures, uniform sets and shaders at
  the slice's exit, and the same for any of them freed during play). And a deadlock in Godot's shader compilation
  fixed: a thread waiting for a version's compile group while holding the version's mutex, with every worker blocked on
  that mutex or busy (the cloud's two-thread pool, one of them the render thread: two runs of four hung); the waiter now
  compiles the group's variants no task has started itself. The same shaders; the image regression suite exact with
  its shader cache warm and cold.
- The pipeline cache's size queries and saves off the RenderingDevice's lock (decision 0166): a save copies and
  checksums the whole Vulkan pipeline cache (hundreds of MB in FellowBench) and held every thread's device calls while
  pipelines compiled in the background. The slice's 20-minute soak on the RTX: the largest frame 397 → 20 ms, hitches
  16 → 4, none over 30 ms, with more pipelines compiled than before.
- Measured on the RTX laptop: decision 0163's settings −8.5 % of Crowd Lab's scene process (256 characters), −9 % in
  the slice; Crowd Lab in 8 process groups 3.07 → 1.04 ms of scene process, the frame −33 % (blending −53 %); the
  separate render thread −32 % in Crowd Lab, nothing in the GPU-bound slice; 10 minutes of the slice with both, no
  error, the largest frame 22 ms. The slice's scene process is 0.66 ms a frame (its "5.3 ms" was the draw's).

## Milestone 59 — pipelines compiled ahead (2026-11-06)

- `rendering/fellow/frame/stable_scene_bits` (on, exact), decision 0161: the colour passes' specialization bits that
  follow what a frame holds (absent decals, probes and lights, the positional lights' loop variants and tile culling,
  the weather's wetness, the clouds' shadow) keep the feature's code once the feature exists or has been in a frame.
  lavapipe: a scene's 21 changes (rain, lights, decal, probe, night) compile 5 specialized pipelines instead of 28,
  every state's picture bit-exact; the image regression suite exact. `FELLOW_SPECIALIZATION_LOG` lists the pipelines
  a draw asked for before they were compiled.
- `FellowPipelineWarmer3D`, decision 0162: while a scene loads, its world drawn from the camera's place in six
  directions into a small viewport set up as the game's, until nothing more compiles. lavapipe: a camera turning to
  24 unseen objects compiles 0 pipelines instead of 7 (MSAA: 0 instead of 13, 6 of them compiled while the frame
  waited). The FellowEngine menu's "Add Pipeline Warm-up"; Slice Lab `--slice_warmup=node`.
- `bench/pipeline_states_check.gd`, `bench/pipeline_warmup_check.gd`. The RTX's measurements (the general pipelines'
  cost, the slice's soak) wait for the GPU.

## Milestone 58 — sculpting and painting the terrain (2026-11-06)

- `FellowTerrain3D.sculpt()` (raise, lower, smooth, flatten) and `paint()` (a layer's weight, the weights still adding
  up to 1; the splat maps made when there are none), decision 0160: only the region under the brush is updated — the
  queries at once, the textures and collision shapes once at the frame's end, the foliage over it scattered again —
  and the result is the terrain a rebuild from the edited images makes, every texture and mip bit for bit (lavapipe,
  read back). `blit_heightmap_region()`, `blit_splat_region()` write rectangles back (undo, stamps). A dab costs 0.02-0.08 ms of the main thread on the cloud CPU whatever the
  map's size (a rebuild, what an edit cost before: 28-455 ms for 1-4 km); the RTX's measurement is still to run.
- In the editor: a selected terrain's toolbar in the 3D view (Raise, Lower, Smooth, Flatten, Paint, the layer, the
  brush's size and strength), the brush drawn on the ground, one undoable action a stroke; the FellowEngine menu's
  "Add Terrain (256 x 256 m, to sculpt)".
- Fixes found with it: a data rebuild of a drawn terrain freed the patches' mesh under them (errors, nothing drawn);
  the foliage cells' tasks now read their own copies of the heights and weights, so edits never race them and a
  rebuild no longer waits for them (a first paint stroke over grown foliage: 3.6 s → 9 ms on lavapipe).
- `bench/terrain_edit_check.gd`; the editor check's terrain brush and setup.

## Milestone 57 — terrain and foliage (2026-11-06)

- `FellowTerrain3D` (decision 0158, module `fellow_world`): a height field (an `Image` of metres) drawn with
  continuous levels of detail — patches whose vertices morph onto the next coarser grid, reading a coarser mip as they
  do: no cracks, no popping — as one MultiMesh the GPU culls patch by patch, written into a second hidden one when the
  selection changes (TAA and FSR see a static ground). Up to 9 `FellowTerrainLayer` materials blended by two splat
  maps and their heights, triplanar per layer; collision (height map shapes, the finest level's triangles); height,
  normal and weight queries; a level-of-detail debug view; the editor's camera in the editor. Slice Lab's 1 km valley at
  1 m (16 times its 4 m chunks' vertices near the camera) on the RTX at 1080p: GPU 3.36 → 3.59 ms (+0.23), the render
  thread 0.16 ms shorter, the terrain's own main-thread time 17 µs a frame; no crack in any view of a 1 km or a 4 km
  map, physics rays within 0.08 mm of `get_height()`.
- `FellowTerrainFoliage` (decision 0159): plants and rocks scattered by the terrain over a layer, its slopes and
  heights, in cells around the camera made on worker threads, each its own MultiMesh culled per instance by distance:
  Slice Lab's trees and grass scattered by the terrain cost the same GPU time as placed by its script, the render thread
  0.12 ms less.
- Exact fixes found with the terrain: `FellowGasVolume3D` no longer copies the data of height map and concave shapes in
  its box every step; the directional shadow cache ignores changes of instances that cast no shadows.
- Slice Lab `--slice_terrain=node`, `--slice_foliage=node`; `bench/terrain_check.gd`; the image regression suite's
  `terrain` scene.
- After milestone 55 (decision 0156): the first snow's pipelines and particles shader and a relit burnable's mesh
  readbacks moved to the nodes' setup — on the RTX the first snow's worst frame 51 → 10.1 ms, a relighting's 17-22 →
  6-9 ms, 2 hitches in 5 minutes (both the GPU's fence).

## Milestone 56 — motion matching on uneven ground; mirrored and tagged clips (2026-11-06)

- `FellowMotionMatcher3D.ground_adaptation` (decision 0157, off by default): rays under the root and each foot's heel
  and toe; the root follows the ground, the feet are placed on it by the legs' IK, the hips lowered for the lower foot,
  a foot that is down turns to the slope. Against the root alone following the ground, a walking character's stance
  feet: 8° slope 28 -> 3 mm off (in the ground in 49 % -> 0 % of the frames), stairs 56 -> 13 mm, uneven ground
  27 -> 10 mm.
- `FellowMotionDatabase.build()` options `mirror` (each clip mirrored, left and right swapped: Matching Lab's foot
  sliding -18 %, trajectory error -6 %, a search twice as long) and `tags`; the matcher's `tags_required`,
  `tags_excluded`.
- Checks: `bench/ground_check.gd`, `bench/matching_tags_check.gd`.

## Milestone 55 — a vertical slice played for an hour (2026-11-06)

- Slice Lab (`--scene=slice`, decision 0156): a 1 km valley with a town of 711 buildings under HLOD and its street
  lights, a campfire with a burning log, a crowd, a lake, 9 000 trees and grass in the wind, weather turning through
  rain, storm and snow, a day turning to night. The bench's `--mode=soak` plays a scene in real time for minutes or
  hours: per second the frame times, the GPU's and render thread's time, memory and counts, pipelines compiled; the
  hitches with what compiled in them; growth per hour. `benchmarks/tools/soak/run_soak.py` samples the process's and
  the GPU's memory from outside, `soak_report.py` writes the CSV, a chart and the summary.
- The first hour on the RTX at 1080p: 251 fps, Godot's video memory 394 -> 395 MB, objects +2 — and 61 hitches, an
  error every four minutes and a 4.8 s freeze at the first start, all from the burning log. Fixed in
  `FellowBurnable3D`: lit again, it freed invalid texture IDs (new texture objects now) and compiled its shaders anew
  (from `FellowSharedShaders` now); its ash heap made its material at the first ash (now when it is configured,
  hidden). The hour again: 252 fps, 25 hitches (2 compiling pipelines, 31 before), no error, no frame over 21 ms in
  the first 25 s.

## Milestone 54 — an image regression suite (2026-11-06)

- `tools/fellow_regression/regression.py` (decision 0155): FellowEngine's features in twelve scenes (lights, shadows,
  materials, water, rain, snow, lightning, fire, HLOD and MultiMesh geometry, sky and clouds, ray tracing) rendered on
  lavapipe and checked against goldens, the release template against the editor build, and FellowEngine off against
  on; every picture bit-exact between runs and between the two builds. `--goldens` keeps another GPU's own.
- Found and fixed: a FellowGasVolume3D's gizmo and handles showed its box centred on the node (its floor is there);
  the FellowEngine menu's Fire, Smoke and Burning Box put their emitters below the box, emitting nothing. An emitter
  that does not reach into its volume's box now has a configuration warning. The clouds traced a quarter of the
  pixels per frame showed a checker of noise (each pixel's history lost): fixed, 45.3 -> 51.5 dB against the full
  trace (a pan 44.4 -> 47.5 dB).
- fellow_agent seeds the global random sequence (`--seed`): two captures of a scene are the same picture.

## Milestone 53 — the editor: scene opening, ready-made setups, grouped inspectors (2026-11-05)

- FellowEngine's nodes make the shaders, compute kernels, meshes and textures they all share once per session
  (`FellowSharedShaders`, decision 0154): a scene with water opened again in the editor 492 -> 4.9 ms, a second water
  body 270 -> 0.1 ms; rain, lightning, shallow water likewise. Exact.
- A FellowEngine menu on the 3D editor's toolbar: Weather, Fire, Smoke, Burning Box, Ocean, Lake, Flowing Pool, added
  wired up (no configuration warning), undoable.
- Inspector groups for shallow water, lightning, the world streamer, the motion matcher and the motion character.
- Measured: FellowEngine adds nothing to the editor's start (15.1 s on, 15.3 s off, lavapipe).

## Milestone 52 — the editor: idle, warnings, handles, tooltips (2026-11-05)

- An untouched editor draws nothing for FellowEngine's nodes unless they move (decision 0153): values written to the
  RenderingServer only when they change (lightning no longer redraws between strikes, rain no longer rewrites its
  drops' positions), passes not dispatched again with the same inputs, simulations asking for a frame per step
  instead of piling up work the editor never drew. The 3D toolbar's **Animate** toggle (editor setting
  `editors/3d/fellow/animate_nodes`) stills water, rain, lightning, wind, gas and shallow water in the editor.
- Configuration warnings for FellowEngine's nodes; `FellowEditorTools.get_configuration_warnings()`; fellow_agent's
  `check` reports every node's warnings.
- Handles (undoable) for emitters, water extent, shallow-water domain, streaming cell bounds, snow trackers and
  lightning; gizmos redraw when their properties change.
- Tooltips for all 296 `rendering/fellow/*` project settings (`doc/tools/fellow_settings_docs.py`).
- Fixed: the rain's shelter heightfield bound in the pass rendering it (debug builds skipped those draws: no shelter in
  the editor); shallow water's timestamps overflowing between frames; Update Continuously set in Editor Settings
  applying only after a restart (stock); a still capture hanging in a project with low processor mode.

## Milestone 51 — tools for AI agents (2026-11-05)

- `tools/fellow_agent/fellow_agent.py` (decision 0152): `capture` (a scene to PNGs from its camera or framed views,
  lit where it has no light, with image and rendering statistics and the share of the picture the scene covers),
  `describe` (the node tree as data), `check` (every scene and script loaded, instantiated and run), `run` (frame time
  percentiles), `compare` (two PNGs), `import`, `api` (the class reference), `init` (an `AGENTS.md` for a game
  project). JSON out, exit codes 0/1/2, errors with file, line and the script's backtrace (a `Logger`).
- `AGENTS.md` (and `CLAUDE.md`) at the root: where things are, how to build and check, the fork's rules and scope.
- A self-test with planted faults: 26 of 26 checks on the cloud's CPU, 22 of 22 on the RTX.
- Fixed: debug builds — the editor's viewport, debug exports — drew no omni lights, spot lights, decals or reflection
  probes since milestone 10 (the element-parallel cluster pass bound with the stock variant's uniform set; release
  builds unaffected).
- A Linux editor build in the cloud.

## Milestone 50 — tools (2026-11-05)

- An editor module (`fellow_editor`, editor builds only; decision 0151): gizmos for FellowEngine's nodes — a gas
  volume's box with resize handles, emitters' shapes, water extents, shallow-water domains, streaming cells' bounds,
  the wind's arrow, lightning's distance band, snow trackers — and their icons as billboards; `.bvh` motion capture
  imported as `Animation` resources (scale, the T-pose frame; a CMU preset); Project > Tools > **Split into Streaming
  Cells** for the selected `FellowWorldStreamer3D` (`split_into_cells()`: the level's nodes grouped on a grid, saved as
  cell scenes, replaced by `FellowStreamingCell3D`s).
- `FellowWorldStreamer3D.editor_preview` (on): the cells show their content in the editor, not saved with the level.
- Icons for the 17 FellowEngine classes; Performance monitors for texture streaming, world streaming and motion
  matching (the debugger's Monitors tab); the class reference complete (184 entries of the nodes were missing or
  empty; three classes outside the module's documented list).
- Checks: an editor check (`benchmarks/tools/editor_check/`: an editor plugin exercises all of it, then the split
  level is streamed back in a game; on the RTX with the editor's window and headless: every check passes), and the
  web template in headless Chromium (`benchmarks/tools/platforms/web/`).
- Android's (NDK r29, arm64) and the web's (Emscripten 4.0.10, threads) templates built; the web one runs with its
  cells streamed on worker threads and desktop OpenGL's image.
- Fixed: `FellowWeather3D.weather` missing in the editor (a two-argument setter, refused by debug builds); the pipeline
  cache's limit read before its setting existed (64 MiB, not 1024: caches over it discarded at start since milestone
  9); the water invisible with the Compatibility renderer (OpenGL's depth read as Vulkan's, an unbound wave array
  lifting it), and the shallow water's refraction there; a streaming cell's double free at exit; the web image's
  missing `libatomic`; the platform builds' copies of the host's generated sources.

## Milestone 49 — platforms without their hardware (2026-11-05)

- `FELLOW_VULKAN_PROFILE` (a diagnostic, decision 0150): the Vulkan driver presents its device as another — Android's
  2022 and 2025 baselines, Intel UHD 620 and Arc A770, GTX 1060, RTX 4070, RX 580, RX 7800 XT, Apple M1 through
  MoltenVK, or with ray tracing, half floats or subgroup operations removed — from the Khronos Vulkan-Profiles data;
  every shader checked against it and what the device could not run reported with the pipeline.
- Checks (`benchmarks/tools/platforms/`): every lab with Godot's three renderers (lavapipe, llvmpipe; `renderer_matrix`
  in bash and Python), eight labs on the nine devices, the Windows template built with MinGW and run under Wine, the
  Linux arm64 template cross-built and run under qemu, a clang build. No crash; Linux's images everywhere.
- Fixed: a crash on GPUs without buffer device addresses (index buffers' uninitialized creation flags); the Mobile
  renderer losing positional lights (Lights Lab 9.3 → 36.6, stock's); stock's DirectInput hook calling
  `VirtualProtect()` with a null pointer (a crash under Wine); fragment-to-light varyings counted as interpolants (stock)
  and the water's two in one; gas volumes dense where the sparse kernels' 11 storage images do not fit, the trace pass
  on textures and one sampler (27 samplers before); the glow's 16 × 16 tiles only on devices with 256 invocations a
  workgroup; MultiMesh culling's buffer leaked at exit; MinGW's and clang's build errors and warnings; warnings instead
  of errors from FellowEngine's nodes with the Compatibility renderer.
- On Android's 2025 baseline and every desktop profile FellowEngine needs nothing the device lacks; ray tracing falls
  back to the same image without it.
- On the RTX: the Mobile and Compatibility renderers give stock's images in every lab; milestones 45-49 cost nothing
  in frame time (the 9 standard scenes against milestone 44). Milestones 45-48 measured there: FSR 3.1 at FSR 2.2's
  cost and **on by default** (`upscaling/fsr3`) after a crash at its first frame was fixed (an ODR violation between the
  two FidelityFX SDKs' `FfxResourceDescription`); texture streaming's per-view memory as on lavapipe (video memory
  377-457 instead of 667 MiB, the first frame 838 → 103 ms); motion matching 0.12-0.89 ms a step for 16-256 characters
  on the laptop's CPU.

## Milestone 48 — motion matching (2026-11-04)

- `FellowMotionDatabase` and `FellowMotionMatcher3D` (new module `fellow_animation`, decision 0149): clips sampled into
  a database of poses, foot contacts and 27 features a frame (feet, hips, the trajectory 1 s ahead), searched every
  0.1 s with bounding boxes; inertialized transitions, the root's motion pulled towards a spring simulation, foot
  locking with two-bone IK, LOD by camera distance, `advance_group()` on the worker threads; BVH import; the database
  saved as a 16-bit resource.
- Matching Lab on 27 minutes of the CMU motion capture database: 16 characters follow their paths within 3.9 cm, feet
  sliding 0.11 m/s in contact (0.32 without locking and IK), popping's p99 36 mm (174 with hard cuts), 15 µs a
  character a frame; 256 characters 4.5 ms, 3.0 ms with LOD.
- FellowBench: Matching Lab (`--scene matching`), `bench/matching_eval.gd`, `scripts/get_cmu_locomotion.sh`.

## Milestone 47 — FSR 3.1 upscaler (2026-11-04)

- `rendering/fellow/upscaling/fsr3` (opt-in, decision 0148): a viewport in the FSR 2 scaling mode runs AMD's FSR 3.1
  upscaler instead of 2.2 — the FidelityFX SDK 1.1.4 (MIT) built from source, with a RenderingDevice backend
  (`effects/fsr3.{h,cpp}`) and the SDK's Vulkan GLSL through Godot's shader builder; Godot's FSR 2.2 options kept
  (motion vectors derived from the depth where there are none, the reactive mask clamp); half precision where the GPU
  has it; the SDK's tunables and debug view as settings.
- PBR Lab on lavapipe at 960 × 540 against an accumulated ground truth: at 2× upscaling less error in motion (−9 to
  −24 %) and flicker (−10 to −14 %) than FSR 2.2, +0.6 to +1.0 dB; in quality mode +0.7 to +0.9 dB, within 0.4-1.1 dB
  of native TAA; as native AA +1.3 dB over TAA in three of four sequences. Still views at 2× softer (−1.3 dB in two of
  three). 8.5 MiB less memory than FSR 2.2 at that size; no validation message of its own.
- FellowBench: `tools/lavapipe/upscaler_check.sh` (TAA, FSR 2.2 and 3.1 at three scales against a ground truth).

## Milestone 46 — world streaming (2026-11-04)

- `FellowWorldStreamer3D` and `FellowStreamingCell3D` (module `fellow_world`, decision 0147): a level in cells, each a
  PackedScene loaded on the ResourceLoader's threads when the camera or a target comes within its distance, its nodes
  built on a worker thread, a few cells a frame entering or leaving the tree (the nearest in first, the farthest out
  first), with hysteresis, `prefetch()`, `load_now()`, `is_settled()` and statistics; the cell's own children are its
  proxy while it is unloaded.
- Texture streaming: a texture not used yet keeps its small mips (a cell's, loaded before the cell enters the tree);
  2D, the decal and area light atlases and materials whose UVs it cannot estimate pin a texture at full resolution.
- A `.ctex` loads where a `Texture2D` is expected (stock: no loader).
- World Lab (lavapipe): at 1080p 289-612 MiB of video memory a view instead of 740 MiB with world and texture
  streaming, the start 4.09 → 2.55 s, the same images (one pixel by one level at 1080p); over a flythrough the peak
  708 → 321 MiB.
- FellowBench: World Lab (`--scene world`, `--world_streaming=1`, generated cells), the streamer's state in results.

## Milestone 45 — texture streaming (2026-11-04)

- `rendering/fellow/streaming/textures` (opt-in, decision 0146): VRAM-compressed `CompressedTexture2D` with a full mip
  chain load only their mips at or below 128 px; the RenderingServer estimates the finest level each texture can need
  (the cameras, the instances' boxes and scales, the surfaces' texel densities, BaseMaterial3D's `uv1_scale`, the
  samplers' bias), loads it on worker threads, swaps it in under the same RID and releases it when no longer needed,
  within `texture_budget_mb`. `RenderingServer.fellow_get_texture_streaming_info()` reports it.
- Streaming Lab at 1080p (lavapipe): 3-77 MiB of its 384 MiB of textures resident in a view (all texture memory 221-295
  MiB instead of 602), the textures loaded in 7-9 ms instead of 460 ms, the same images bit for bit once loaded.
- FellowBench: Streaming Lab (`--scene streaming`; textures from `benchmarks/scripts/make_streaming_textures.py`),
  waits for streaming before captures (`--streaming_fixed_wait`, `--streaming_prepass`), the streamer's state in every
  result; `tools/lavapipe/streaming_check.sh`.

## Milestone 44 — neural motion's CPU side and faster tensor-core products; readiness for AAA (2026-11-04)

- `fellow_motion` off the main thread (decision 0144): the service encodes the requests and decodes and
  endpoint-corrects the results on the WorkerThreadPool, one task per request; a character interpolates, crossfades
  and poses only the 26 joints the model drives, with a ring history and no per-frame allocation. The characters'
  main-thread work a step: 64 characters 0.888 → 0.212 ms on the laptop's CPU (16 on the cloud's: 0.611 → 0.204 ms);
  the same motion within 8.3e-6 m.
- The tensor-core products (decision 0145): the activations loaded four at a time (`vec4`), the epilogue's addresses
  and mask a row at a time — the same results bit for bit, 13-22 % faster from 2 requests on (RTX: 16 at once
  15.23 → 12.60 ms, 64 at once 51.55 → 40.95 ms; a request 4.03 ms). Six other designs measured and not kept.
  `FELLOW_MOTION_GEMM=scalar` (diagnostic) takes the scalar loads.
- Motion Lab on the RTX (milestone 43's build alternating with this one): 64 characters 8.52 → 7.33 ms a frame in
  flush mode, 4.30 → 3.56 ms asynchronous (the 99th percentile 11.6 → 6.1 ms); 16 characters 3.51 → 3.43 ms.
- `docs/fellow_aaa_readiness.md`: an assessment of FellowEngine's readiness for AAA production — a scorecard of 14
  areas, what an AAA project would hit first, what it is ready for now, a path in seven steps. Verdict: not AAA-ready as
  an engine (streaming, platforms and GPU breadth, upscaling, animation, tools); a renderer and simulation close to
  AAA-class on Windows/Vulkan/NVIDIA, ready for PC indie and AA games with a high-end look.

## Milestone 43 — neural motion: MotionBricks on the GPU (2026-11-03)

- New module `fellow_motion` (decision 0143): `FellowMotionModel` runs NVIDIA's MotionBricks SOMA model (a root and a
  pose transformer and a VQ-VAE decoder, 188 M weights) on a RenderingDevice of its own — a plan of 251 GPU ops that
  `tools/fmb_convert.py` writes from the released checkpoints into an `.fmb` file, checked against NVIDIA's PyTorch
  model (`tools/fmb_plan.py` executes the same plan in NumPy). The weights are NVIDIA's (NVIDIA Open Model Agreement)
  and are not in this repository.
- Tensor cores: `VK_KHR_cooperative_matrix` enabled in the Vulkan driver when the device has it
  (`RD::SUPPORTS_FELLOW_COOPERATIVE_MATRIX_F16`); the products run on it with fp16 weights (`cooperative_matrix`, on),
  fp32 elsewhere. RTX 5070 Ti Laptop (in use): a request 4.3 ms, 16 at once 18 ms, 64 at once 62 ms (the first
  version: 32, 68 and 171 ms).
- `FellowMotionCharacter3D`: a desired velocity in, a walking, jogging, running, turning, stopping and standing
  character out, with no animation clip (root-only targets from a relaxed stance; MotionBricks' runtime glue ported);
  one batched service per model (async, flush or immediate); a skinned SOMA mannequin (`tools/fmb_usd.py`). Falls
  (the training data has lying and crawling): the hips' target at a standing height, `get_up` (a standing target on
  the ground), `max_speed` 3.5 m/s — 32 characters for 40 s: 8 falls, all back up within 0.4 s.
- FellowBench: Motion Lab (`--scene=motion`, `--motion_model`, `--motion_body`, `--motion_crowd`, `--motion_mode`,
  `--motion_rewind`): 64 characters 9.4 ms a frame waiting for their requests, 5.0 ms with a worker thread running them.

## Milestone 42 — the weather's own rain in thin streaks (2026-11-03)

- A rain node a FellowWeather3D creates (`create_missing`) draws its drops as thin streaks (`streak_width` 0.004 m,
  decision 0142); a scene's own rain node is left as it is.
- The final check of milestones 36-42 (`m41_f317_final`, stock against fellow229 and fellow236, the nine scenes, 3 runs):
  fellow236 faster than stock in every scene (Stress Lab within its spread, its frame bound by the render thread), and
  against fellow229 each scene within its runs' spread. The README's table is from it.

## Milestone 41 — thin rain streaks (2026-11-03)

- `FellowRain3D.streak_width` (decision 0141, 0: off): each drop drawn at its own width or a pixel at its distance,
  whichever is wider, its opacity scaled by their ratio — near drops thin, far ones faint, instead of `drop_size`'s
  bars (heavy by day against the sun). FellowBench: `--weather_streaks`.
- Milestone 36's frozen puddles: white only at their edges (the rims had turned whole puddles white in Bistro).

## Milestone 40 — slush (2026-11-03)

- `FellowRain3D.slush` (decision 0140): melting snow soaks with its meltwater — greyer, glossier, without glints, its
  thin parts first. A FellowWeather3D raises it while snow lies above 0 °C (fully at 3 °C or in the rain) and lowers it
  below freezing and under fresh snow. `--weather_ground` takes a fifth value, the slush.

## Milestone 39 — wind-driven rain (2026-11-03)

- `FellowRain3D.wind_driven` (decision 0139, 1): in a wind the shelter follows the drops' slant — the heightfield
  looked up back along their drift as well — so walls facing the wind get wet under their eaves (and as wet as the
  ground), the rain reaches in under an awning on the windward side and the lee stays dry beyond it. Nothing changes
  below a drift of 0.2 m per metre of fall (a breeze of 1.8 m/s); not for snow. FellowBench: `--weather_wind_driven`.

## Milestone 38 — wetting and drying in patches (2026-11-03)

- `FellowRain3D.drying_patches` (decision 0138, 1): between dry and wet the surfaces are wet in patches — the puddles'
  noise taken as the ground's lows and highs, a broad and a fine noise with it: the highs dry first and wet last — and
  the puddles shrink to their deepest instead of fading. At a wetness of 0 or 1 the image is the same bit for bit.
  FellowBench: `--weather_patches`.

## Milestone 37 — showers (2026-11-03)

- `FellowRain3D.showers` (decision 0137, 0 by default): with the volumetric clouds on, it rains at the camera only as
  the clouds above let it — under their dense parts, as the rain shafts hang — so showers pass over with the clouds'
  wind: the drops, the ripples, the wetting and the settling snow follow; the shafts and the rainbow keep the rain set.
  A one-thread probe of the clouds' layer at the camera, read back asynchronously (0.010 ms on the RTX, `m37_f310`).
  FellowBench: `--weather_showers`. Showcase: a shower passing over Bistro.

## Milestone 36 — ice and hoarfrost (2026-11-03)

- `FellowRain3D.freeze` and `.frost` (decision 0136): the water on the ground frozen — no ripples, the puddles dark
  clear ice with white rims, the wet film a glaze — and hoarfrost on what faces the sky where the rain would reach, fine
  grey-white grains growing into a veil the surface shows through, glinting in the sun; under the snow cover.
- A FellowWeather3D's ground model below 0 °C freezes the water and, when nothing falls, grows the frost (slower under
  clouds), in the rain node's `freezing_time` (600 s) at −5 °C; above 0 °C both thaw. It creates its rain node in the
  cold; ready below 0 °C it settles the ground frozen and frosted under a clear sky.
- FellowBench: `--weather_temperature`, `--weather_ground=<wet,snow,freeze,frost>`, `--weather_drops` (0: none drawn).

## Milestone 35 — rain shafts (2026-11-03)

- `FellowRain3D.shafts` (decision 0135): while it rains under the volumetric clouds, grey curtains of rain hang below
  their dense parts (local coverage 0.7 to 0.95), streaked along the fall and drifting with the clouds, beyond 2 km of
  the camera and fading out from 25 to 40 km; snow's are brighter. Without rain the clouds' trace is the same bit for
  bit (lavapipe).
- Cost on the RTX (Water Lab in showers, `m35_f307`): the clouds' pass +0.05 ms; the frame within noise.

## Milestone 34 — rainbows (2026-11-03)

- `FellowRain3D.rainbow` (decision 0134): while it rains or lately rained (fading over 90 s once it stops, none once it
  snows) and the sun shines on the rain, the primary and secondary bows around the point opposite the sun — from each
  wavelength's refraction in water through the CIE matching functions, the sky brighter inside the primary and darker
  between the two — over the sky and over what stands behind the rain, dimmed by the clouds' shadow at the camera.
- Off is the previous image (lavapipe). Cost on the RTX (Bistro raining, `m34_f305`): the pass 0.045 ms, the frame
  +0.07 ms. FellowBench: `--weather_rainbow`. Showcase: a double rainbow over Bistro as the rain clears.

## Milestone 33 — the clouds' shadows and the sky's light under them (2026-11-03)

- `post/clouds/shadows` (decision 0133, on with the clouds): a map of the sun's transmittance through the cloud layer,
  512² over 16 km around the camera (the clouds' own density, eroded edges included), multiplied into the sun's shadow
  in the colour passes — moving shadows on a cloudy day, sunny gaps under an overcast; a FellowWeather3D's `sunlight`
  factor steps aside while they are on.
- `post/clouds/sky_light`: where the clouds cover the sky, its ambient light and reflections turn grey (no clear-sky
  blue on a wet street under rain clouds).
- Fixed before release: with ray-traced GI on, reading the map written earlier in the same frame lost the device on the
  RTX; the map is double-buffered (read a frame later).
- Off is the previous image (lavapipe: Rain Lab without drops, Water Lab, Rain Lab under clouds with both off, bit for
  bit). Cost on the RTX (Bistro under clouds, `m33_f305`): the map 0.12 ms, the frame +0.17 ms.
- `Light3D::get_light_rid()` serves the sun's identification; `light_storage.cpp` skips the weather's sun factor
  under the clouds' shadows.

## Milestone 32 — a weather controller (2026-11-03)

- Added `FellowWeather3D` (decision 0132): the weather from one node — presets (clear, cloudy, overcast, rain, storm,
  snow, blizzard) or a custom state of wind, rain or snow, lightning, cloud coverage and density, sunlight, sky light,
  fog and temperature — changed smoothly with `set_weather(weather, seconds)` (rain and snow hand over at the
  transition's midpoint). It drives the scene's `FellowWind3D`, `FellowRain3D` and `FellowLightning3D` (creating the
  missing ones), the volumetric clouds through a render-side override, the sun on the scene's surfaces only (the sky
  and the clouds keep the full sun), the environment's fog and background energy, and the ground: wet in the rain,
  drying after it, covered by falling snow below freezing, melting above it. Nothing in the editor.
- `Light3D::get_light_rid()` (C++): a light's RenderingServer RID (`VisualInstance3D::get_base()` is not set for lights).
- Off is the previous image (lavapipe: Rain Lab without drops, Water Lab bit for bit). Cost on the RTX (Bistro
  exterior, `m32_f297`): the controller not measurable, its weathers the cost of their parts (rain +0.5 ms).
- FellowBench: `--weather`, `--weather_cycle`, `--weather_times`. Showcase: Bistro under each weather, a time-lapse.
- The decision index lists records 0127-0132.

## Milestone 31 — lightning lights the clouds (2026-11-02)

- `FellowLightning3D.cloud_glow` (decision 0131): while a flash lasts, the volumetric clouds glow around it — by how
  near the view passes to the flash and how opaque they are — added in the clouds' composite at full resolution every
  frame (their traces, accumulated over frames, would smear a flash of a few frames). Without a flash the clouds are
  bit for bit milestone 30's (lavapipe). Showcase: the thunderstorm in Rain Lab under a cloud deck.
- Snow tracks: a new `track_window` starts the tracks afresh; a one-step parallax for them was tried and reverted.
- The defaults are not slower (`m30_f294_final`, stock / fellow212 / fellow220, 3 runs): fellow220 faster than stock in
  every scene (Sponza −54.2 %, Bistro exterior −33.4 %, City Lab −25.8 %, Crowd Lab −24.0 %, Bistro interior −23.6 %,
  Lights Lab −23.4 %, Foliage Lab −17.8 %, PBR Lab −16.4 %; Stress Lab −2.7 %, within its spread this time). Against
  milestone 26's build within the spread (`m30_f296_order`, the order reversed).

## Milestone 30 — snow tracks (2026-11-02)

- Added `FellowSnowTracker3D` and `FellowRain3D.stamp()` (decision 0130): feet, wheels and runners press tracks into
  the snow while they touch the ground (the shelter heightfield, eroded); the cover shows their walls, packed snow at
  their bottom, a rim beside them and the ground where they cut through; the falling snow fills them back. The
  tracks live in a world-anchored, wrapping R16F window around the camera (48 m, 2048², `tracks`, `track_window`,
  `track_resolution`, `snow_depth`, `track_refill_time`), updated only where something changed.
- Cost on the RTX (Rain Lab snowing, a cart's wheels pressing every frame, `m30_f292`): not measurable. Rain Lab
  without drops and Water Lab bit for bit on lavapipe.
- FellowBench: `--weather_snow_footprints`, `--weather_snow_ruts`, `--weather_snow_depth`; Rain Lab `--rain_tracks=1`.

## Milestone 29 — lightning (2026-11-02)

- Added `FellowLightning3D` (decision 0129): strikes at random around the camera (`strikes_per_minute`,
  `distance_range`) or on `strike()`; two to four return strokes, each a flash decaying over about 40 ms — a
  directional light from the bolt's direction with shadows (no sun disc, not taken for the sun by `FellowWater3D`), a
  branching bolt of a constant width on screen, a lit cloud. The same clock gives the same storm; `struck(position,
  distance)` for the thunder.
- Cost on the RTX (Rain Lab at dusk, `m29_f290`): +0.04 ms of GPU time on average with a strike every 2 s; nothing
  between strikes.
- FellowBench: `--weather_lightning`, `--weather_lightning_flash` (captures), `--weather_lightning_frames`
  (sequences). Showcase: a thunderstorm in Rain Lab.

## Milestone 28 — snow (2026-11-02)

- `FellowRain3D.precipitation` = Snow (decision 0128): flakes drift down in a box around the camera, take the
  scene's wind where they are (gusts included, the particles' Wind group), swing about their paths, stop on the
  shelter heightfield and receive shadows; drawn in the opaque pass with hashed alpha so that TAA reprojects them
  (blended, TAA wiped them out).
- `FellowRain3D.snow`: a snow cover on what faces up where the snow falls — patchy with ragged edges while thin,
  everything facing up once deep, nothing on slopes over 60°, bare under roofs and canopies (the shelter eroded by
  35 cm, so wires and lamps overhead leave no bare spots); smooth with gentle undulations, glinting
  (`snow_sparkle`); `snow_follows_precipitation` grows it in `snowing_time` and melts it in `melting_time`.
- The rain unchanged: Rain Lab without drops and Water Lab bit for bit on lavapipe. Cost on the RTX (Rain Lab,
  `m28_f289`): snowing on a covered street +0.37 ms of GPU time (raining +0.34).
- FellowBench: `--weather_snow` in any scene, Rain Lab `--rain=snow`. Fire Lab's embers are round.
- Class reference for the new members and for milestone 27's water and particle wind members.

## Milestone 27 — the scene's wind for water, gas and particles (2026-11-01/02)

- `FellowWater3D.wind_from_scene` (decision 0127): the spectrum follows the active `FellowWind3D`'s speed and
  direction, and its gusts roughen the shortest waves — darker, rougher water in a gust, a mirror in a lull, the
  "cat's paws" running over a lake (`gust_ripples` scales them).
- `FellowGasVolume3D.wind_from_scene`: the gas's wind each step is the scene's wind at the box's centre, gusts
  included; flames and smoke lean and waver with the gusts.
- `ParticleProcessMaterial` gets a Wind group (`wind_enabled`, `wind_influence`): the particles' horizontal velocity
  relaxes towards the wind at their position, gusts included. Particle shaders read it as `WIND_VELOCITY` (zero
  without a wind and on the Compatibility renderer).
- Off is the previous image: Water Lab and Fire Lab without a wind bit for bit on lavapipe. Cost on the RTX
  (`m27_f287`, `m28_f288`): the water's gust ripples and the gas's wind not measurable, Particles Lab's update +0.04
  to +0.15 ms, its frame +0.09 ms.
- Fixed: Water Lab with contact shadows crashed — the depth pre-pass bound per-render masks that did not exist yet
  (`84f3a8e`).
- FellowBench: `--weather_wind=<m/s>` in any scene (Water, Fire and Particles Lab hand it on), `--wind_leaves=<n>`
  falling leaves in Wind Lab.

## Milestone 26 — wind in the forest (2026-10-29/30)

- Added `FellowWind3D` (module `fellow_fluid`, decision 0125): the scene's wind with gusts, a noise field travelling
  with it that rolls over a meadow in waves. Each plant bends with the wind's force towards its top, sways at a
  frequency of its size, its branches at their own phases, its leaves flutter; normals turn with the bend and bent
  grass brightens (a sheen). `BaseMaterial3D` gets a Wind group (`wind_height`, `wind_bend`, `wind_flutter`,
  `wind_sheen`); a `ShaderMaterial` uses the render mode `fellow_wind` and the vertex built-ins `WIND_HEIGHT`,
  `WIND_BEND`, `WIND_FLUTTER`, `WIND_SHEEN`. Shadows and motion vectors follow (TAA and motion blur see it);
  `FellowRain3D`'s drops follow the same wind.
- New bench scene Wind Lab (`--scene=wind`: about 40 k grass clumps, 700 broadleaf trees, 500 bushes). Without a
  `FellowWind3D` and without wind materials, City Lab, Water Lab, Rain Lab `--rain=off` and Foliage Lab with its
  TIME-based sway render bit for bit the images of `d8f2909`.
- The wind's cost is in the shadows. MultiMesh culling now uses the wind's bound on the displacement, and the far
  cascades (from the third, `shadows/wind_cached_from_cascade` = 2) are cached with the plants' pose of their last
  build: lavapipe Wind Lab −41 to −63 % GPU time against the first build, 32-58 % less than the TIME-based sway
  projects write. RTX (`m26_f275_wind`): still 3.566 ms of GPU time, `FellowWind3D` 4.122 ms (+0.56: the near shadow
  cascades +0.33, the opaque pass +0.23, the pre-pass +0.06), the TIME-based sway 5.372 ms (+1.81); the wind with
  gusts is 1.25 ms (23 %) cheaper than what projects write.
- Optimizations of the features of milestones 24-26 (2026-10-29, lavapipe):
  - rain: the ripples' compute works out the 75 cells' drops once per group in shared memory instead of 27 times per
    texel (exact): 14.1 → 9.4 ms on lavapipe (−33 %; on the RTX it is 0.04 ms) (0124);
  - water: reflections traced over a Hi-Z pyramid of the depth texture (`postfx/depth_copy_nearest_mipmaps`, on,
    0126): on the RTX the reflections 0.99 → 0.61 ms (−38 %), Water Lab's GPU time 3.128 → 2.762 ms (−12 %; shore
    −29 %, pier −24 %, glint −36 % of the water's pass, aerial +26 %); the far shore reflected at its true parallax,
    without jitter;
  - tried and not kept: a Fresnel-scaled step count for the reflection march (+2-9 %, the spread; reverted).
- On the RTX, the night of 2026-10-29/30 (rows 5-14 of the report's table):
  - row 5, rain, the ripples' cells shared: the ripples' compute −0.025 ms; Rain Lab's frame unchanged (the basin's
    depth pyramid +0.047 ms); kept;
  - row 6, water, the rays towards the camera bounded by `reflection_distance`: aerial −0.01 ms, the rest within the
    spread (`m26_f276`); not kept;
  - row 7, water, the depth pyramid asked for only while reflections are traced (`560d36c`): without reflections the
    depth copy 0.079 → 0.026 ms (`m26_f278`); exact; kept;
  - row 8, wind, the shadows cached from the second cascade: −0.11 ms more than from the third, but only the nearest
    cascade's shadows sway; the third stays;
  - row 9, depth texture, the single-level copy skipped where the pyramid replaces it (`e99e5db`): the depth copy
    −0.023 ms in Water Lab and Rain Lab (`m26_f280`); exact (lavapipe: Water Lab bit for bit); kept;
  - row 10, water, where the shading without reflections goes (`m26_f281`): of the 0.46 ms, the cascades' lookups
    0.21 ms (their anisotropic filtering 0.10), the foam's noise 0.013, the caustics 0.02, the refraction 0.01; the
    rest the lighting;
  - row 11, the labs the final run does not cover, before / after milestone 26 (`m26_f283`, fellow207 / fellow211):
    frame time Fire Lab 2.914 / 2.950 ms, Burn Lab 3.669 / 3.628, Fluid Lab 2.315 / 2.311, Particles Lab
    2.946 / 2.921, Blend Lab 2.008 / 2.033, within the runs' spread;
  - row 12, burning (milestone 22), where its drawing goes (`m26_f284`): +0.84 ms of GPU time against the original
    meshes (the opaque pass +0.43, the depth pre-pass +0.28, the shadows +0.16); the crack cells 0.19 ms of it, the
    vertices' march into the voxels 0.37 ms (it ran in every pass drawing the mesh);
  - row 13, burning, the crack cells skip the neighbours that cannot be nearer (exact): no gain (the pre-pass
    0.358 ms with and without, the opaque pass +0.02 ms, `m26_f285`); reverted;
  - row 14, burning, the vertices march into the voxels once per frame with steps, in a compute pass, the materials
    reading them by `VERTEX_ID` (`fluid/burn_vertices_cached`, on, 0120): Burn Lab's GPU time 2.634 → 2.139 ms
    (−19 %; the frame −13 %): the shadows −0.14 ms (−65 %), the opaque pass −0.27, the pre-pass −0.13, the compute
    pass +0.03; at 48 voxels per object −24 % (`m26_f285`); as shipped (fellow213) 2.638 → 2.134 ms, Fire Lab
    unchanged (`m26_f286`); the images alike (at most 151 pixels per 1080p view differ, PSNR ≥ 81 dB).
- Final check against stock (nine scenes, three runs each after a pass filling the shader caches; frame time):
  `m26_f279_final` (fellow210) faster in every scene and every pair of runs — Sponza −52.8 %, Bistro exterior
  −29.6 %, City Lab −27.4 %, Bistro interior −24.1 %, Crowd Lab −23.0 %, Lights Lab −22.9 %, Foliage Lab −18.0 %,
  PBR Lab −14.7 %, Stress Lab −10.3 %. Again at the end of the night with fellow212 (`m26_f285_final`: the copy
  skipped, the burning's vertices marched once per frame), faster in every scene and every pair of runs — Sponza
  −54.6 %, Bistro exterior −30.8 %, City Lab −25.1 %, Bistro interior −23.5 %, Lights Lab −22.2 %, Crowd Lab
  −21.8 %, Foliage Lab −16.7 %, PBR Lab −16.0 %, Stress Lab −11.5 %.

Details: [report](docs/fellow_milestone26_report.md)

## Milestone 25 — rain and wet surfaces (2026-10-29)

- Added `FellowRain3D` (module `fellow_fluid`, decision 0124). Drops around the camera: stock GPU particles drawn by a
  shader of their own as lit streaks along their velocity (leaning with the wind), colliding with the scene seen from
  above and splashing where they land. Wet materials: every opaque Forward+ material wetted after its own code where
  the rain reaches it — porous surfaces darker, all with a glossy film, puddles on flat ground with the rain's ripples
  (a tiling texture computed on the GPU every frame) — in the depth pre-pass too, so screen-space reflections show the
  street in them. Shelter: a heightfield of the scene from above, following the camera, keeps the ground under roofs
  and trees dry and the drops off it. A `FellowWater3D` shows the ripples too.
- The renderer keeps a weather state on the render thread (`RendererRD::FellowWeather`) and a specialization bit
  (`fellow_wetness`), so nothing changes without rain: City Lab and Rain Lab `--rain=off` bit for bit the images of
  `5e26af0` (with the final build Rain Lab `--rain=off` has 10 pixels one level off in the basin: float rounding in
  the water's march).
- New bench scene Rain Lab (`--scene=rain`, `--rain=on|dry|off`, `--rain_light=day|dusk`): a town street, a cobbled
  square with a fountain basin, an arcade whose floor must stay dry, a bus shelter, a parked car, street lamps.
- Cost on the RTX (Rain Lab flythrough, 3 runs each): 0.36 ms of GPU time, after three rounds of optimization from
  0.49 ms (frame: no rain 1.889 ms, the first version 2.398 ms, the last 2.243 ms); a `FellowRain3D` without rain
  0.04 ms (the spread). The wetting's five `textureGrad` lookups became lookups at explicit mip levels (the wetting
  0.175 → 0.116 ms). The rest of the 0.36 ms: SSR on the wet surfaces 0.13 ms, the particles 0.04 ms, the ripples'
  compute 0.04 ms, the drops' drawing 0.03 ms.
- Water Lab: a reflection march with the clip position linear along the ray made no measurable difference; 16
  reflection steps instead of 32 save 0.36 ms (frame 2.974 ms against 3.310).

Details: [report](docs/fellow_milestone25_report.md)

## Milestone 24 — water: lakes and rivers (2026-10-28)

- Added `FellowWater3D` (module `fellow_fluid`, decision 0123), a body of water to the horizon: wind waves simulated
  on the GPU after Tessendorf (JONSWAP for the wind and fetch, FFT in three cascades of 160 / 27 / 5 m), matching
  NumPy to half-float precision (worst relative error 9 x 10⁻⁴); the river's simulation (`FellowShallowWater3D`) drawn
  as part of the lake, its waves carried by the flow; reflections traced in screen space with the sky where nothing is
  hit; refraction, absorption and in-scattering, foam at shores and objects, caustics from the waves' own slopes, the
  sun's glitter path.
- Exact: the screen texture's copy builds its mip chain only when a material drawn this frame samples its mips
  (`postfx/screen_copy_mipmaps_on_demand`): the copy 0.031 ms (0.064 with its mips always); Water Lab and Fluid Lab
  bit for bit with the option on and off.
- New bench scene Water Lab (`--scene=water`; `--water=basic` is what a Godot project would use). Off, Fluid Lab
  renders bit for bit the images of `6f091cd`.
- Cost on the RTX (Water Lab flythrough, frame / the water's pass): no water 1.615 ms; Godot's water
  1.999 / 0.166 ms; `FellowWater3D` 3.482 / 1.591 ms (`m24_f269`); with the cheaper reflection march
  3.264 / 1.389 ms; without reflections 2.372 / 0.463 ms (`m24_f270`). The waves' compute 0.10 ms, the simulation
  0.14 ms, the caustics 0.03 ms. The reflections are the cost: 1.13 ms, and 0.93 ms after the Fresnel cutoff, steps by
  screen length and jitter (−18 %, 45-57 dB against the full march). In all, 1.6 ms of the frame (Godot's water
  0.38 ms), more than half of it the reflections (milestone 26 made them cheaper).

Details: [report](docs/fellow_milestone24_report.md)

## Milestone 23 — far objects: contribution culling, automatic HLOD (2026-10-28)

- Contribution culling (decision 0121, off by default): the camera skips a mesh or MultiMesh node — and, with
  MultiMesh culling, each MultiMesh instance on the GPU — whose bounding sphere would cover fewer than N pixels of the
  viewport's width (`geometry/contribution_cull_pixels`); a directional cascade skips casters spanning fewer than N of
  its texels (`shadows/contribution_cull_texels`). At about 4 px of 1080p: 300-1 400 nodes per view not drawn, 7-13 %
  fewer primitives, the render thread 1-7 % shorter; 62.9-78.4 dB against the full image. Small MultiMesh instances
  are thinned, not culled (`geometry/contribution_cull_instances`): culled, Foliage Lab's meadow turned 3.7 levels
  lighter; thinned, its mean colour stays within 0.05 levels.
- Added `FellowHLOD3D` (module `fellow_world`, decision 0122): the static meshes below it are replaced beyond a
  distance by proxies. Repeated meshes become instanced proxies (one MultiMesh per kind, culled instance by instance
  on the GPU, drawn from the distance on): the same image bit for bit on lavapipe, the render thread 23-42 % shorter,
  the GPU unchanged; City Lab's 18 800 nodes in 14 proxies, built in 44 ms. Unique meshes are merged per cell and
  material into meshes with generated LODs. Shadows are decided per cascade, camera-independent, so the directional
  shadow cache keeps them.
- City Lab flythrough on the RTX (`m23_f267`): stock 2.705 ms frame (render thread 2.066 ms, GPU 1.789 ms);
  fellow200 2.053 ms; with contribution culling at 4 px 2.020 ms (−1.6 %); with `FellowHLOD3D` at 100 m 1.813 ms
  (−11.7 %), render thread 1.172 ms (−26.4 %), GPU 1.746 ms. Foliage Lab with contribution culling, after the
  compaction grows only the instances the cull kept (`m23_f268`): aerial −10.5 %, forest −1.2 %, meadow −2.4 %.
- Impostors (`FellowHLOD3D.impostor_distance`, an option): far instances of heavy meshes drawn as octahedral
  impostors baked at run time (8 x 8 views of 128 px). They do not pay for Foliage Lab's 420-triangle trees
  (`m23_f268_foliage`): flythrough 4.805 → 4.671 ms (−2.8 %), but aerial +5.1 % and forest +6.2 %; images 27.4 / 49.0
  / 32.0 dB. `impostor_min_triangles` defaults to 2 000, so light meshes stay meshes.
- Tried and not kept: HLOD merging every mesh per cell (18 814 meshes into 196 proxies: a third of the triangles, but
  1 500 draws instead of 200 and the render thread no faster — merging repeated meshes undoes Godot's instancing).

Details: [report](docs/fellow_milestone23_report.md)

## Milestone 22 — burning objects: char, embers, ash (2026-10-26/28)

- Added `FellowBurnable3D` (module `fellow_fluid`, decision 0120): a `MeshInstance3D` burns like wood in a gas
  volume's fire. Per object a GPU-simulated box of voxels (temperature, wood, char, ash): conduction, the heat of the
  gas's flame or the surface's own flame, pyrolysis (wood → char; the volatiles are the gas's fuel, so the flames come
  from the object), char oxidation (embers → ash), the remains falling to the ground and sliding into heaps. Coupled
  both ways with `FellowGasVolume3D`.
- Drawn on the object's own material (BaseMaterial3D or ShaderMaterial, extended): the mesh subdivided, its vertices
  receding with the wood and char, char and ash colours, embers glowing in cracks; burned-through parts not drawn; the
  ash and crumbled charcoal drawn as a heap of its own (`FellowAshHeap`). Below `collapse_fraction` of wood and char
  the physics body stops colliding (signal `collapsed`).
- Verification: the GPU matches a NumPy reference (`burn_reference.py`): `--case=solo` 1.1 × 10⁻⁵, `--case=ash`
  2.2 × 10⁻⁵, `--case=gas` 9.1 × 10⁻⁷ (relative L2); Fire Lab and the gas checks unchanged bit for bit.
- New bench scene Burn Lab (`--scene=burn`): two logs and a branch over a kindling fire, a crate lit at a corner. The
  branch burns away by 15 s, the logs by 35-40 s, the crate's planks by about a minute; the logs end as 0 % wood,
  13-25 % char, 9-11 % ash of their volume at 70 s. The look was reworked on 2026-10-27 (standing, licking flames;
  alligator char; beds of grey ash with pieces of charcoal).
- Cost on the RTX (`m22_f267_burn_cost`, fellow200): frame 3.417 ms burning drawn and simulated, 2.594 ms simulated
  with the original meshes drawn, 2.387 ms with neither. The simulation of four objects costs about 0.2 ms of the frame
  (0.03 ms of GPU time); drawing them burning 0.8 ms (milestone 26 cut that with `fluid/burn_vertices_cached`).

Details: [report](docs/fellow_milestone22_report.md)

## Milestone 21 — gas: where the time goes, sparse bricks (2026-10-26)

- Sparse bricks (decision 0119, on by default: `FellowGasVolume3D.sparse`, `sparse_margin`; project setting
  `fluid/gas_sparse`): only the 4³ bricks within two bricks of smoke, heat, fuel or an emitter are simulated. Fire
  Lab's simulation per step on the RTX: `--gas_res=64` 1.77 → 1.58 ms (−11 %), frame 3.38 → 3.10 ms (−8.2 %);
  `--gas_res=128` 6.43 → 3.16 ms (−51 %), frame 8.52 → 5.04 ms (−41 %).
- Verification: the GPU matches the NumPy reference, which models the bricks, to ≤ 3.1 × 10⁻⁶ after 30-60 steps; two
  runs bit for bit; dense volumes are milestone 20's bit for bit; City Lab 0 differing pixels.
- Profile (`fluid/gas_profile_passes`): the pressure solve is 65-77 % of a step. At 128 cells, dense → sparse:
  emission group 0.16 → 0.34 ms, advection 0.84 → 0.25, forces 0.54 → 0.17, pressure 4.49 → 2.62, projection and
  bricks 0.72 → 0.29.
- From the RTX profiles: after a world-space box's shift the bricks outside the simulated ones are cleared one
  invocation per texel, not per brick (the emission group 0.58 → 0.34 ms at 128 cells).
- Tried and not kept: the multigrid's small levels in one workgroup (`fluid/gas_mg_tail_cells`): the same state bit
  for bit, slower at every threshold but 2048 cells at 64 (−2.0 %).

Details: [report](docs/fellow_milestone21_report.md)

## Milestone 20 — smoke and fire (2026-10-26)

- Added `FellowGasVolume3D` (module `fellow_fluid`, decision 0117): a box of air simulated on the GPU, one step per
  physics tick — a staggered grid, MacCormack advection, combustion, buoyancy, vorticity confinement, turbulence, wind,
  a multigrid pressure projection; the physics bodies inside it are moving obstacles. Its `FellowGasEmitter3D`
  children emit smoke, fuel and heat.
- Drawn by Forward+ (decision 0118): a half-resolution ray march with the smoke's self-shadow, the sky's light, the
  fire's blackbody emission (its colour from the temperature × 1.4, `fire_temperature_scale`), the smoke's shadow on
  the scene; an omni light at the fire placed and coloured from the simulated emission; the velocity as a texture for
  GPU particles (embers).
- Verification: the GPU's state equals a NumPy port of the solver to float rounding (≤ 2.5 × 10⁻⁶ after 30-60 steps,
  except the campfire's fuel after 60 steps, 9.6 × 10⁻⁵); two runs of Fire Lab render the same frames bit for bit;
  without a gas volume every frame is milestone 19's bit for bit.
- New bench scene Fire Lab (`--scene=fire`: a campfire, a smoking chimney, a sphere through the smoke, a carried
  torch). Cost on the RTX (`m20_f261`): at `--gas_res=64` (0.47 M cells) the simulation 1.69 ms of GPU per step, the
  drawing 0.31 ms, the frame 3.25 ms (1.76 without the simulation); at 128 (3.24 M cells) 5.62 and 0.45 ms, the frame
  7.52 ms.
- Final measurement, stock against fellow193 (`m20_f260_final`, nine scenes, three runs each): faster in every scene
  and every pair of runs — Sponza 13.82 → 6.40 ms (−53.7 %), Bistro exterior 6.33 → 4.44 (−29.8 %), City Lab
  2.88 → 2.10 (−27.0 %), Bistro interior 4.31 → 3.33 (−22.7 %), Lights Lab 3.05 → 2.36 (−22.7 %), Crowd Lab
  5.63 → 4.49 (−20.2 %), Foliage Lab 3.41 → 2.86 (−16.2 %), PBR Lab 2.47 → 2.09 (−15.5 %), Stress Lab 4.82 → 4.43
  (−8.2 %).

Details: [report](docs/fellow_milestone20_report.md)

## Milestone 19 — more of the look, and its cost (2026-10-25)

- Four visual features after Unreal's, off by default (off, the image is the previous build's bit for bit):
  `post/bloom/enabled`, an energy-conserving bloom of the whole image (0112); `post/bloom/lens_flare`, ghosts and a
  halo of the bright sources (0113); `post/white_balance/temperature` and `tint`, a camera's white balance (Bradford
  adaptation to D65) (0114); `post/light_shafts/intensity`, rays from the sun broken by roofs and clouds (0116).
- Three optimizations of milestone 18's features (`m19_f258`): clouds traced a quarter of the pixels per frame
  (`post/clouds/interleaved`, 0115), City Lab's clouds 0.505 → 0.325 ms; the atmosphere's sky no longer redraws
  Godot's radiance every frame, "Setup Sky" 0.25 → 0.002 ms in every scene (0108); contact shadows in 8 steps, City
  Lab 0.159 → 0.104 ms, Sponza 0.282 → 0.179 ms (0107). With every feature on, about 0.5 ms saved per frame in City
  Lab and 0.37 ms in Sponza.
- GPU cost per feature (`m19_f258`, City Lab with an 8° sun / Sponza; none: 2.03 / 6.44 ms): bloom +0.14 (City Lab);
  bloom and lens flare +0.15 / +0.13; light shafts +0.06 (City Lab, where the sun is on the screen); white balance
  −0.002 (n.s.); interleaved clouds +0.39 / +0.15; contact shadows in 8 steps +0.12 / +0.17.

Details: [report](docs/fellow_milestone19_report.md)

## Milestone 18 — the look (2026-10-25)

- Seven visual features after Unreal Engine 5's, each under `rendering/fellow/post/*`, off by default, and off the
  image is the previous build's bit for bit: `post/exposure/histogram` (histogram eye adaptation, 0105),
  `post/local_exposure/enabled` (0106), `post/contact_shadows/enabled` (0107), `post/atmosphere/enabled` (Hillaire's
  sky atmosphere and aerial perspective, 0108), `post/clouds/enabled` (volumetric cumulus, 0109),
  `post/sharpen/strength` (RCAS after TAA, 0110), `post/bloom/lens_dirt` (0111).
- GPU cost, each against none (`m18_f255`/`m18_f256`, City Lab / Sponza / Bistro exterior; none 2.18 / 6.40 /
  4.32 ms): exposure (histogram and local) +0.01 (n.s.) / +0.03 (n.s.) / +0.03; contact shadows +0.22 / +0.34 /
  +0.29; atmosphere +0.11 / +0.10 / +0.07; clouds +0.63 / +0.62 / +0.52; all seven +0.88 (+40 %) / +1.01 (+16 %) /
  +0.86 (+20 %). The histogram's exposure pass is cheaper than stock's reduction (0.054 against 0.062 ms in City Lab).
- Second round (`m18_f257`): clouds traced only where the sky is seen (Sponza +0.19 ms, the pass 0.107 ms, was 0.609;
  Bistro exterior +0.22, the pass 0.151 ms, was 0.496); contact shadows marched in clip space (0-10 % cheaper); the
  sky's Rayleigh coefficients integrated over the sRGB channels (`srgb_rayleigh`, the default), matching a spectral
  sky within 1-2 % where the three-wavelength coefficients leave the red 15-20 % low.

Details: [report](docs/fellow_milestone18_report.md)

## Milestone 17 — physics and collisions (2026-10-24/25)

- Both of Godot's 3D physics engines are faster, and the exact fork's simulation is stock's tick by tick: in every
  case of the new Physics Lab, with both engines, the physics state hashed every 30 ticks equals stock's (also with 1
  to 24 workers). New tools: Physics Lab (five cases), the bench's `--mode=physics`, `benchmarks/physics_bench.py`.
- GodotPhysics3D, exact, on: motion queries skip the trimesh faces a separating-axis test proves out of reach
  (`physics/distance_face_cull`, 0085; with the shape itself and the faces' edge axes, 0096; culled once per query,
  0097; not tested again within a sweep, 0100); a convex polygon's vertices transformed once per SAT test (0086); the
  broadphase's culls on worker threads (0087); a large island solved on every thread in stock's per-body order (0089:
  `pile`'s solve 9.9 → 3.9 ms); islands from contiguous constraint lists (0091); a repeated motion test answered from
  the first within one `move_and_slide()` (0093); rays skip tree nodes beyond the closest hit (0098); vectorised and
  inline segment-box tests (0094, 0099); `Vector3::is_finite()` inline (0103). `physics/verify` checked 50-66 million
  (0085) and 36 million (0096, 0100) skipped faces against GJK: none touching.
- Jolt, exact, on: Area3D contacts recorded per thread instead of under one mutex (0088); the jobs per phase by the
  work, one per 128 active bodies and at least 4 (`physics/jolt_max_jobs`, 0090).
- Laptop, headless, 600 ticks, ms per tick, stock → exact fork (`m17_f254`): GodotPhysics3D `pile` 28.11 → 12.36
  (× 2.27), `stack` 1.37 → 1.13 (−17 %), `chars` 27.39 → 19.95 (−27 %), `rays` 74.08 → 8.65 (× 8.6), `areas`
  4.00 → 2.61 (−35 %); Jolt `pile` 3.52 → 3.50 (−1 %), `stack` 0.77 → 0.65 (−16 %), `chars` 5.77 → 5.78 (=), `rays`
  10.52 → 10.12 (−4 %), `areas` 3.78 → 2.20 (−42 %).
- Fast mode (off by default, `physics/fast/*`; deterministic, not stock's results bit for bit):
  `physics/fast/jolt_shape_cast` (0095) and `physics/fast/godot_toi` (0102). GodotPhysics3D `chars` 12.69 ms (× 2.2),
  `rays` 8.02 ms (× 9.2); Jolt `chars` 3.87 ms (−33 %), `rays` 2.38 ms (× 4.4); Jolt's shape casts within 2.5 mm on
  25 m of stock's.
- Correction kept visible: the first results (`m17_f252`) for `chars` and `rays` were measured on a Physics Lab
  terrain wound facing down (fixed in `2e1cd05`); the numbers above replace them.
- Tried and not kept: Jolt's jobs through a lock-free queue and pump tasks (0092, no consistent gain); a capsule's SAT
  without the faces proven out of reach (0101, exact, no gain); `test_body_motion()`'s broadphase culls from one
  collected set (0104, exact, −1.5 %).

Details: [report](docs/fellow_milestone17_report.md)

## Milestone 16 — a better image: post-processing and ray tracing (2026-10-24)

- Everything new is off by default (with the defaults the image is the previous milestone's bit for bit). Motion blur
  after TAA, a reconstruction filter over the velocity buffer (`post/motion_blur/enabled`, 0078): City
  Lab GPU +0.35 ms, Sponza +0.39 ms. Lens effects folded into the tone mapping pass (`post/lens/chromatic_aberration`,
  `post/lens/vignette`, `post/lens/film_grain`, 0079): within noise.
- Ray tracing (`raytracing/enabled`, 0080): ray queries enabled in the Vulkan driver (stock never turned them on), a
  bottom-level structure per mesh surface, the frame's top-level structure from the mesh and MultiMesh instances
  within 200 m of the camera. Building the top-level structure for City Lab's 7 600 instances took 5.7 ms of the
  render thread with stock's driver code; built in local memory and copied whole, 0.6 ms.
- Four effects on it: ray-traced ambient occlusion in place of SSAO (`raytracing/ambient_occlusion`, 0081), ray-traced
  sun shadows (`raytracing/sun_shadows`, 0082), DDGI from two cascades of ray-traced probes (`raytracing/gi`, 0083;
  probe relocation, `gi_sky_leak` 0.1), ray-traced reflections in place of SSR (`raytracing/reflections`, 0084); a
  path-traced reference (`debug/raytracing_gi_reference`).
- GPU cost against ray tracing on with no effect (`m16_f246_rt_ab`, City Lab / Sponza / Bistro exterior): RTAO
  −4 % / +5 % / +17 %; sun shadows +4 % / +7 % / +14 %; DDGI +57 % / +22 % / +44 %; reflections +8 % / +13 % / +5 %;
  all four +66 % (2.15 → 3.55 ms) / +53 % (6.33 → 9.72 ms) / +59 % (4.07 → 6.45 ms). The render thread pays about
  1 ms for the scene in City Lab and Bistro exterior.
- Tried and not kept: reflections traced by a thread per 2 × 2 block (Sponza 2.04 ms against 1.57 ms).

Details: [report](docs/fellow_milestone16_report.md)

## Milestone 15 — frame pacing, skeletal animation (2026-10-22)

- Closed without its final measurement: the final run (`m15_f229`) was stopped after its first scenes when the job
  queue was cleared at the user's request on 2026-10-22; the numbers below are the milestone's own measurements.
- Six worker tasks instead of ten (`cpu/worker_tasks`, automatic: half the logical processors less two, 1-6; decision
  0073): City Lab's presentation stalls gone, 1 % low 117-281 → 246-289 fps; no scene's mean frame moves.
- New bench scene Crowd Lab (256 skinned characters with 52 bones, an AnimationPlayer and a Skeleton3D each): its
  frame is the main thread animating them, in stock's code.
- Skeletal animation without work whose result is known (0076; `animation/elide_modifier_backup`,
  `animation/full_weight_slerp`, `animation/track_shortcuts`, `animation/key_hints`): the same bone poses bit for bit;
  Crowd Lab between processes stock 5.25 → 4.60 ms (−12.3 %), in one process −19.6 %, and the key hints −4.9 % more.
- A skin's bone transforms handed to the RenderingServer in one call (`animation/batched_skin_transforms`, 0077): with
  the separate render thread Crowd Lab −25.2 % (4.63 → 3.47 ms), now faster than the default thread model (4.34 ms).
- Rejected, exact but without gain: SSR's resolve with its samples shared (`postfx/ssr_resolve_shared`, 0074); TAA's
  tile loaded by every invocation (`postfx/taa_strided_load`, 0075).

Details: [report](docs/fellow_milestone15_report.md)

## Milestone 14 — the same frames on every run (2026-10-22)

- With the defaults and autotune off, two runs at a fixed frame rate render the same frames bit for bit in every
  scene the run-to-run floors are measured in (Bistro interior, PBR Lab, Sponza, Lights Lab), where stock's two runs
  differ at 69-98 dB; also with the separate render thread.
- Textures a pass can write, created without data, cleared before their first use (`frame/clear_new_textures`,
  0070): no measurable cost. The positional shadow atlas's reallocation tolerance counted in simulated time at a fixed
  frame rate (`shadows/atlas_simulated_time`, 0071). Movie Maker mode: a 3D draw waits for its specialized pipeline
  (`frame/movie_wait_pipelines`, 0072): 0 draws on the ubershader (34 without).
- Final (`m14_f213_final`, stock → fellow162): Sponza 13.97 → 6.66 ms (−52.3 %), Bistro exterior 7.06 → 4.52
  (−36.0 %), City Lab 3.35 → 2.24 (−33.3 %), Lights Lab 3.28 → 2.48 (−24.3 %), Bistro interior 4.46 → 3.49
  (−21.8 %), PBR Lab 2.80 → 2.29 (−18.0 %), Foliage Lab 3.90 → 3.20 (−17.9 %), Stress Lab 5.29 → 5.00 (−5.4 %);
  Foliage Lab with one MultiMesh per type 11.31 → 5.09 ms (−55.1 %). Load to the first frame −21 % (Bistro exterior)
  to −60 % (Lights Lab).
- Godot's separate render thread measured with FellowEngine (`m14_f204`): Stress Lab −6.7 %, City Lab −4.5 % (n.s.),
  Bistro exterior −1.6 %; recorded in the options doc as a recommendation for projects whose main thread works.

Details: [report](docs/fellow_milestone14_report.md)

## Milestone 13 — frame pacing, LOD of large surfaces, run-to-run floors (2026-10-20)

- Hot workers linger 30 µs instead of 300 µs (`cpu/hot_workers_usec`, 0065): City Lab's frames of 16-52 ms are gone,
  1 % low 340-382 fps against milestone 12's 44-46 and stock's 244-260; its mean frame −10.4 % against milestone 12.
- Two reads in stock's SSAO/SSIL code fixed (`postfx/ss_interleave_exact_bounds`, `postfx/ss_depth_mips_exact`,
  0067): a last column never written at widths such as 1920, and racing stores into the depth mipmaps. Two runs of
  FellowEngine now give the same 60-frame sequences bit for bit in Bistro interior, PBR Lab and Sponza; two runs of
  stock differ at 69-82 dB.
- Fewer draws on the ubershader during autotune's evaluations (`autotune/prepare_pipelines`, 0068;
  `frame/depth_pipeline_keys`, 0069): Stress Lab 118-289 per measurement → 1, Lights Lab 15-19 → 0, PBR Lab
  57-174 → 17-49.
- Large surfaces at the LOD of their parts (`geometry/cluster_lod`, 0063, an option, off: it changes the image as
  LOD does): Sponza GPU −2.8 % (6.649 → 6.461 ms), render thread +0.101 ms; 63.6 dB on a moving sequence.
- Final (`m13_f199_final`, stock → fellow158): Sponza 13.61 → 6.39 ms (−53.1 %), Bistro exterior 6.55 → 4.45
  (−32.0 %), City Lab 2.84 → 1.94 (−31.8 %), Lights Lab 3.09 → 2.34 (−24.0 %), Bistro interior 4.32 → 3.36
  (−22.3 %), PBR Lab 2.65 → 2.17 (−18.1 %), Foliage Lab 3.66 → 3.03 (−17.2 %), Stress Lab 5.07 → 4.50 (−11.1 %).
  Load to the first frame −11 % (Sponza) to −67 % (Lights Lab).
- Verification: every default against its absence on lavapipe, 151 comparisons, 148 bit for bit and the three
  documented differences; Vulkan synchronization validation 0 hazards. Tried and removed: compiling autotune's other
  choices ahead (`frame/pipeline_prefetch_tunables`, 0066: its tasks hung the engine's exit on lavapipe).

Details: [report](docs/fellow_milestone13_report.md)

## Milestone 12 — the queues, loading, synchronization (2026-10-19)

- Loading (`frame/parallel_shader_create`, `frame/lazy_shader_variants`, 0061): shaders read from the shader cache
  are created in parallel and only when first asked for; a scene's build to its first frame −12 to −62 % against
  stock (Sponza 3.78 → 3.31 s, Bistro exterior 3.78 → 3.08 s, Lights Lab 0.28 → 0.11 s).
- SSIL on the main queue beside the async SSAO and SSR (`frame/async_ss_split`, 0060): in one process with autotune
  on, Bistro interior −2.7 % GPU, Bistro exterior −0.8 %; exact.
- Two synchronization fixes found by Vulkan validation: driver callbacks' barrier stages (`fellow147`), and a barrier
  for a read from a stage the last write was not made visible to (`debug/visibility_barriers`, 0062; no measurable
  cost).
- Autotune: an undecided evaluation of an option that starts off is continued next, and frames that create pipelines
  are not counted (`autotune/continue_undecided`, `autotune/skip_creation_frames`, 0064): Lights Lab's tile culling
  on for 79-100 % of a run (39-100 % before). The final's Lights Lab +3.5 % against milestone 11 was this coin toss,
  not a change of code.
- Final (`m12_f178_final`, stock → fellow151): Sponza 13.54 → 6.24 ms (−53.9 %), Bistro exterior 6.99 → 4.26
  (−39.1 %), City Lab 3.12 → 2.14 (−31.4 %), Bistro interior 4.24 → 3.35 (−21.1 %), Lights Lab 3.09 → 2.44
  (−21.1 %), Foliage Lab 3.66 → 2.96 (−19.0 %), PBR Lab 2.60 → 2.19 (−15.9 %), Stress Lab 4.99 → 4.92 (−1.4 %,
  n.s.).
- Rejected: large surfaces' clusters drawn front to back (`geometry/cluster_front_to_back`, 0058: 2 % off the depth
  pre-pass, nothing off the frame); a compiled sibling instead of the ubershader while a specialization compiles
  (`frame/pipeline_fallback`, 0059: doubles the compilations where materials first come into view).

Details: [report](docs/fellow_milestone12_report.md)

## Milestone 11 — specialized passes, synchronization between command buffers (2026-10-18)

- The colour passes without the code the frame cannot run (`shading/absent_features_specialized`, 0057): six
  specialization bits for decals, reflection probes, directional lights, omni and spot loops and positional shadows;
  the main variants drop to 72-80 registers. GPU in one process: Sponza −9.5 %, Bistro exterior −6.2 %, Bistro
  interior −6.0 %, Foliage Lab −4.7 %, City Lab −4.2 %, PBR Lab −3.4 %, Lights Lab −0.8 %. Exact after four versions
  (v4 on the RTX: bit for bit in City Lab and Bistro exterior, the floors elsewhere).
- Tone mapping compiled for the frame's paths only (`postfx/tonemap_specialized`, 0055): the pass −12 to −14 %, GPU
  −0.3 to −1.1 %. Volumetric fog's density pass without the loops the frame does not run (`postfx/fog_specialized`,
  0056): the pass −21 to −27 % (Lights Lab 0.200 → 0.159 ms).
- Light tile culling split around the async compute part (0049 v6): Bistro interior −1.9 % GPU. Barriers between the
  command buffers of a frame for resources used again with the same usage (`debug/segment_barriers`, 0054): a real gap
  in the render graph closed, no measurable cost.
- Final (`m11_f168_final`, stock → fellow142): Sponza 13.32 → 5.89 ms (−55.8 %), Bistro exterior 5.90 → 4.09
  (−30.8 %), City Lab 2.82 → 2.04 (−27.6 %), Bistro interior 4.06 → 3.05 (−25.0 %), Lights Lab 2.88 → 2.21
  (−23.3 %), Foliage Lab 3.44 → 2.75 (−20.0 %), PBR Lab 2.48 → 2.17 (−12.2 %), Stress Lab 4.82 → 4.48 (−7.2 %).
  Against milestone 10: Sponza −9.9 %, Bistro interior −6.6 %, Foliage Lab −5.3 %, Bistro exterior −5.1 %.
- Rejected: volumetric fog's light loops walked by the subgroup (`postfx/fog_light_loop`, 0053: the pass −5 % in
  Lights Lab, the frame slower in Sponza and Bistro exterior).

Details: [report](docs/fellow_milestone11_report.md)

## Milestone 10 — stock's inefficiencies: the cluster build, glow's blur (2026-10-14)

- The light cluster build without its 21.5 MB clear per frame, with instanced proxy draws and a pack pass per element
  (`lights/cluster_pack_elements`, `lights/cluster_instanced_draws`, 0051; exact): `debug/cluster_verify` 0
  mismatches over 10.6-21.1 billion words per scene; GPU in one process −2.3 % Lights Lab, −1.1 % PBR Lab, −0.9 %
  City Lab.
- Glow's blur without shared-memory bank conflicts, in 16 × 16 tiles (`postfx/glow_blur`, 0052; exact): glow's region
  0.136-0.199 → 0.053-0.076 ms; GPU City Lab −4.5 %, Lights Lab −3.8 %, PBR Lab −3.8 %, Bistro interior −2.0 %,
  Bistro exterior −1.7 %, Sponza −1.1 %.
- Final (`m10_f152_final`, stock → fellow131): Sponza 13.715 → 6.766 ms (−50.7 %), Bistro exterior 6.754 → 4.451
  (−34.1 %), City Lab 2.954 → 2.070 (−29.9 %), Lights Lab 3.044 → 2.352 (−22.7 %), Foliage Lab 3.652 → 3.015
  (−17.5 %), Bistro interior 4.183 → 3.458 (−17.3 %), PBR Lab 2.558 → 2.293 (−10.4 %), Stress Lab 4.928 → 4.776
  (−3.1 %, p 0.07); Foliage Lab with one MultiMesh per type 10.750 → 5.018 ms (−53.3 %).
- Rejected: the same padding in SSR's roughness filter (`postfx/ssr_filter_padded`: exact, no gain).

Details: [report](docs/fellow_milestone10_report.md)

## Milestone 9 — the positional lights (2026-10-12/14)

- The omni and spot lights' fields read together, before their tests (`lights/fields_early`, 0045; exact): Bistro
  interior −3.1 %, Lights Lab −3.0 % in one process.
- Light tile culling (`lights/tile_cull`, 0049; exact, under autotune): per 8 × 8 tile, the lights whose sphere can
  reach the tile's geometry; the light loop visits half the lights (lavapipe, Lights Lab: 9.8-16.1 → 4.6-9.2 per
  pixel); Lights Lab −5.6 % between processes. It slows other scenes, so autotune switches it on per scene only where
  it measures faster.
- Stock's pipeline cache crashed on save at 2 GiB: a file over `frame/pipeline_cache_max_mb` (1024 MiB) is now
  replaced, and a cache that would overflow the checksum is not saved (0050).
- Final (`m9_f145_final`, stock → fellow127): Sponza 13.591 → 7.123 ms (−47.6 %), Bistro exterior 6.771 → 4.551
  (−32.8 %), City Lab 2.932 → 2.119 (−27.7 %), Lights Lab 3.026 → 2.472 (−18.3 %), Bistro interior 4.199 → 3.536
  (−15.8 %), Foliage Lab 3.602 → 3.128 (−13.2 %), PBR Lab 2.558 → 2.367 (−7.5 %), Stress Lab 4.940 → 4.831 (−2.2 %,
  n.s.). Against milestone 8: Lights Lab −8.0 %, PBR Lab +2.6 % (milestone 10 found build-to-build variation of about
  ±2 % there, with no step at any change).
- Rejected: the next light read while the current one is processed (0046, 2-11 % slower), the loop's fields from an
  80-byte copy (0047), 0048's experiments: the colour pass's main variants sit at 96 registers, a budget to fit in.

Details: [report](docs/fellow_milestone9_report.md)

## Milestone 8 — the opaque pass: what its time is spent on (2026-10-12/14)

- Soft shadow filter taps fetched four at a time, with stock's arithmetic (`shadows/batched_taps`, 0040; exact): in
  one process Bistro exterior −5.0 %, PBR Lab −5.0 %, Sponza −4.6 %, Foliage Lab −3.8 %, Bistro interior −3.6 %,
  Lights Lab −2.1 %, City Lab −0.9 % (n.s.).
- TAA velocities in shared memory (`postfx/taa_shared_velocity`) and SSIL's last frame without its unread full-size
  level (`postfx/ss_last_frame_direct`) (0044; exact): the TAA pass −0.006 to −0.033 ms; the frame −0.5 to −1.0 %
  where SSIL is on.
- Final (`m8_f125_final`, stock → fellow110): Sponza 13.67 → 6.92 ms (−49.3 %), Bistro exterior 6.15 → 4.93
  (−19.8 %), City Lab 2.45 → 2.03 (−17.1 %), Bistro interior 4.12 → 3.61 (−12.5 %), Lights Lab 2.98 → 2.68
  (−10.3 %), Foliage Lab in chunks 3.53 → 3.17 (−10.2 %), PBR Lab 2.55 → 2.42 (−4.9 %), Stress Lab 5.11 → 4.84
  (−5.3 %, within noise); Foliage Lab with one MultiMesh per type 10.24 → 4.97 ms (−51.5 %). Milestone 7's build
  against this one in one session: −3.1 % (City Lab) to −6.8 % (PBR Lab).
- Rejected (seven ideas, six records): register experiments (0037), light cluster tiles under 32 pixels (0038), a
  PCSS early-out after the blocker search (0039), batched SSAO/SSIL taps (0041), two cascades through one PCSS and a
  blocker search shared by pixel quads (0042), dropping the lights that cannot reach a pixel first (0043).

Details: [report](docs/fellow_milestone8_report.md)

## Milestone 7 — vegetation: MultiMesh instances culled one by one (2026-10-11/12)

- MultiMesh culling (`geometry/multimesh_culling`, 0034; exact, on from fellow93): every instance tested against
  every pass's view volume on the GPU (test, scan, compact; order preserved; indirect draws). Foliage Lab with one
  MultiMesh per type −52.9 % against stock (−21 % with milestone 6's defaults), in chunks −9.5 %. Pass groups that
  would draw fewer than 300 000 indices are drawn the stock way (`geometry/multimesh_cull_min_indices`); autotune
  keeps the faster way.
- Per-instance mesh LOD for culled MultiMeshes (`geometry/multimesh_instance_lod`, 0035, an option, off: it changes
  the image; Foliage Lab from above 33 dB on lavapipe, 39.9 dB on the RTX): −2.2 % with the bench's light meshes.
- Swaying vegetation (`geometry/multimesh_cull_vertex_motion`, 0036, an option, off): the node's extra cull margin
  taken as the bound of the vertex motion; Foliage Lab with wind, one MultiMesh per type, stock 10.68 ms → 6.31 ms
  (−40.9 %), against −3.2 % with the defaults.
- Final (`m7_f105_final`, stock → fellow95): Sponza 13.67 → 7.48 ms (−45.3 %), Bistro exterior 6.59 → 4.90
  (−25.6 %), City Lab 2.82 → 2.20 (−22.1 %), Bistro interior 4.19 → 3.91 (−6.6 %, p 0.07), Lights Lab 3.04 → 2.79
  (−8.2 %), PBR Lab 2.63 → 2.53 (−3.8 %), Foliage Lab in chunks 3.61 → 3.27 (−9.5 %), Foliage Lab with one MultiMesh
  per type 10.89 → 5.13 (−52.9 %), Stress Lab 5.00 → 4.94 (−1.3 %, within noise).

Details: [report](docs/fellow_milestone7_report.md)

## Milestone 6 — visually lossless options; dGPU only (2026-10-08/09)

- A second kind of option: changes to the image that measurements show to be invisible (≥ 50-55 dB against stock and
  almost no pixel above 2 levels), off by default. In one process: adaptive shading rate (`shading/adaptive_vrs`,
  0027, under autotune) Lights Lab −21.1 % (opaque pass 1.356 → 0.649 ms), PBR Lab −5.2 %, Bistro interior −2.8 %;
  temporal SSAO (`postfx/ssao_temporal`, 0028) −1.5 to −3.1 %; temporal SSIL (`postfx/ssil_temporal`, 0030) −1.0 to
  −1.5 % where SSIL is used; volumetric fog at half rate (`postfx/fog_half_rate`, 0031) Lights Lab −1.7 %.
- Together, against the defaults between processes: Lights Lab −24.9 %, Bistro interior −9.0 %, PBR Lab −7.0 %, the
  other three scenes −3.2 to −3.4 %.
- TAA direct output (`postfx/taa_direct_output`, 0033; exact, on from fellow91): the post-processing chain reads the
  TAA history texture instead of a copy; the TAA region −0.034 to −0.041 ms, the frame −0.5 to −1.2 %.
- Final (`m6_f94_final`, stock → fellow87, now with the NVIDIA GPU only): Sponza 13.48 → 7.37 ms (−45.3 %), Bistro
  exterior 6.54 → 4.87 (−25.6 %), City Lab 2.84 → 2.16 (−24.0 %, p 0.07), Bistro interior 4.17 → 3.86 (−7.5 %),
  Lights Lab 3.02 → 2.75 (−8.9 %), PBR Lab 2.56 → 2.52 (−1.5 %, within noise).
- Rejected: occlusion culling of the opaque pass (0026: exact, frame +0.1 to +5.6 %); the depth pre-pass with the
  simple elements first (0029); soft shadows with half their taps under TAA (0032, not merged: PBR Lab's penumbrae
  47 dB).

Details: [report](docs/fellow_milestone6_report.md)

## Milestone 5 — scene-dependent choices, persistent instance data (2026-10-07)

- Autotune (`autotune/enabled`, 0020): the render thread runs each scene-dependent exact optimization on and off in
  alternating blocks of frames and keeps the faster choice per scene; all evaluations together take at most 20 % of
  the frames (`autotune/max_duty_percent`).
- Persistent instance data (`cpu/persistent_instances`, 0022): per-surface entries kept between frames, rewritten
  only when they change; City Lab's render thread −13.5 %.
- Async compute on by default on NVIDIA GPUs (`frame/async_compute`, 0015), chosen per scene by autotune. Auto
  exposure's luminance reduction with two barriers per workgroup (`postfx/fast_luminance_reduce`, 0025): the pass
  0.106 → 0.060 ms, 0 mismatches in 44 298 789 texels.
- New defaults against milestone 4's (`m5_f73_defaults`): City Lab 2.396 → 2.299 ms (−4.1 %), draw-heavy City Lab
  −4.6 %, PBR Lab −4.0 %, Lights Lab −1.5 %, Bistro exterior −0.7 % (p 0.17), Bistro interior +0.2 % and Sponza
  +0.2 % (n.s.); with the luminance reduction, Bistro exterior −0.7 %, Bistro interior −0.8 %, Sponza −0.4 % more.
- Rejected: instance data buffers in system memory (0021: GPU +2.5 to +3.4 %), the TAA resolve in 16 × 16 groups
  (0023), SSAO/SSIL gathers by sample rotation (0024).

Details: [report](docs/fellow_milestone5_report.md)

## Milestone 4 — render-thread CPU (2026-10-06/07)

- New bench scene City Lab: about 19 000 MeshInstance3D nodes, 300 carts moving every frame, a sun with 4 cascades;
  CPU-bound in stock. In-process A/B tool (`--ab`, `tools/ab_inproc.py`).
- Render lists built by the render thread and worker tasks with stock's lists, keys, order and instance data
  (`cpu/parallel_*`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, hot workers; 0016); draw lists
  recorded on several threads (`cpu/parallel_recording`, 0018); the end of the frame on other threads: parallel replay,
  present thread and submit thread (`frame/parallel_replay`, `frame/present_thread`, `frame/submit_thread`, 0017).
  `cpu/verify` 0 mismatches in every scene.
- Final (`m4_f61_*_perf`, stock → fellow59): City Lab draw-heavy 3.832 → 2.531 ms (−34.0 %, render thread −42.9 %),
  City Lab 2.777 → 2.321 (−16.4 %), Bistro exterior with the interior 6.335 → 5.051 (−20.3 %), Sponza 13.545 → 7.076
  (−47.8 %), Lights Lab 3.252 → 2.969 (−8.7 %), Bistro interior 4.295 → 4.070 (−5.2 %), PBR Lab 2.800 → 2.784
  (−0.6 %, noise). No scene slower than stock.
- Kept as options, off: the frame plan (`frame/replay_frame_plan`, 0019: −2.7 % in one process, −0.2 % between
  processes); async compute (costs about 3 % in City Lab); idle workers in UMWAIT (`cpu/hot_workers_wait`).

Details: [report](docs/fellow_milestone4_report.md)

## Milestone 3 — the light loop and the shadow filters (2026-10-01/02)

- Light clusters with logarithmic depth slices (`lights/cluster_depth_slices`, 0008): lights visited per pixel in
  Lights Lab 33.9 → 19.1; Lights Lab −0.27 ms (−7.9 %). The zero-attenuation skip (`lights/zero_attenuation_skip`,
  0008): Lights Lab opaque pass −0.38 ms; with both, 1.709 → 1.151 ms.
- Shadow min/max window pyramid v7, directional and positional (`shadows/directional_max_mip`,
  `shadows/positional_max_mip`, 0005): filters skip their taps where one fetch proves the result; Sponza −14 %, Bistro
  interior −3 %, Bistro exterior −1 %; its build 0.06 ms for a fully rewritten 4096² atlas.
- After the measured binary: blended fragments with alpha 0 skip their lighting (`shading/blended_alpha_zero_skip`,
  0010): Blend Lab transparent pass 0.417 → 0.275 ms (−34 %), frame 2.738 → 2.519 ms (−8.0 %); every FellowEngine
  scene-shader feature behind specialization constants (0012): the null test's Lights Lab opaque pass 1.506 ms against
  stock's 1.515 (was 1.847); the cluster pack pass with one workgroup per tile (`lights/cluster_pack_parallel`, 0014):
  Lights Lab −2.3 %, Bistro interior −1.0 %; async compute as an option, off (`frame/async_compute`, 0015: −0.9 to
  −2.1 %, render thread +0.15-0.76 ms).
- Final (`m3_final_perf`, fellow22): Sponza 13.547 → 7.201 ms (−46.8 %), Bistro exterior 5.625 → 4.973 (−11.6 %),
  Bistro interior 4.315 → 4.080 (−5.4 %), Lights Lab 3.238 → 3.018 (−6.8 %), PBR Lab 2.825 → 2.876 (+1.8 %). With
  fellow33 (`m3_f33_perf`): Sponza −47.4 %, Bistro exterior −11.5 %, Blend Lab −8.6 %, Lights Lab −5.7 %, Bistro
  interior −5.5 %, PBR Lab −0.5 % (within noise): no scene slower than stock.
- Bistro scene v2, the benchmark default from 2026-10-04: the exterior also loads the interior (stock 6.641 ms,
  fellow33 5.110 ms, −23.1 %). Fixed: headless `--import` crashed on exit (also in stock).
- Rejected: early submit and presenting from a second queue (0009); SSAO with shared-memory tiles (0011); TAA motion
  vectors of static geometry from the depth buffer (`postfx/taa_camera_motion_vectors`, 0013: not bit-identical,
  frame within noise).

Details: [report](docs/fellow_milestone3_report.md)

## Milestone 2 — shadow maps, shadow lookups, mesh clusters, the TAA regression (2026-10-01)

- Directional shadow static cache (`shadows/directional_cache`, 0004): Sponza shadow pass 3.40 → 0.28 ms, Bistro
  exterior 0.52 → 0.26 ms, render-thread CPU −42 %; VRAM +52 MB where a sun casts shadows.
- Shadow sampling skips for N·L = 0 pixels and an unblended cascade (`shadows/skip_*`, 0005): opaque pass −0.07 to
  −0.29 ms.
- Mesh clusters, frustum culling inside large surfaces (`geometry/mesh_clusters`, 0007): Sponza depth pre-pass −43 %,
  opaque pass −22 % (−1.2 ms GPU). The cluster build was moved off the loading path in fellow17 (Sponza 3.70 s
  against stock's 3.33 s).
- TAA v2 motion soften (0001 follow-up): milestone 1's foliage regression fixed; closer to the ground truth in 8/8
  motion sequences by PSNR and SSIM (+0.1 to +2.3 dB) and on fixed cameras in 4/4 scenes (LPIPS −19 to −24 %).
- Frame time at 1080p (`fellow-m2_perf`): Sponza 13.93 → 8.63 ms (−38.1 %), Bistro exterior 5.75 → 4.96 (−13.7 %),
  Bistro interior 4.56 → 4.41 (−3.3 %), PBR Lab 3.07 → 2.96 (−3.4 %); at 2560x1440 −30.3 %, −11.8 %, −5.3 % and
  −2.3 % (PBR Lab not significant). The null test (`rendering/fellow/enabled=false`) stays within noise.
- Optional look changes, off: the PCSS blend band and a parallel-light penumbra (opaque −0.18 to −0.40 ms with the
  band). Rejected: the max-only shadow pyramid v1 (no net gain); temporal SSAO (0006, parked on branch `fellow/ssao`).

Details: [report](docs/fellow_milestone2_report.md)

## Milestone 1 — temporal anti-aliasing and a GPU shallow-water prototype (2026-09-30)

- TAA resolve v2 (`postfx/taa_resolve_v2`, 0001): reverse-Z closest-depth dilation, dilated-velocity reprojection, a
  YCoCg variance clip that does not collapse under motion. Closer to an accumulated ground truth on fixed cameras in
  4/4 scenes (LPIPS −19 to −25 %) and in 7 of 8 motion sequences (PSNR +0.3 to +1.8 dB).
- TAA history ping-pong (`postfx/taa_history_pingpong`, 0002): two fewer full-screen copies; the TAA stage 14-22 %
  faster (0.04-0.12 ms) at identical output. With v2, the TAA stage is 3-15 % cheaper than stock in all 8
  scene/resolution configurations; the whole frame is unchanged within noise.
- `FellowShallowWater3D` (module `fellow_fluid`, 0003, opt-in): a GPU shallow-water prototype, a 512² grid in 0.21 ms
  (1024² in 0.75-0.79 ms), mass conserving against a NumPy reference, no readback.
- Benchmark suite, renderer audit (`docs/fellow_renderer_audit.md`), ground truth from 2×2 SSAA × 8× MSAA × jitter.
- One measured regression: more shimmer on alpha-tested grass and fence during a lateral strafe (motion error +6 %,
  flicker +5 %, PSNR −0.4 dB); fixed in milestone 2.

Details: [report](docs/fellow_milestone1_report.md)
