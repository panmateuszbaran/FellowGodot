# FellowEngine — milestone 168 report (screen probes that keep their place)

Status: **FINAL** (2026-11-22). Branch `fellow/geometry`. The screen gathers measured on lavapipe and confirmed on the
RTX (job f532, the night the owner allowed the GPU); the showcase's light and sky (sections 4-5) on lavapipe, the
laptop's GPU not used again by the owner's decision.

## 0. Result

Milestone 167's screen gathers were closer to the path tracer than SDFGI and DDGI, but the ray-traced one flickered on
Sponza's curtains (10.7 % of a still view's pixels, DDGI 3.7 %). This milestone finds why, fixes it, and feeds the
ray-traced gather's further bounces from its own probes (decision 0309).

* **Why.** The RTX's still frames of the curtains: the flicker is white in time (lag-1 autocorrelation −0.03 over the
  flickering pixels; DDGI +0.39). Averaged probes change slowly, so these were starting over every frame. A probe was
  placed again at its tile's centre pixel each frame; the TAA's jitter moves that pixel's sample by up to half a pixel,
  and on a fold, a leaf or a slat it lands on another surface, where the history's check fails.
* **Fix.** `sdfgi/screen_gather_persistent` (on): a probe keeps its world point and its history while that point is
  still seen in its tile (a pixel within one of its projection lies on its plane). A still view's probes no longer
  move; a moving view's move with the surfaces.
* **The further bounces.** `raytracing/gi_screen_gather_feedback` (on): a ray-traced gather ray's hit takes its
  further bounces from the previous frame's screen probes where they saw it, DDGI's 2 m probes only elsewhere.
* **GI Arcade** (`--scene=gi_arcade`, new): a colonnade with folded double-sided curtains, a tree of 600 leaves and a
  shutter of slats, Sponza's kinds of geometry without its assets.

The flicker a GI option adds (GI Arcade, lavapipe, 480x270; the share of a still view's pixels changing by more than 2
levels over 16 frames, among those that change by less than 1 without GI):

| | colonnade | the tree |
|---|---:|---:|
| ray-traced DDGI | 0.04 % | 0.18 % |
| ray-traced gather (M167) | 0.71 % | 0.49 % |
| **ray-traced gather, persistent probes** | **0.07 %** | **0.17 %** |
| SDFGI | 0.15 % | 0.20 % |
| SDF gather (M167) | 0.73 % | 0.61 % |
| **SDF gather, persistent probes** | **0.17 %** | **0.21 %** |

Against the path tracer (bias / error a pixel in stops, lavapipe; GI Lab after 300 frames, GI Arcade the mean of four
views):

| | GI Lab: room | GI Arcade |
|---|---:|---:|
| ray-traced DDGI | +0.86 / 0.88 | +0.21 / 0.23 |
| ray-traced gather (M167) | +0.35 / 0.36 | +0.02 / 0.08 |
| … persistent probes | +0.36 / 0.37 | +0.03 / 0.09 |
| **… and further bounces from its probes** | **+0.25 / 0.27** | **+0.02 / 0.08** |
| SDFGI | −0.74 / 0.97 | −0.98 / 0.99 |
| SDF gather (M167 → persistent) | −0.91 → −0.92 | −0.95 → −0.95 |

* Persistent probes leave the light as it was (0.01 stops) and take the flicker the gathers added away.
* The ray-traced gather's further bounces from its own probes take a third of its remaining excess in GI Lab's room.

On the RTX (job f532, Sponza's curtains at 1280x720, the share of a still view's pixels the GI makes flicker): the SDF
gather 0.58 % → **0.10 %** (SDFGI's 0.10 %), the ray-traced gather 7.62 % → **3.44 %** (DDGI's 1.55 %): its probes now
keep their history (lag-1 autocorrelation −0.03 → +0.38, DDGI's +0.39); what remains is their 16 rays' own noise,
for more history to take (section 3). Against the path tracer the further bounces from the probes take 40 % of the
ray-traced gather's excess in GI Lab's room (+1.22 → +0.73 stops) and a quarter in GI Arcade's gallery: the closest
option on the RTX, 0.26 stops a pixel over 23 views (SDFGI 0.39, DDGI 1.05).

## 1. Found: the RTX and lavapipe disagree inside GI Lab's room

The path tracer's pictures of GI Lab agree to 0.00 stops between the RTX (decision 0308's captures) and lavapipe. The
real-time options do not: inside the room the RTX's are 1.40 stops brighter with SDFGI, 0.89 with its gather and 1.20
with ray-traced DDGI (outside, within 0.05). Not the resolution (lavapipe at 1280x720: the same), the autotuner (off:
the same), FellowEngine (stock SDFGI on lavapipe: the same) or the frames (600: the same). SDFGI (stock Godot's) and
DDGI (FellowEngine's) move together, so it is something they share on one of the two drivers. On lavapipe SDFGI and
its gather are also 1.7-2 stops too dark in GI Arcade's shaded gallery. Decision 0308's absolute numbers in enclosed
rooms (RTX) and this milestone's (lavapipe) cannot both be right; the comparisons between options on one platform hold.
Job f533 (prepared, not queued) renders the room on the RTX with FellowEngine off and with full barriers.

GI Arcade on the RTX (f532) says more: the path tracers agree within 0.01 stops in its four views, and the real-time
options within 0.13 in its sun-lit ones, but in the gallery's and the shutter's shade the RTX's are brighter by 1.3-1.4
stops (SDFGI), 1.1-1.2 (DDGI) and 0.4-0.6 (the ray-traced gather, which lavapipe renders within 0.05 of the path
tracer there). Something lifts the light reaching shade through the options' own caches (SDFGI's cascades, DDGI's
probes, the gather's further bounces from them) on one of the two drivers, not the path tracer's, which traces every
bounce again each frame.

## 2. Checks

* The image regression suite on lavapipe (38 scenes, the editor build): every picture identical to its golden (the
  gathers are off by default, and their new settings only act where one is on). Again after the clouds' detail: the
  sky scene changed (its new golden), identical to the old with `post/clouds/detail` off; the other 37 unchanged.
* Lavapipe runs: 16 still frames after 200 for both gathers (old, persistent, with feedback), DDGI, SDFGI and no GI in
  two of GI Arcade's views; linear captures of every option and the path tracer (16 bounces) in GI Lab's three views and
  GI Arcade's four; GI Lab's room after 600 frames; DDGI at 1280x720; stock SDFGI.
* The RTX's still frames of Sponza from decision 0308's jobs (f527-f530), analysed again (the autocorrelation).
* The RTX (f532, the m168a template): Sponza's curtains in 16 still frames for every option; linear captures of GI Lab,
  GI Arcade (its path-traced reference new), Sponza and Bistro (inside and out) for both gathers with and without the
  new settings, against the path tracer (decision 0309's tables).

## 3. On the RTX

* Done (f532): above and in decision 0309.
* Prepared, not queued (the GPU not to be used for now): f533, GI Lab's room with FellowEngine off and with full
  barriers (the platforms' disagreement; its first attempt stopped on a batch file's `%` before it rendered, fixed).
* Next there: the ray-traced gather's remaining flicker with 32 and 64 frames of history (decision 0308's f528
  changed nothing because the history was not kept); the persistent probes' and the feedback's GPU time.

## 4. The showcase's light beside the reference

The owner: "still very far from AAA". The rooftops' reference shot beside the owner's video frame by frame (lavapipe,
its frames 45, 84 and 93): milestone 159 had tripled the sky's and the bounce's light, which flattened every shadow, and
a dense volumetric haze and the camera's depth of field veiled the square.

* The ambient light 3.0 → 1.0, SDFGI's energy 2.0 → 1.0, SSIL 1.2 → 1.0; the sun 3.3 → 4.0; the volumetric fog a
  quarter as dense; the reference camera's far blur 0.06 → 0.02; the saturation 0.8 → 0.9; the ashlar fronts darker,
  each block's tone more its own; the square's setts greyer.
* The shade as dark as the reference's in the climb (the climber's shadow a clear shape on the sunlit wall); the
  median 1.16 times the reference's (1.36 before), the highlights unchanged.

## 5. The sky

The owner: the reference's sky "has much more detail and is prettier and more interesting". Beside it ours was a dim
grey over bright walls with a few soft blobs of cloud; the reference's is a bright golden-hour sky, peach towards the
sun and pale blue above, with streaks of stratocumulus and lit billows. Of the variants shown, the owner chose a layer
of stratocumulus with the sun 10 degrees up.

* **The clouds' detail** (decision 0310, `post/clouds/detail`, on): their detail noise had eroded every edge alike (its
  octaves' sum varies by a tenth) at features of a pixel or less, under 64 even steps of several hundred metres. Now
  stretched to [0, 1] at 150-600 m and eroded deeper; coarse steps through clear air and fine ones inside the clouds
  (25 m near the camera); the billows shading one another; Schneider's powder term.
* **The layer** (`scripts/rooftops.gd`): coverage 0.6, density 2.5, 2.5 km up, 0.6 km thick (crisp edges; flat, so
  that towards the horizon they are streaks); their shadows off (they had shaded the climb's wall); the haze's aerosols
  halved; the depth fog no longer over the sky (its beige had greyed it).
* **A sun for the sky** (`sky_sun_elevation` 10): a second directional light only the sky and the clouds see
  (`SKY_MODE_SKY_ONLY`; the scene's Sun `LIGHT_ONLY`), in the Sun's direction but 10 degrees up: the golden hour's sky
  and clouds lit from the side over a square lit from 27 degrees. With the Sun itself at 10 degrees the climb's wall
  fell into its neighbours' shade, and at 19 the climber's shadow left the wall.
* **The sky's brightness** (`sky_brightness` 4): the sky seen and its clouds four times what the sun's energy makes them
  (the SkySun's energy), its light on the scene (the radiance map: ambient light, reflections) as before (the sky
  shader's new `radiance_scale`, 1/4; the sun's disk too, `sun_disk_energy`). Beside the reference ours had been at two
  thirds of its level (AgX: a stop moves a mid-grey sky by about 15 levels of 255), its clouds hardly brighter than the
  sky (the clouds' multiple scattering approximated by four octaves: a low sun's side light darker than a white
  wall's).
* **The square's light through the same air** (`sun_air` 1): the Sun's colour times the air's transmittance between a
  sun 10 and 27 degrees up (Rayleigh's sRGB coefficients, the haze, ozone; Kasten and Young's air mass; its luminance
  kept), as the sky's sun is: (1.0, 0.9, 0.78) → (1.09, 0.88, 0.63).

Lavapipe, the reference shot's frames 45, 84 and 93 and two views over the square, one towards the sun and one away
(the sky before: the committed one, the clouds' detail off; `docs/showcase/rooftops_sky_m168.jpg`):

| | the sky away from the sun (sRGB) | the sunlit stone: red / green, blue / green |
|---|---:|---:|
| before | (126, 133, 146) | 1.13-1.17, 0.82-0.85 |
| now | (173, 176, 182) | 1.13-1.17, 0.81-0.85 |
| the reference | 200-230, pale lavender to peach | 1.23-1.27, 0.76 |

* The sky a bright pale one under cream-lit stratocumulus instead of a dim grey-blue with a few wisps; towards the sun
  the clouds' edges lit and the low sun in a warm haze; the scene's shade unchanged (the radiance map scaled back).
* The light shafts kept as they were (their threshold four times higher, their intensity a quarter: the sky's excess
  over the threshold four times as bright); the sun's disk at the Sun's own energy. Four times as bright, both had
  veiled the square looking towards the sun.
* The sunlit stone as warm as before: the clearer air's whiter sun warmed back by `sun_air`. The reference's is warmer
  still; its roof tiles' colour ours already reaches (red / green 1.45 against 1.39), so the stone's own colour, not the
  light, is what differs.
* Left: the clouds' sharper edges show the half-resolution trace's steps in a still frame; the reference's sky has
  more colour (peach to lavender) than the physical atmosphere gives under AgX; with the sky's sun at 6 degrees the
  clouds turn peach (the owner chose 10).

## 6. Next (graphics)

* The RTX/lavapipe disagreement inside rooms (f533), then decision 0308's and 0309's absolute numbers corrected.
* SDFGI's darkness in shade on lavapipe (GI Arcade's gallery 2 stops) if it is lavapipe's, or the RTX's brightness if
  it is the RTX's.
* Specular against a reference; screen-space traces for contact detail below SDFGI's cells.
