# FellowEngine — milestone 18 report (the look)

Status: **DONE** (`fellow190`-`fellow191`, laptop measurements `m18_f255`-`m18_f257`, 2026-10-25). Branch
`fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71: RTX 5070 Ti Laptop GPU, Core Ultra 9 275HX;
Windows 11). Decision records `docs/decisions/0105`-`0111`; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`
("Visual features").

## 0. Result

Seven visual features after Unreal Engine 5's, each an option under `rendering/fellow/post/*`, **off by default, and
off the image is the previous build's bit for bit** (lavapipe: City Lab, Crowd Lab, Lights Lab, 0 differing pixels;
PBR Lab's first camera differs by ±1-2 in up to 60 pixels even between two runs of the same build — the pipelines'
compile timing):

| setting | what it is | record |
|---|---|---|
| `post/exposure/histogram` | auto exposure from a histogram of log luminance (the mean between two percentiles), adapted exponentially in stops with separate speeds up and down — Unreal's eye adaptation | 0105 |
| `post/local_exposure/enabled` | Unreal 5's local exposure: a bilateral grid and a wide blur of log luminance; large-scale contrast compressed around middle grey, local detail kept | 0106 |
| `post/contact_shadows/enabled` | a short screen-space march towards the sun after the depth pre-pass, multiplied into the sun's shadow | 0107 |
| `post/atmosphere/enabled` | Hillaire's sky atmosphere (Unreal's SkyAtmosphere): transmittance, multiple scattering, sky-view and aerial perspective tables; the sky from a sky shader reading them, haze over distance, the sun's colour through the air | 0108 |
| `post/clouds/enabled` | volumetric cumulus (Nubis, Unreal's Volumetric Cloud) ray-marched at half resolution with temporal accumulation, lit by the sun through the atmosphere, the sky and the ground | 0109 |
| `post/sharpen/strength` | contrast-adaptive sharpening (AMD's RCAS) of the TAA output | 0110 |
| `post/bloom/lens_dirt` | the environment's glow map as a lens dirt mask added where the glow is (Unreal's bloom dirt) | 0111 |

GPU cost on the laptop at 1920 × 1080, each feature in one process against none (`m18_f255`/`m18_f256`, in-process
A/B, paired blocks; auto exposure on, the atmosphere's sky shader):

| feature | City Lab | Sponza | Bistro exterior |
|---|---:|---:|---:|
| none (ms per frame) | 2.18 | 6.40 | 4.32 |
| exposure: histogram + local | +0.01 (n.s.) | +0.03 (n.s.) | +0.03 |
| contact shadows | +0.22 | +0.34 | +0.29 |
| atmosphere + aerial perspective | +0.11 | +0.10 | +0.07 |
| clouds (with the atmosphere) | +0.63 | +0.62 | +0.52 |
| all seven | +0.88 (+40 %) | +1.01 (+16 %) | +0.86 (+20 %) |

The histogram's exposure pass is cheaper than stock's reduction (0.054 against 0.062 ms in City Lab); local exposure,
sharpening and lens dirt cost the tone mapping pass 0.06-0.07 ms together. After the second round (§4) the clouds cost
Sponza 0.19 ms and Bistro 0.22 ms instead of 0.62 and 0.52 (they are traced only where the sky is seen).

## 1. The features

**Exposure (0105, 0106).** One compute pass over the image, one workgroup per 32 × 32 cell, builds both the frame's
histogram (64 bins over [−14, 14] EV) and the local exposure's bilateral grid (32 slices of 1 EV per cell); a
64-thread pass adapts the exposure and writes it where stock's reduction does, so the tone mapper, glow and
`auto_exposure_scale` work unchanged. Local exposure is applied in the tone mapping pass before the exposure, in
specialized pipelines.

**Contact shadows (0107).** 16 steps over 0.3 m towards the first directional light, compared with the depth buffer; a
surface occludes when the march has just passed behind it (within two steps' change of depth, at least 2 cm), so the
march does not shadow behind everything seen in front of it. The mask is bound where the ray-traced sun shadows'
mask is.

**Sky atmosphere (0108).** The tables are per unit illuminance; the sun's illuminance is the colour passes' (×π
without physical light units), so the sky is as bright as the scene's sun implies. The sky itself is a sky shader
(`benchmarks/project/bench/fellow_atmosphere_sky.gdshader`) reading the tables through two global shader parameters, so
the sky also lights the ambient light and the reflections. The sun's light is tinted by the atmosphere's
transmittance from the camera (orange at sunset).

**Clouds (0109).** A spherical layer 1.5-4 km up; a 2D weather field decides where clouds stand (`coverage` is about the
fraction of the sky covered), 3D Perlin-Worley noise shapes them, Worley noise erodes their edges; 64 steps per ray,
6 towards the sun, four octaves of Wrenninge's multiple-scattering approximation, the sky's and the ground's light.

**Sharpening, lens dirt (0110, 0111).** Both in the tone mapping pass, specialization constants (0: the pipelines
without them).

## 2. Development

* **Clouds.** The first version applied the coverage to the 3D noise alone, whose octaves average out to 0.68 ± 0.07:
  any coverage above 0.5 filled the layer (an overcast), and its underside was nearly black (0.002-0.004 of the sun's
  illuminance, darker than the blue sky) — on lavapipe the "clouds" were a darker sky. A CPU model of the shader
  (numpy, the same noise) reproduced it and chose the weather field, the stretched noise, the longer light march, the
  fourth octave and the ground's light; separate cumulus with grey-white undersides followed.
* **Atmosphere against an independent integration.** The linear HDR capture's zenith radiance per unit illuminance
  (0.0065, 0.0136, 0.0314) against a single-scattering integration's (0.0041, 0.0084, 0.0173): the excess × 1.6-2.1 is
  the multiple scattering and the ground's light, both of about that size. The Mie absorption had been set to the whole
  extinction (10 × too much; fixed).
* **Contact shadows.** A fixed thickness of 0.3 m shadowed the characters' sunlit sides behind their own silhouettes;
  the tolerance of two steps' depth change fixed it.
* **Lens dirt** without a glow map would have brightened the glow: it now needs one.

## 3. The captures

`m18_f255_capture`, `m18_f255_city35`, `m18_f255_city10` (1920 × 1080, auto exposure on in both columns, the
default sky against the atmosphere's): City Lab gets a blue sky with cumulus at a 35° sun, warm clouds with bright rims
towards a 10° sun; Crowd Lab clouds and contact shadows at the feet; Bistro exterior and Sponza clouds over the
courtyards and haze. **The shade in Sponza and Bistro turned strongly blue** (the red awnings in shade purple): the
scenes' HDRI skies were grey, the atmosphere's is a clean deep blue that lights the ambient. §4.

## 4. Second round

* **The sky's colour (0108, `srgb_rayleigh`).** A spectral single-scattering sky (5 nm steps, a 5778 K sun, CIE 1931
  through the sRGB primaries) against RGB renderings: Hillaire's (and Unreal's) three wavelengths, 680/550/440 nm,
  leave the red 15-20 % low at every elevation; coefficients integrated over the sRGB channels, (7.190, 13.763, 32.519)
  × 10⁻³ per km, match the spectral colour within 1-2 %. They are the default (`srgb_rayleigh` false: Unreal's). The
  rest of the blue is the atmosphere's cleanliness: Hillaire's aerosol optical depth is 0.005, a typical day's
  0.05-0.15 (`mie_scale` 10-30); Crowd Lab's zenith 0.21 : 0.44 : 1 at `mie_scale` 1, 0.39 : 0.56 : 1 at 10.
* **Clouds (0109)** are traced only where the composite can show them (a sky pixel in the half-resolution pixel's
  footprint): Sponza traced every pixel for a strip of sky.
* **Contact shadows (0107)** march in clip space (one projection per pixel, the view depth from the inverse
  projection's z and w rows): the same image within rounding. Skipping surfaces facing away from the light (normal
  from the depth buffer) lost the shadows in the characters' creases and was dropped.

Laptop, `m18_f257` (`fellow191`, the same A/B with none, contact shadows, the atmosphere, the clouds with the
atmosphere; ms per frame of GPU time, the passes' own times in brackets):

| | City Lab | Sponza | Bistro exterior |
|---|---:|---:|---:|
| contact shadows | +0.23 (0.171, was 0.184) | +0.26 (0.265, was 0.293) | +0.25 (0.264, was 0.257) |
| atmosphere | +0.12 | +0.10 | +0.10 |
| clouds with the atmosphere | +0.62 (clouds 0.455, was 0.530) | **+0.19** (0.107, was 0.609) | **+0.22** (0.151, was 0.496) |

The clouds' pass now costs what the sky in view needs: a fifth of before in Sponza's courtyard, a third in Bistro's
streets. The clip-space march saves contact shadows 0-10 % (their cost is the 16 depth fetches per pixel).

Captures `m18_f257_capture` (Sponza, Bistro exterior; every feature, the sRGB-integrated sky, and the same with
`mie_scale` 10): the sRGB coefficients are a small change (the zenith slightly less saturated); a typical day's haze
(`mie_scale` 10) turns Sponza's sky-lit floor from blue to blue-grey and Bistro's shaded streets less blue. The default
stays Unreal's clean atmosphere (`mie_scale` 1); `docs/FELLOWENGINE_OPTIONS.md` names 10-30 for a typical day.

## 5. Open

* **An Unreal-like bloom.** Lens dirt multiplies the glow, and Godot's glow of the sun is weak: a threshold, a
  luminance cap (12; 256 changed little), the levels' spread — with the atmosphere's sun in clear view the dirt adds at
  most 27 levels to 1 700 pixels (lavapipe). Unreal's bloom convolves the whole image at six sizes without threshold or
  cap; a bloom of that kind (energy-conserving, the sun's disk at full radiance) is the missing half of 0111.
* The clouds' edges are soft (half resolution, temporal blending); their history and the exposure's textures are the
  renderer's, not the viewport's (several viewports share them).
* The ozone's coefficients are still the three wavelengths'.
* No white balance: a scene lit only by a clear sky is as blue as the sky.
* PBR Lab's first camera varies by ±1-2 between runs (the pipelines' compile timing), with or without this milestone.

## 6. Next

The bloom under the lens dirt (above); white balance (a temperature and tint in the tone mapping pass, as Unreal's);
or other work, as the user decides.
