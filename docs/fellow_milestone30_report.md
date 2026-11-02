# FellowEngine — milestone 30 report (snow tracks)

Status: **measured** — lavapipe (2026-11-02) and the RTX 5070 Ti Laptop at 1080p (`m30_f292`, `f293`, 2026-11-02).
Branch `fellow/geometry`. Decision record `docs/decisions/0130-snow-tracks.md`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md` ("Snow", tracks).

## 0. Result

Milestone 28's snow can now be walked in. `FellowSnowTracker3D` nodes — feet, wheels, runners — press tracks into
the snow while they touch the ground, and `FellowRain3D.stamp()` lays them when a script asks. The cover shows them:
the walls of a footprint lit from the side, packed snow at its bottom, a rim of pushed-up snow around it, and the
ground where a cart's wheels cut through. The falling snow fills them back. They cost nothing measurable.

## 1. Development

* **A world-anchored, wrapping texture.** The tracks' depths (R16F, 2048² over 48 m around the camera: 2.3 cm cells)
  are stored so that texel *i* holds world cell *c* ≡ *i* (mod 2048): sampled with repeat at the world position, the
  texture never has to be copied when the camera moves; only the strips the window enters are reset.
* **Updating only what changed.** The pass covers the strips entered this frame, then the bounds of this frame's
  stamps — the whole window only when the falling snow has filled the tracks back by 0.1 mm (smaller steps would
  vanish in half floats, so the refill is accumulated on the CPU). A first version dispatched the whole texture
  every frame a tracker moved: 4 M cells a frame for a few square centimetres of change.
* **Contact.** A tracker presses only within `contact_height` of the scene's top, read on the GPU from the shelter
  heightfield — eroded as the snow's shelter (0128): under a wire the raw heightfield's top is the wire, and a wheel
  rolling under it had stopped leaving a groove.
* **The look.** Walls sloping over the outer half of the stamp's radius (over the outer quarter, they stepped on the
  cells close up); packed snow 30 % greyer, smoother, without glints; a rim a quarter of the depth high; the ground
  where a track reaches the snow's depth.
* FellowBench: `--weather_snow_footprints` / `--weather_snow_ruts` lay footprints and ruts along polylines in any
  scene, again whenever the camera jumps (the tracks live in a window around it); Rain Lab `--rain_tracks=1` drives a
  cart down the street and lays footprints. To place the paths in Bistro, a first job captured its streets from
  above with test paths.

## 2. Verification

* Rain Lab without drops and Water Lab bit for bit on lavapipe against milestone 27's build.
* A test scene and Rain Lab on lavapipe: footprints, a groove to the ground, ruts and footprints down the street.

## 3. Performance

RTX 5070 Ti Laptop, 1080p, Rain Lab snowing, 2 runs each (`m30_f292`): 2.258 ms of GPU time without tracks, 2.249 ms
with a cart's two wheels pressing every frame and 120 footprints while the camera follows the flythrough — not
measurable.

## 4. Showcase

Bistro's street under snow with a cart's ruts and footprints (`m30_s11_*`, fellow220; [SHOWCASE.md](SHOWCASE.md)):
at night (a still and a loop down the street) and after sunset. The ruts cut to the cobbles and catch the lamplight;
the footprints read as a line of shallow prints. The paths were laid after a job (`m30_s10`) that captured the
streets from above with test paths: the first ruts ran over the curb and were moved half a lane.

## 5. Left for later

* Real depth: the tracks shade the snow but do not displace it (no parallax, no shadows inside a footprint). A
  one-step offset parallax was tried and reverted: at grazing views it moved a deep rut's samples out of the rut (a
  thin line instead of a groove) and drew footprints as outlines; a march of several steps, or a displaced snow mesh
  near the camera, would be needed.
* Characters in the bench that walk (Crowd Lab's) with trackers on their feet.
* Tracks in the snow on other surfaces than the scene's top (a roof under a balcony is not reachable).
