# FellowEngine — milestone 47 report (FSR 3.1 upscaler)

Status: **measured on lavapipe** (the cloud's CPU Vulkan driver, 2026-11-04), **and its cost on the RTX** (milestone
49's jobs f335-f337, 2026-11-05).
Branch `fellow/geometry`. Decision record `docs/decisions/0148-fsr3-upscaler.md`.

## 0. Result

`rendering/fellow/upscaling/fsr3`: a viewport in Godot's FSR 2 scaling mode runs AMD's FidelityFX Super Resolution
3.1 upscaler instead of stock's 2.2, built from the FidelityFX SDK's source (MIT) on the RenderingDevice. In PBR Lab's
moving sequences at 960 × 540 against an accumulated ground truth it is better than FSR 2.2 in every sequence and
every mode measured — at 2× upscaling **−9 to −24 % error where things move, −10 to −14 % flicker, +0.6 to +1.0 dB**;
in quality mode (1.5×) +0.7 to +0.9 dB, within 0.4-1.1 dB of the native-resolution TAA at 44 % of its pixels; as native
AA +1.3 dB over TAA in three of four sequences. On still views at 2× it is softer than FSR 2.2 (−1.3 dB in two of
three views; FSR 2.2's sharper image aliases). It uses 8.5 MiB less memory at that size and adds no Vulkan validation
message. On the RTX it costs what FSR 2.2 costs (Bistro exterior and Sponza at 1080p, within 0.7 %) — after a crash
there, an ODR violation between the two SDKs, was fixed — and it is **on by default from milestone 49**.

## 1. Development

* **The SDK** (`thirdparty/amd-fsr3`, FidelityFX SDK 1.1.4): the upscaler's host code, GPU headers and the Vulkan
  backend's GLSL passes; patches for Godot's shader builder (relative includes, `#version`, FSR 1's header included
  once in the RCAS pass), for GCC/Clang and 4-byte `wchar_t` (the context's size), 16 binding slots instead of 64 in
  its job descriptions (the render thread's recording 0.213 → 0.083 ms), the luma history's storage format, and FSR
  2.2's Godot options (motion vectors from the depth where the velocity buffer has none, the reactive mask clamp).
* **The backend** (`servers/rendering/renderer_rd/effects/fsr3.{h,cpp}`): the SDK's `FfxInterface` on RD — textures,
  registered inputs, mip views for storage images, pipelines with bindings by name, constant buffers in sets of
  uniform buffers that turn over, clears, copies and compute lists with uniform sets from the cache; the three
  resources FSR 3.1 shares with frame generation are the context's. Opaque to the rest of the engine (the SDK's types
  share FSR 2.2's names).
* **The shaders** (`shaders/effects/fsr3/*.glsl`, ten wrappers): the SDK's GLSL with its Vulkan build's options and the
  context's (HDR, render-resolution motion vectors, inverted depth), half precision where the GPU has it (not SPD's
  pyramids: their 16-bit subgroup operations need a feature RD does not enable).
* **The renderer** (`render_forward_clustered.cpp`): the FSR 2 mode's dispatch runs FSR 3.1 with the setting on;
  frame time in milliseconds; the SDK's tunables and debug view as settings.
* **FellowBench** (`tools/lavapipe/upscaler_check.sh`): a ground truth from jittered renders, TAA and the two
  upscalers at 0.5, 0.667 and 1.0 through `temporal.py`, the fixed cameras' stills.

## 2. Verification

* Vulkan's validation layer on lavapipe: no message from FSR 3.1 (36 barrier messages in both FSR runs are Godot's);
  the 16-bit SPD pyramids' 12 messages led to their full precision.
* Half against full precision: 8.6 % of the pixels differ, by 0.05 levels in the mean.
* The debug view: zero motion vectors on a still view, the depth, the reactive mask on the transparent panels.
* Run to run, the stills differ in a few thousand pixels (pipeline compilation; FSR 2.2 alike), their PSNRs within
  0.01 dB.

## 3. Image quality

PBR Lab, 960 × 540, four sequences of 60 frames (a camera pan, a strafe past grass and a fence, a box sweeping across
spheres, a dolly towards thin wires); ground truth 8 jittered renders a frame without TAA.

| | TAA (native) | FSR 2.2 0.5 | **FSR 3.1 0.5** | FSR 2.2 0.667 | **FSR 3.1 0.667** | FSR 2.2 1.0 | **FSR 3.1 1.0** |
|---|---:|---:|---:|---:|---:|---:|---:|
| PSNR, camera pan | 35.97 | 33.47 | **34.06** | 34.83 | **35.50** | 35.96 | **37.27** |
| PSNR, foliage strafe | 30.13 | 27.28 | **28.26** | 28.84 | **29.72** | 31.26 | **31.44** |
| PSNR, moving object | 38.39 | 34.66 | **35.50** | 36.46 | **37.27** | **38.20** | 38.10 |
| PSNR, thin dolly | 32.94 | 30.01 | **30.89** | 31.65 | **32.40** | 33.87 | **33.89** |
| flicker, foliage strafe | 0.01718 | 0.02383 | **0.02055** | 0.01981 | **0.01729** | 0.01532 | **0.01412** |
| error in motion, moving object | 0.01146 | 0.02282 | **0.01742** | 0.01714 | **0.01397** | **0.00913** | 0.01034 |
| still PSNR, overview | 39.20 | **36.27** | 34.97 | **37.28** | 36.98 | 37.32 | **37.60** |
| still PSNR, thin | 37.65 | **35.58** | 34.25 | **36.43** | 36.38 | 36.12 | **36.38** |
| still PSNR, specular | 30.14 | **26.50** | 26.43 | **28.28** | 28.00 | 33.48 | **34.55** |

All metrics per sequence are in the decision record. At 2× FSR 3.1 reconstructs the chain-link fence that FSR 2.2
turns into a pattern of crosses, and keeps the far wires continuous but fainter; its still image is softer (gradient
energy 0.0165, the ground truth's 0.0177, FSR 2.2's 0.0190).

## 4. Performance and memory

* Memory (960 × 540, scale 0.5): all textures 207.2 MiB with FSR 3.1, 215.7 MiB with FSR 2.2.
* lavapipe (A/B blocks in one run, `--profile`): the upscaler's passes 81.7 ms against FSR 2.2's 98.3 ms of the CPU
  rasterizer's time — the shaders' work ranked, not a GPU's time; recording 0.083 against 0.074 ms.
* On the RTX (1080p output, 2 runs): Bistro exterior TAA 4.13 ms; FSR 2.2 / 3.1 at 50 % 2.72 / 2.72, at 67 % 3.20 /
  3.22, at 100 % 4.71 / 4.75 ms. Sponza TAA 5.96 ms; 3.56 / 3.57, 4.36 / 4.37, 6.58 / 6.55 ms. Not measured: 1440p
  and 4K, half precision's gain alone.
* Found on the RTX: FSR 3.1 crashed at its first frame (heap corruption). FSR 2.2's and FSR 3.1's SDKs each define
  `FfxResourceDescription` (28 and 32 bytes), and `LocalVector<FfxResourceDescription>` in both backends was one
  template to the linker; FSR 2.2's growth allocated FSR 3.1's table. Found with AddressSanitizer, fixed with a type of
  FSR 3.1's backend's own (decision 0148).

## 5. Limits

* Godot's FSR 2 scaling mode is the switch: there is no separate mode in the Viewport's enum.
* No reset on camera cuts (stock has none for FSR 2.2), no generated reactive mask, no transparency and composition
  mask.
* Frame generation and FSR 3's frame interpolation are not ported; DLSS and XeSS are not integrated (vendor binaries,
  their hardware to test).

## 6. Next

* Done in milestone 49: the cost on the RTX at 1080p (FSR 2.2's), and the default (on). Still to measure: 1440p and
  4K, half precision's gain.
* DLSS (Streamline or the DLSS SDK) and XeSS behind the same mode; FSR 3 frame generation with a present path.
* The agreed order continues with animation.
