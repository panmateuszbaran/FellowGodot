# Offline reference (Blender Cycles) for the PBR Validation Lab

The lab is exported by the benchmark project itself, so geometry, UVs, normals, material parameters, textures,
light and cameras are *identical* by construction:

```bat
REM 1) export (reference lighting: uniform sky radiance + sun only)
.tools\bin\stock\godot.windows.template_release.x86_64.console.exe --path benchmarks\project --fixed-fps 60 ^
    --resolution 1920x1080 --position 0,0 -- --scene=pbr --mode=capture --lighting=ref --hdr=1 --export-ref=<dir> --out=<dir>
REM 2) path trace (GPU, OptiX)
blender --background --factory-startup --python benchmarks\reference\pbr_lab_cycles.py -- ^
    --scene <dir>\pbr_lab_scene.json --out <dir> --samples 2048 --res 1920x1080
```

`run_suite.py baseline` performs step 1 (session `<tag>_capture_pbrref`, export in `<tag>_pbrref_export`);
step 2 is run separately because it needs Blender (see the milestone report for the exact command used).

## Equivalences
| Quantity | Godot (non-physical light units) | Cycles |
|---|---|---|
| Sun | energy E, colour C → irradiance π·E·C | Sun strength = π·E (W/m²), colour C, angle 0.5° |
| Sky | uniform panorama radiance L (energy 1) | World background colour L, strength 1 |
| Base colour | sRGB-encoded `albedo_color` = linear_to_srgb(ρ) | Principled Base Color ρ (linear) |
| Specular | `metallic_specular` 0.5 → F0 = 0.04 | IOR 1.5 → F0 = 0.04 |
| Diffuse | Lambert (lab materials set `diffuse_mode = LAMBERT`) | Principled v2 diffuse (Lambert) |
| Emission | colour × energy (radiance) | Emission colour × strength (radiance) |
| Alpha blend | albedo alpha | Principled Alpha |
| Alpha scissor | threshold 0.5 | Math GREATER_THAN 0.5 → Alpha |
| Camera | vertical FOV, same pose | sensor fit VERTICAL, same angle / pose |
| Output | linear EXR before tonemapping (`--hdr=1`: HDR 2D viewport, linear tonemapper, glow off) | 32-bit EXR, Standard view transform |

Both EXRs pass through the *same* neutral display transform (ACES fit + sRGB) before PSNR/SSIM are computed
(`benchmarks/tools/imageutil.py: tonemap_display`).

## Differences that are expected (not rendering bugs)
* Cycles computes full multi-bounce global illumination; the Godot preset uses sky ambient + SSAO only
  (no SDFGI / VoxelGI / lightmaps). The dark room interior and contact regions will differ most.
* Emissive bars illuminate their surroundings in Cycles only.
* Transparent panes: Principled alpha in both, no refraction in either.
* Godot's specular IBL comes from a filtered radiance map with split-sum approximation; Cycles integrates it.
* Cycles applies a 1.5 px Blackman-Harris pixel filter; Godot's capture here uses TAA (preset `high`).

The comparison is used to spot gross errors (energy scale, Fresnel, roughness mapping, normal-map orientation,
shadow bias/leaks), not to optimise an image metric.
