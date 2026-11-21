# FellowEngine — milestone 168 report (screen probes that keep their place)

Status: **FINAL** (2026-11-21). Branch `fellow/geometry`. In the cloud on lavapipe only: the owner needs the laptop,
and its GPU was not used (its queue paused since 05:05).

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

## 1. Found: the RTX and lavapipe disagree inside GI Lab's room

The path tracer's pictures of GI Lab agree to 0.00 stops between the RTX (decision 0308's captures) and lavapipe. The
real-time options do not: inside the room the RTX's are 1.40 stops brighter with SDFGI, 0.89 with its gather and 1.20
with ray-traced DDGI (outside, within 0.05). Not the resolution (lavapipe at 1280x720: the same), the autotuner (off:
the same), FellowEngine (stock SDFGI on lavapipe: the same) or the frames (600: the same). SDFGI (stock Godot's) and
DDGI (FellowEngine's) move together, so it is something they share on one of the two drivers. On lavapipe SDFGI and
its gather are also 1.7-2 stops too dark in GI Arcade's shaded gallery. Decision 0308's absolute numbers in enclosed
rooms (RTX) and this milestone's (lavapipe) cannot both be right; the comparisons between options on one platform hold.
Job f533 (prepared, not queued) renders the room on the RTX with FellowEngine off and with full barriers.

## 2. Checks

* The image regression suite on lavapipe (38 scenes, the editor build): every picture identical to its golden (the
  gathers are off by default, and their new settings only act where one is on).
* Lavapipe runs: 16 still frames after 200 for both gathers (old, persistent, with feedback), DDGI, SDFGI and no GI in
  two of GI Arcade's views; linear captures of every option and the path tracer (16 bounces) in GI Lab's three views and
  GI Arcade's four; GI Lab's room after 600 frames; DDGI at 1280x720; stock SDFGI.
* The RTX's still frames of Sponza from decision 0308's jobs (f527-f530), analysed again (the autocorrelation).

## 3. On the RTX when the owner allows (prepared, not queued)

* f532: Sponza's curtains in still frames for every option (old, persistent, feedback), captures of every scene
  against the path tracer (GI Arcade's reference new), on the m168 template.
* f533: GI Lab's room with FellowEngine off and with full barriers (the platforms' disagreement).

## 4. Next (graphics)

* The RTX/lavapipe disagreement inside rooms (f533), then decision 0308's and 0309's absolute numbers corrected.
* SDFGI's darkness in shade on lavapipe (GI Arcade's gallery 2 stops) if it is lavapipe's, or the RTX's brightness if
  it is the RTX's.
* Specular against a reference; screen-space traces for contact detail below SDFGI's cells.
