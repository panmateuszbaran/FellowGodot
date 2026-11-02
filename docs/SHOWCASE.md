# FellowEngine showcase

Real-time captures from FellowBench (`benchmarks/project`) on an RTX 5070 Ti Laptop GPU, builds `fellow214`
(`0b7c021`) to `fellow220` (`9356d62`, the snow, its tracks and the lightning). Each was rendered at 1920 × 1080, supersampled 1.5× under TAA and
settled for 300 frames, then scaled to 1600 × 900. The loops are 960 × 540 animated WebP.

Nothing here is painted over. Each scene is the bench scene with FellowEngine's settings and nodes turned on, plus a
camera, sun and exposure chosen for the picture. The command lines are in the showcase jobs (`m26_s2`-`m26_s4`): see
[how they were made](#how-they-were-made) below.

## Bistro at night, in the rain

<img src="showcase/bistro_night_rain_cafe.jpg" width="100%" alt="Bistro's cafe at night in the rain, wet cobblestones reflecting the lamps">

<img src="showcase/bistro_night_rain_street.jpg" width="100%" alt="A Bistro street at night in the rain, puddles mirroring the street lamps">

<img src="showcase/loop_bistro_rain_street.webp" width="100%" alt="Moving down the wet street at night">

<img src="showcase/bistro_night_rain_alley.jpg" width="100%" alt="An alley at night in the rain">

This is Amazon Lumberyard Bistro with a `FellowRain3D` added. The drops fall around the camera and splash on the
scene, and roofs and awnings keep them off. Every opaque material gets wet after its own code: porous surfaces darken,
everything gets a glossy film, and puddles with ripples form on flat ground. The ray-traced reflections then mirror the
lamps and string lights in the cobblestones. The sun is off, and the scene's own lamps and ray-traced GI light it.

Settings: ray tracing (DDGI, reflections, AO), bloom with lens flare, contact shadows, exposure ×6.

## Bistro in the snow

<img src="showcase/bistro_snow_night_cafe.jpg" width="100%" alt="Bistro's cafe at night in the snow: snow on the awnings, the planters and the street, flakes in the lamplight">

<img src="showcase/loop_bistro_snow_cafe.webp" width="100%" alt="Moving towards the cafe at night as it snows">

<img src="showcase/bistro_snow_night_street.jpg" width="100%" alt="A Bistro street at night under snow, the lamps lighting the flakes">

<img src="showcase/loop_bistro_snow_street.webp" width="100%" alt="Moving down the snowy street at night">

<img src="showcase/bistro_snow_gold_tree.jpg" width="100%" alt="The square's tree after snowfall against a low sun: bare cobbles under its canopy, light shafts, flakes glowing">

<img src="showcase/bistro_snow_gold_cafe.jpg" width="100%" alt="The cafe in the snow in low sunlight">

The same Bistro with `FellowRain3D` in its snow mode (milestone 28). Flakes drift down around the camera and stop on
roofs and awnings. The snow cover lies on everything that faces up, where the snow falls: the streets, the awnings,
the planters, the leaves. Under the café's awnings and the tree's canopy the ground stays bare, but wires and strings
of lights overhead leave no marks. The flakes are drawn in the opaque pass with the particles' motion vectors, so TAA
keeps them. At night the lamps light them; after sunset they glow against the low sun.

Settings: at night as in the rain, exposure ×5, `--weather_snow=0.8`; at the golden hour as below with
`--weather_snow=0.3`, the sun at 7°.

<img src="showcase/bistro_snow_tracks_night.jpg" width="100%" alt="A snowy Bistro street at night with a cart's wheel ruts cut to the cobbles and a line of footprints">

<img src="showcase/loop_bistro_snow_tracks.webp" width="100%" alt="Moving down the street along the ruts and the footprints">

<img src="showcase/bistro_snow_tracks_gold.jpg" width="100%" alt="The same tracks after sunset">

Tracks (milestone 30): a cart's two ruts cut down to the cobbles and a line of footprints beside them, laid along
polylines with `--weather_snow_footprints` and `--weather_snow_ruts` (`FellowRain3D.stamp()`; in a game,
`FellowSnowTracker3D` nodes on feet and wheels press them as they move). The footprints' walls catch the light, their
bottoms are packed snow, and a rim of pushed-up snow runs beside them.

## Bistro at the golden hour

<img src="showcase/bistro_gold_tree.jpg" width="100%" alt="The square's tree against a low sun, light shafts through its leaves">

<img src="showcase/loop_bistro_gold.webp" width="100%" alt="Moving past the tree at the golden hour">

<img src="showcase/bistro_gold_cafe.jpg" width="100%" alt="The cafe in warm low sunlight">

The sun is 7° above the horizon (5° for the cafe). The sky comes from FellowEngine's atmosphere: its tables give the
sky, the aerial perspective and the sun's colour through the air. Volumetric clouds drift overhead. Light shafts
come through the leaves, and bloom and histogram auto exposure finish the image. The ray-traced GI, reflections, AO
and sun shadows are on.

## Water at sunset: `FellowWater3D`

<img src="showcase/water_pier_sunset.jpg" width="100%" alt="The pier against the sun's glitter path at sunset">

<img src="showcase/loop_water.webp" width="100%" alt="Panning over the lake towards the low sun">

<img src="showcase/water_sunset_glint.jpg" width="100%" alt="The glitter path under clouds with crepuscular rays">

The lake's wind waves come from a JONSWAP spectrum, simulated by FFT in cascades. The glitter path comes from the waves
a pixel does not resolve. Reflections are traced over a Hi-Z pyramid of the depth texture, so the far shore and the
pier are mirrored at their true parallax. The sun is at 8°, under the atmosphere sky with volumetric clouds and light
shafts.

<img src="showcase/water_gusts_sunset.jpg" width="100%" alt="The glitter path broken up by gusts of wind running over the lake">

Under a `FellowWind3D` of 9 m/s (milestone 27) the water takes the scene's wind: the spectrum follows its speed and
direction, and its gusts roughen the short waves, so the glitter path breaks up where a gust runs over the lake.

## Wind in the forest: `FellowWind3D`

<img src="showcase/wind_forest_backlit.jpg" width="100%" alt="Light shafts between the trunks of a forest, backlit by a low sun">

<img src="showcase/loop_meadow.webp" width="100%" alt="Gusts rolling over the meadow">

<img src="showcase/loop_forest.webp" width="100%" alt="The forest swaying in the wind">

<img src="showcase/wind_meadow_backlit.jpg" width="49%" alt="The meadow against the sun"> <img src="showcase/wind_forest_edge.jpg" width="49%" alt="The forest's edge in the wind">

Wind Lab: about 40 000 grass clumps in a ring of 700 broadleaf trees and 500 bushes. Each plant bends with the
wind's force at its position and sways at a frequency of its size, and its leaves flutter. The gusts are a noise field
travelling with the wind: they roll over the meadow, and the bent grass brightens. Shadows and motion vectors follow
the motion. The sun is at 9°, behind the trees. The loops show four seconds of the wind.

## Fire and burning wood at night: `FellowGasVolume3D`, `FellowBurnable3D`

<img src="showcase/burn_night_logs.jpg" width="100%" alt="Two logs and a branch burning with their own flames at night">

<img src="showcase/loop_burn.webp" width="100%" alt="The logs burning">

<img src="showcase/burn_night_kindling.jpg" width="49%" alt="The kindling's flames heating the logs"> <img src="showcase/burn_night_crate.jpg" width="49%" alt="The crate burning after the campfire has burned out">

Burn Lab. A kindling fire (a gas volume simulated on the GPU) heats two logs, a branch and a crate. Each object is
voxelized, and the voxels heat, pyrolyse to char and oxidise to glowing embers and ash. Their volatiles feed the gas's
flames, so the logs burn with flames of their own. The drawn mesh recedes with the wood, its char cracks along the
grain, and what is left falls as a heap of ash and charcoal. Later the campfire is out and the crate burns on.
Exposure is ×1.6.

<img src="showcase/burn_closeup.jpg" width="100%" alt="Close-up of the charring logs: alligator cracks along the grain">

A daylight close-up of the char's alligator cracks, with crumbled char and ash piling up below (`m26_f285`, without
the supersampling).

<img src="showcase/fire_night_campfire.jpg" width="49%" alt="A campfire at night"> <img src="showcase/loop_fire.webp" width="49%" alt="Orbiting the campfire">

Fire Lab's campfire. Fuel burns above its ignition temperature into heat, soot and expansion, and the flames glow by
their blackbody temperature. Only the 4³ bricks near smoke, heat or fuel are simulated. The ball, a physics body
circling through the smoke, is an obstacle the gas flows around. Exposure is ×2.5.

## Rain Lab at dusk: `FellowRain3D`

<img src="showcase/loop_rain.webp" width="100%" alt="Walking down a wet street at dusk in the rain">

## A thunderstorm in Rain Lab: `FellowLightning3D`

<img src="showcase/storm_square.jpg" width="100%" alt="A lightning bolt over Rain Lab's square, the flash lighting the rain and the wet ground">

<img src="showcase/loop_storm.webp" width="100%" alt="Walking down the street in a thunderstorm: flashes, then a bolt over the roofs">

<img src="showcase/storm_arcade.jpg" width="100%" alt="The arcade's columns casting the flash's shadows across its floor">

The same street at dusk with a `FellowLightning3D` (milestone 29). Each strike is a few return strokes in quick
succession: the flash lights the scene from the bolt's direction with shadows, so the arcade's columns throw them
across its floor. The bolt branches from the cloud and stays the same width on screen at any distance, and the wet
ground mirrors the flash. The captures strike in the camera's view two frames before the save
(`--weather_lightning_flash=2`); the loop strikes at two frames of its sequence.

Settings: night look, exposure ×2, the lightning's energy 6.

## Ray-traced GI in Sponza

<img src="showcase/sponza_rt.jpg" width="100%" alt="Intel New Sponza lit by the sun and ray-traced GI">

Intel New Sponza. The sun is at 58°, along the courtyard, and the ray-traced GI (DDGI), reflections, AO and sun
shadows are on.

## How they were made

Every capture uses `run_benchmarks.py --mode capture` (stills) or `--mode temporal` (loops) with `fellow214` and these
settings under `rendering/fellow/`:

* **Day**: `post/exposure/histogram`, `post/contact_shadows/enabled`, `post/atmosphere/enabled`,
  `post/clouds/enabled`, `post/sharpen/strength=0.3`, `post/bloom/enabled`, `post/light_shafts/intensity=1`, with
  FellowBench's `--sky=atmosphere` and the camera's auto exposure on.
* **Night**: `post/contact_shadows/enabled`, `post/sharpen/strength=0.3`, `post/bloom/enabled`,
  `post/bloom/lens_flare=1`, with a fixed exposure.
* **Ray tracing** (Bistro, Sponza): `raytracing/enabled`, `raytracing/gi`, `raytracing/reflections`,
  `raytracing/ambient_occlusion`, `raytracing/sun_shadows`.

FellowBench's options place the sun and the cameras: `--sun_elevation`, `--sun_azimuth`, `--sun_energy`,
`--add_camera`, `--add_seq`. Its options `--weather_rain` and `--weather_snow` add the rain and the snow to Bistro,
and `--set` supplies the 1.5× supersampling, the exposure and the sky's energy.

The benchmark assets keep their licenses: Intel New Sponza (CC BY 4.0, Intel) and Amazon Lumberyard Bistro (CC BY
4.0, Amazon Lumberyard / NVIDIA ORCA).
