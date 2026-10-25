# 0118 — Gas rendering: smoke and fire in the Forward+ renderer

Status: **KEEP as a feature** (milestone 20; draws the `FellowGasVolume3D` nodes, decision 0117) · Branch
`fellow/geometry` · Setting `rendering/fellow/fluid/gas_rendering` (true) · Code:
`servers/rendering/renderer_rd/effects/fellow_gas.*`, `shaders/effects/fellow_gas_render.glsl`,
`forward_clustered/render_forward_clustered.cpp` (`_fellow_begin_gas`, `_fellow_gas_shadow`, the composite after the
clouds).

## Problem

The simulation (0117) gives a 3D field of smoke density and temperature; the scene needs it drawn as a participating
medium (lit by the sun and the sky, shadowing itself), the fire glowing as hot gas does, the smoke's shadow on the
ground, and the fire's light on what is around it — at a cost a frame can afford with several volumes on screen.

## Change

Per render (a single view, no MSAA, not a reflection probe), for at most 8 volumes (the nearest), in four kinds of
compute pass:

- **Light** (per volume, when its step or the sun changed): the optical depth from each cell of a half-resolution grid
  to the box's exit towards the sun, marched in 1.5-cell steps with the empty bricks (0117's 4³ maxima) skipped to
  their exits. It shadows the smoke itself and gives the shadow on the scene.
- **Shadow** (per pixel, after the depth pre-pass, where the sun casts shadows): the sun's ray from the pixel's
  position into each volume's box; the optical depth at its entry point is the smoke the ray crosses. The
  transmittance multiplies the contact shadow mask (decision 0107), which the opaque passes multiply the sun's shadow
  by; without contact shadows the pass makes the mask. Not with the ray-traced sun shadows (0082).
- **Trace** (per pixel at half resolution, over the screen rectangle the boxes cover): the camera's ray through every
  box it crosses, front to back, up to the depth buffer, in steps of `render_step` cells with the start dithered per
  pixel and frame (TAA averages it) and the empty bricks skipped; at each sample the smoke's extinction (`smoke_density`
  per unit of smoke per metre) and in-scattering: the sun through the light grid with Wrenninge's multiple scattering
  (four octaves of a Henyey-Greenstein phase, `anisotropy`), darkened where the scene shadows it (the sun's shadow
  map, sampled as Godot's volumetric fog samples it: the cascade by view depth, a soft exponential comparison; setting
  `fluid/gas_scene_shadows`), the environment's ambient light (as the scene shader and
  DDGI take it: the sky's radiance from six directions, mostly from above), the fire light's (below) without
  shadowing; and the fire's emission: radiance per metre = `fire_intensity` × (T / 1500 K)⁴, visible from 700 K
  (a smoothstep to 1000 K), coloured as a blackbody (Krystek's Planckian locus to sRGB) at T × `fire_temperature_scale`
  (1.4: a flame's glow is yellower than its gas temperature's blackbody). Energy-conserving integration per step.
- **Composite** (per pixel over the rectangle): the half-resolution result upsampled with the four nearest samples
  weighted bilinearly and by how close the depth each was traced to is to the pixel's (silhouettes in front of the
  smoke stay sharp), over the colour buffer, before the transparent pass and the screen texture's copy (refraction sees
  the smoke behind).

**The fire light.** The simulation sums the fire's emission per workgroup (0117); read back asynchronously, its total
and centroid place and colour an `OmniLight3D` the node owns (an internal child): energy = the radiant intensity / π
(the scene's Lambert surfaces), inverse-square attenuation, a range where it falls below 0.004 (2-60 m) unless
`fire_light_range` is set, its flicker the simulation's. The smoke is lit by it too (the trace).

## Limits

Transparent objects behind the smoke are drawn over it (the composite is before the transparent pass); the smoke is
not in the volumetric fog; one view (no XR), no MSAA. (Since added: the scene's shadows on the smoke; the world-space
volume, 0117.)

## Verification

Without a volume, or with `gas_rendering` off, nothing of it runs: the frame is the previous build's bit for bit
(lavapipe: City Lab, Crowd Lab, Lights Lab, City Lab with auto exposure and the dirt map, against milestone 19's build).

Fire Lab with the sun's shadow map on the smoke against without (lavapipe, 960 × 540): the shadow of the sphere
circling through the campfire's smoke a darker channel through it (32 000 pixels changed, by up to 68 levels, under
the sphere's camera), the logs' shadows in the flames' smoke at the bottom.

## Results

Fire Lab on lavapipe (960 × 540): flames orange to yellow at their cores, red at their tips, the logs and stones lit
orange by the fire light (at night the only light), the smoke's shadow on the ground beside the fire, a sunlit white
plume from the chimney. RTX: the milestone report.
