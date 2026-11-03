# 0139 — Wind-driven rain: the shelter follows the slant

Status: **KEEP** (`FellowRain3D.wind_driven` 1; below a drift of 0.2 m per metre of fall nothing changes) · Branch
`fellow/geometry` · Node: `FellowRain3D.wind_driven` · Code:
`servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` (`fellow_wetness_apply()`,
`fellow_wet_surface()`), `scene_forward_clustered_inc.glsl` (`FellowWeatherData::slant`), `effects/fellow_weather.{h,cpp}`
(`State::rain_drift`, `State::rain_slant`), `modules/fellow_fluid/fellow_rain_3d.{h,cpp}`.

## Problem

In a wind the drops fall slanted (they take the scene's wind, 0125), but the ground and the walls are wet where the
rain would reach falling straight down (0124's shelter heightfield): under an awning the dry patch sits right below it
whichever way the wind blows, a wall facing a storm under its eaves stays dry, and a wall is half as wet as the street
whatever it faces.

## Change

The rain node sends the drops' drift per metre of fall — the wind they were given (the scene's at the camera, its gust
included) over their speed — and how slanted the shelter is: none below a drift of 0.2 (a breeze of 1.8 m/s at the
default 9 m/s: the rain as good as vertical), all of it from 0.6 (5.4 m/s), times `wind_driven`; nothing when it
snows. The colour passes look for the rain along the slanted path as well: from the point (a wall, as before, from just
outside itself) back along the drift, up to 1 m and as far as the path rises 3 m, the heightfield there must be below
the height the drop had at that point. The shelter is the straight one mixed with the slanted one by the slant. A wall
facing the wind (the drift's opposite, from 0 to 0.6 of the cosine) is as wet as the ground instead of half as wet.
Under an awning the rain reaches in on the windward side and the lee stays dry beyond its edge; a facade facing the
storm gets wet under its eaves. One more lookup of the heightfield per pixel while it applies.

## Verification

* **Off is the previous image** (lavapipe, against milestone 38's build): Rain Lab raining without drops in its light
  wind (1.3 m/s: drift 0.15, nothing slanted; 5 images), the wind meadow under snow (3) — 0 pixels differing.
* **Lavapipe, the test scene** in the controller's rain (6 m/s, drift 0.67), the shelter (a roof of 3 × 2.5 m at
  2.2 m on posts) seen across the wind: with `wind_driven` 0 the dry ground right below the roof; with 1 the wet ground
  reaching under the roof on the windward side, the dry patch moved to the lee and past the roof's edge there, the
  posts' windward faces wet.

## Results

**Cost** (RTX 5070 Ti Laptop, 1080p, Bistro exterior in a FellowWeather3D storm — 12 m/s, fully slanted — straight
against slanted, 2 runs each, `m39_f313_wind_driven`, fellow234): the depth pre-pass 0.696 → 0.742 ms, the opaque pass
1.550 → 1.585 ms; the frame 4.908 → 4.968 ms (+0.06 ms).

**Bistro in the rain** (`m39_s23_bistro_wind_driven`: the controller's rain, 6 m/s, and storm, 12 m/s, the drops not
drawn, straight against slanted, five views; `bistro_wind_driven_ab.jpg`): the facades facing the wind darker and wet
under their awnings and balconies, the street's wet edges moved along the kerbs, the lee walls as before. In the storm
the lightning's flashes differ between the captures, so the rain's comparison is the cleaner one.

## Decision

**KEEP.**
