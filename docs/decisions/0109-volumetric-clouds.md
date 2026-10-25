# 0109 — Volumetric clouds

Status: **KEEP as an option**, off by default — a visual feature (milestone 18) · Branch `fellow/geometry` · Settings
`rendering/fellow/post/clouds/enabled` (false), `coverage` (0.45), `density` (1), `altitude` (1.5 km), `thickness`
(2.5 km), `wind` (0.01 km/s) · Code: `effects/fellow_clouds.*`, `shaders/effects/fellow_clouds.glsl`,
`forward_clustered/render_forward_clustered.cpp` (`_fellow_process_clouds`).

## Problem

Godot's skies are a 2D picture (panorama, procedural, physical sky): no clouds with volume, lit by the sun from the
side, moving with the wind and seen in perspective over a city. Unreal (Volumetric Cloud) and Horizon Zero Dawn's
Nubis ray-march a layer of noise-shaped clouds.

## Change

A layer of clouds in a spherical shell (`altitude` above the ground, `thickness` deep) over a planet of the
atmosphere's radius (decision 0108), shaped by tileable 3D noise generated once on the GPU: a 128³ texture (Perlin
noise dilated by Worley noise — Schneider's Perlin-Worley — and three Worley octaves) for the shapes and a 32³
texture (Worley octaves) for the eroded edges. The density at a point (Nubis):

- **weather**: a 2D field (the shape noise at 0.4 × its frequency, about 16 km per period; the shapes repeat every
  6.25 km), spread to about [0, 1]; the clouds stand where it exceeds `1 − coverage` (so `coverage` is about the
  fraction of the sky covered), their local coverage rising from 0 at their rims to 1 in their middles;
- **shape**: the Perlin-Worley noise with its Worley octaves, under a height profile (flat bottoms within the lowest
  8 %, rounded tops at 30 % to 100 % of the layer, higher where the local coverage is higher: cumulus), remapped by the
  local coverage;
- **erosion**: the detail noise carves the edges (wispy at the bottoms, billowy at the tops); `density` scales the
  extinction (40 per km at 1).

Per frame, one ray per pixel at half resolution, 64 steps through the layer (up to 60 km), dithered per frame; at each
sample in a cloud six samples towards the sun (about 2 km, the shapes only) give the optical depth, and the light is
Wrenninge's multiple-scattering approximation (four octaves, each with 0.6 × the energy, 0.3 × the extinction and half
the phase's eccentricity) of a dual-lobe Henyey-Greenstein phase (g = 0.8 and −0.3), plus an isotropic ambient term: the
sky's light from above (from the atmosphere's sky-view table) and the sunlit ground's from below (its albedo). With the
atmosphere the sun reaches each sample through its transmittance (orange at sunset). Far clouds fade into the sky
(40 km). A temporal pass blends each frame's trace with the reprojected history (85 %, clamped to the frame's 3 × 3
neighbourhood), and a composite pass puts the clouds over the sky's pixels (depth at the far plane), before the
transparent pass. Needs a single view and no MSAA; the sky must be drawn. The history is the renderer's, not the
viewport's: several viewports with clouds share it (clamped, so only blurrier).

## Development

The first version shaped the clouds with the coverage applied to the 3D noise alone: the noise's spread is narrow (its
octaves average out to 0.68 ± 0.07), so any coverage above 0.5 filled the whole layer — an overcast — and the light
march of 1.2 km with three octaves left that overcast's underside nearly black (from the ground: 0.002-0.004 of the
sun's illuminance, darker than the blue sky). A CPU model of the shader (numpy, the same noise) reproduced it and
chose the weather field, the stretched noise, the longer light march, the fourth octave and the ground's light.

## Verification

Off (the default) the image is the previous build's bit for bit.

## Results

City Lab (lavapipe, 960 × 540, sun at 35°): separate cumulus over a blue sky at coverage 0.3, a broken layer at 0.6;
grey-white undersides, warm at a low sun. RTX cost: milestone 18's report.
