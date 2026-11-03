# 0136 — Ice and hoarfrost

Status: **KEEP** (with a FellowRain3D: `freeze` and `frost` 0, nothing drawn; a FellowWeather3D below 0 °C sets them)
· Branch `fellow/geometry` · Nodes: `FellowRain3D.freeze`, `.frost`, `.freezing_time`, `FellowWeather3D.temperature` ·
Code: `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` (`fellow_wet_surface()`,
`fellow_frost()`), `scene_forward_clustered_inc.glsl` (`FellowWeatherData::frost`), `effects/fellow_weather.{h,cpp}`
(`State::freeze`, `State::frost`), `forward_clustered/render_forward_clustered.cpp`,
`modules/fellow_fluid/fellow_rain_3d.{h,cpp}`, `fellow_weather_3d.cpp` (`_apply_ground()`),
`benchmarks/project/bench/main.gd` (`--weather_temperature`, `--weather_ground`, `--weather_drops`).

## Problem

The weather's ground (0124, 0128, 0132) is wet, or covered in snow, or dry. Below freezing a wet street stays a wet
street — its puddles ripple in the rain, its film is water — and a clear cold night leaves nothing on the ground: no
frozen puddles, no hoarfrost on the morning after.

## Change

**Ice.** `FellowRain3D.freeze` (0-1) is how much of the water on the ground is ice. The rain's ripples on the wet
surfaces are multiplied by `1 − freeze` (ice does not ripple; rain falling on it splashes still). The puddles frozen are
dark clear ice, a little milky (4 %), glossy still (roughness 0.05), with a white rough rim where the ice is thin (the
puddle's edge, its depth from 0.05 to 0.3: up to a third white, roughness 0.35). The wet film keeps its darkening and
gloss: glaze.

**Hoarfrost.** `FellowRain3D.frost` (0-1): crystals on what faces the sky, where the rain would reach (its shelter
heightfield, 0124) — all of it on what faces up, less on walls (a third on a vertical face), none underneath. It grows in
fine grains (the puddles' noise at 5 cm, sooner in places at 20 cm) and stays a veil the surface shows through: grey
white (0.76, 0.8, 0.85) over 20-50 % (a finer noise), matte (roughness at least 0.75), no metal; one crystal in 14
(cells of 5 mm) turned at random and smoother — glints in the sun, faded where a cell is under a pixel. Drawn before the
snow cover, which covers it.

Both go to the renderer with the wetness and the snow (`FellowWeather::State`, the weather UBO's `frost` vector); the
weather's specialization (the `fellow_wetness` bit, 0124) is on while either is above zero. At 0 the code is skipped
(`freeze` multiplies the ripples by exactly 1).

**The ground model.** A FellowWeather3D (0132) below 0 °C freezes the water on the ground in its rain node's
`freezing_time` (600 s) at 5 degrees below (twice as fast at 10 below and colder; rain falling below freezing glazes as it
lands) and thaws it above, twice as fast. Hoarfrost forms below 0 °C while nothing falls, in `freezing_time` at 5 below
under a clear sky, a fifth as fast under a full deck (the clouds keep the ground warm), and goes above 0 °C — four times
as fast, at once in the rain. Below 0 °C the controller creates its rain node (without rain) for them. Ready below 0 °C
it settles the ground frozen, and frosted under a clear sky when nothing falls.

## Verification

* **Off is the previous image** (lavapipe, against milestone 35's build): Rain Lab raining without drops (5 images; the
  particles are not deterministic from run to run: with them two runs of the same build differ in about 1000 pixels),
  Water Lab (5), the wind meadow under snow (3) — 0 pixels differing.
* **Lavapipe, the test scene** at the feet (2 m away): the puddles frozen dark with whitish rims against the same wet
  ground; frost at 0.4, 0.6 and 1 — scattered grains, a speckled cover, a grey-white veil with the ground showing
  through — on the ground, the sphere's top and the box's top, less on their sides. The first frost (the puddles' noise
  at 20 cm, white over 85 %) was patchy snow; at 5 cm and as a veil it reads as frost.
* **The first look on the RTX** (`m36_s19_bistro_thaw_loop`, fellow230: the veil over 30-70 % at roughness 0.5) turned
  Bistro's street white as snow: most of it was the sky mirrored at a grazing angle by the glossier crystals. Matte
  (roughness at least 0.75) and thinner (20-50 %), the cobbles show through (lavapipe). The frozen puddles' rims
  (depth 0.1 to 0.7, half white) turned whole puddles white on the street and the roofs (`m36_s23_bistro_frost`);
  narrowed to their edges (0.05 to 0.3, a third white) the ice is dark (lavapipe).
* **The ground model** (the test scene, custom weather at −5 °C under a clear sky, the ground at 0, the model 200 times
  as fast): frost 0.147, 0.587 and 1 at 0.5, 2 and 4 s — the expected 200 × (1 − 0.8 × 0.15) / 600 = 0.293 per second; the
  puddles' ice 0.167 and 0.667 (0.333 per second).

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Bistro exterior under a FellowWeather3D clear at −4 °C with the ground held, wet
against frozen and frosted, 2 runs each, `m36_f313_ice`, fellow234): the depth pre-pass 0.765 → 0.821 ms (+0.06 ms;
the frost's three lookups run there too), the opaque pass 1.648 / 1.642 ms; the frame 4.971 / 4.837 ms (the first
build's runs noisy: not measurable).

**Bistro on a frosty morning** (the controller clear at −4 °C, the sun 9° up, the day look with ray tracing; stills
`m36_s23_bistro_frost` with fellow234, `m36_s25_bistro_ice` with fellow236; the loop `m36_s25_bistro_thaw_loop`;
`bistro_frost_morning.jpg`, `loop_bistro_thaw.webp`): the street grey-white with hoarfrost, bare by the walls under
the awnings; the puddles' ice dark with pale edges; turned warm, the frost thaws in grains and the ice goes, the street
left wet.

## Decision

**KEEP.**
