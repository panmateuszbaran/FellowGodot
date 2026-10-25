# 0108 — Sky atmosphere and aerial perspective (Hillaire, Unreal's SkyAtmosphere)

Status: **KEEP as an option**, off by default — a visual feature (milestone 18) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/atmosphere/enabled` (false), `aerial_perspective` (true), `sun_transmittance` (true),
`aerial_distance_scale` (1), `mie_scale` (1), `rayleigh_scale` (1), `ground_albedo` (0.3), `srgb_rayleigh` (true) · Code:
`effects/fellow_atmosphere.*`, `shaders/effects/fellow_atmosphere.glsl`,
`forward_clustered/render_forward_clustered.cpp` (`_fellow_update_atmosphere`), `storage_rd/light_storage.*` (the sun's
tint); bench `bench/fellow_atmosphere_sky.gdshader`, `--sky=atmosphere`, `--sun_elevation`.

## Problem

Godot's sky materials are separate from the scene's lighting: the procedural and physical skies are a gradient and a
single-scattering approximation for the background only; nothing in the scene gets bluer and fainter with distance
(fog is a uniform colour), the sun keeps its colour at sunset unless the project animates it, and the sky does not
follow from the sun's position and the air. Unreal's SkyAtmosphere (Hillaire, "A Scalable and Production Ready Sky and
Atmosphere Rendering Technique", EGSR 2020) computes the sky, the sun's colour and the aerial perspective from one
physical model of the atmosphere.

## Change

Hillaire's method with his (and Unreal's) Earth defaults: a ground sphere of 6360 km, an atmosphere 100 km deep,
Rayleigh scattering (5.802, 13.558, 33.1) × 10⁻³ per km with an 8 km scale height, Mie scattering 3.996 × 10⁻³ and
absorption 0.444 × 10⁻³ per km with 1.2 km (Cornette-Shanks phase, g = 0.8), an ozone layer (absorption
(0.650, 1.881, 0.085) × 10⁻³ per km, a tent 30 km wide at 25 km), the ground's albedo. Compute passes, for a sun of
illuminance 1:

- the **transmittance** to the atmosphere's top by height and zenith angle (256 × 64, Bruneton's mapping) and the
  **multiple-scattering** factor by height and sun angle (32 × 32, Hillaire's isotropic infinite-order term), once and
  again when the settings change;
- per frame, the **sky-view** table (192 × 108: the in-scattered radiance by azimuth from the sun and elevation, denser
  at the horizon; below it the ground lit by the sun and the sky) and the **aerial perspective** froxels (32 × 32 × 32
  over the camera's frustum, slices squared up to 32 km: in-scattering and transmittance from the camera);
- an **apply** pass on the opaque surfaces (before the transparent pass): colour × transmittance + in-scattering ×
  the sun's illuminance, at the pixel's distance × `aerial_distance_scale`.

The sun is the first directional light that lights the scene (not a sky-only one); its illuminance is the colour
passes': energy × intensity with physical light units, energy × π without (the tables are per unit illuminance). With
`sun_transmittance` the light's colour is tinted by the atmosphere's transmittance from the camera towards the sun
(computed on the CPU: yellow-orange at sunset, dark below the horizon) — Unreal's "atmosphere sun light". The sky
itself is a sky shader (the bench's `fellow_atmosphere_sky.gdshader`) reading the tables through two global shader
parameters (`fellow_atmosphere_sky_view`, `fellow_atmosphere_transmittance`, sampler2D, declared in the project) plus
the sun's limb-darkened disk; as a sky it lights the scene's ambient and reflections too. Needs a single view; the
aerial perspective also no MSAA.

## Verification

Off (the default) the image is the previous build's bit for bit.

## Results

The tables against an independent numerical integration (Python, the same coefficients, single scattering only): at a
sun 45° high, the zenith's radiance per unit illuminance from the linear HDR capture is (0.0065, 0.0136, 0.0314) against
single scattering's (0.0041, 0.0084, 0.0173); 10° above the horizon, 125° from the sun's azimuth, (0.024, 0.045, 0.080)
against (0.014, 0.025, 0.038). The excess (× 1.6-2.1, the most in blue) is the multiple scattering and the light the
ground reflects (albedo 0.3), each of about that size by estimate; the shapes (the horizon brighter, whiter) agree. A
first version (the one measured) added the Mie absorption as the whole extinction (4.4 × 10⁻³ instead of 0.444 × 10⁻³
per km, an optical depth of 0.005 too much); fixed. The sky is physically consistent with the scene's sun: with the
bench's sun of energy 2 and exposure 1, a sunlit wall is about 15 × brighter than the zenith (a clean atmosphere;
`mie_scale` 2-4 gives a hazier, brighter sky).

**The sky's colour (`srgb_rayleigh`).** The first RTX captures (milestone 18's report) showed the shade in Sponza and
Bistro strongly blue — the red awnings in shade purple. Part of it is Hillaire's (and Unreal's) three Rayleigh
coefficients: λ⁻⁴ sampled at 680, 550 and 440 nm. A spectral single-scattering sky (5 nm steps, a 5778 K sun, CIE 1931
through the sRGB primaries; Python) against RGB renderings of the same integral, as the ratio red : green : blue:

| elevation | spectral | Hillaire's coefficients | sRGB-integrated |
|---|---|---|---|
| 90° | 0.293 : 0.520 : 1 | 0.242 : 0.503 : 1 | 0.295 : 0.516 : 1 |
| 30° | 0.312 : 0.553 : 1 | 0.256 : 0.535 : 1 | 0.314 : 0.547 : 1 |
| 10° | 0.428 : 0.694 : 1 | 0.360 : 0.679 : 1 | 0.430 : 0.687 : 1 |
| 3° | 0.745 : 0.976 : 1 | 0.647 : 0.978 : 1 | 0.734 : 0.978 : 1 |

(sun 45° high, `mie_scale` 1; with 10 the same.) Three wavelengths leave the red 15-20 % low everywhere; the
sRGB-integrated coefficients — λ⁻⁴ × the sun's spectrum against each channel's colour matching function, divided by the
sun's own response: (7.190, 13.763, 32.519) × 10⁻³ per km — reproduce the spectral colour within 1-2 %. They are the
default; `srgb_rayleigh` false gives Unreal's. The rest of the blue is the atmosphere's cleanliness: Hillaire's aerosols
are an optical depth of 0.005 at 550 nm, a typical day's 0.05-0.15 (`mie_scale` 10-30). Crowd Lab's linear captures
(lavapipe): the zenith's ratio 0.21 : 0.44 : 1 at `mie_scale` 1, 0.28 : 0.49 at 4, 0.39 : 0.56 at 10, 0.52 : 0.65 at 20.
The ozone's coefficients are still Hillaire's three wavelengths (no spectrum at hand).

City Lab (lavapipe, 960 × 540): a blue sky brightening to white at the horizon at a high sun, orange at 3°, the city's
far blocks bluish in the haze, the sunlit walls orange-tinted at a low sun. RTX cost: milestone 18's report.
