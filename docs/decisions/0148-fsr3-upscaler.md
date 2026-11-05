# 0148 — FSR 3.1 upscaler on RenderingDevice

Status: **KEEP** (`rendering/fellow/upscaling/fsr3`, on by default from milestone 49, after the RTX) · Branch
`fellow/geometry` · `servers/rendering/renderer_rd/effects/fsr3.{h,cpp}`, `shaders/effects/fsr3/*.glsl`,
`render_forward_clustered.cpp` (the FSR 2 scaling mode's dispatch) · `thirdparty/amd-fsr3` (AMD FidelityFX SDK 1.1.4,
MIT; patches `0001`, `0002`) · Settings `rendering/fellow/upscaling/*` · Bench: `tools/lavapipe/upscaler_check.sh`.

## Problem

Godot's only temporal upscaler on Vulkan is AMD's FSR 2.2 (2023); DLSS, XeSS, FSR 3+ and frame generation are absent
(`docs/fellow_aaa_readiness.md`: "Upscaling, frame generation, HDR — Gap"). An AAA project at 1440p-4K renders at a
fraction of the output resolution and depends on the upscaler for what the player sees. FSR 3.1 (2024) is AMD's
current upscaler, rewritten since 2.2 (new reactivity, shading change detection, luma instability, a new accumulation)
and published with its Vulkan GLSL under MIT: of the modern upscalers, the one that runs on every GPU and can be built
from source into the engine. DLSS and XeSS ship as vendor binaries and need their hardware to be tested.

## Change

**The SDK.** The upscaler's host code (`ffx_fsr3upscaler.cpp`), its GPU headers and the Vulkan backend's GLSL passes
from FidelityFX SDK 1.1.4. Patches: includes between the GPU folders made relative (Godot's shader builder resolves
includes from the including file); GCC/Clang build fixes; a context twice the SDK's size where `wchar_t` has 4 bytes
(the private context holds twelve pipeline states with `wchar_t` names: the SDK's static assertion fails on Linux);
16 instead of 64 SRV/UAV slots in a pipeline state (each dispatch zeroes and copies three job descriptions: 18 instead
of 69 KB each); `#version` left to Godot's wrappers; FSR 1's header included once in the RCAS pass (Godot's builder
includes a file where it first appears, there in a branch the preprocessor leaves out, and RCAS failed to compile); the
luma history's storage-image format `rgba16f` as its texture (the GLSL says `rgba8` for an R16G16B16A16_FLOAT
texture); and FSR 2.2's Godot options — the reprojection matrix in the constants, from which a pixel without a motion
vector (the sky) gets one from its depth, and the reactive mask clamped at 0.9.

**The backend** (`effects/fsr3.cpp`, an `FfxInterface` on RenderingDevice). Resources: textures created with RD
(initial data from the SDK's buffers or values), the game's textures registered for a dispatch, a storage image of a
texture with mips bound one level at a time; the three resources FSR 3.1 shares with frame generation (dilated depth,
dilated motion vectors, reconstructed previous depth) are the context's own. Pipelines: the SDK asks for each pass's
pipeline with permutation flags; the backend fills the bindings by name (Godot has no reflection of names: the tables
are written from the passes' `FSR3UPSCALER_BIND_*` defines and the callbacks' names) and builds the RD pipeline on first
use, half precision or not, sharpening or not. Constant buffers: staged per dispatch into one set of uniform buffers of
eight that turn over. Jobs: clears with `texture_clear`, copies with `texture_copy`, a compute job as one compute list
with its uniform set from the uniform set cache (set 0 holds the textures, the images, the buffers and the two
samplers at bindings 1000-1001); discards and barriers are RD's own. The SDK's types share their names with FSR 2.2's
and differ, so `fsr3.h` is opaque and only `fsr3.cpp` includes the SDK; FSR 2.2's `ffx_assert.cpp` serves both.

**The shaders** are the SDK's GLSL built with the options its Vulkan build uses (half-precision reprojection samplers,
the approximated Lanczos upsampling, the reference Lanczos for reprojection except where the GPU's waves are 32 and 64
wide) and the context's (HDR colour, motion vectors at the render resolution without jitter, inverted depth). Half
precision where the GPU has it (`fsr3_half_precision`, on), except SPD's two pyramids — their 16-bit path uses subgroup
operations on half floats, a feature RD does not enable (`shaderSubgroupExtendedTypes`; Vulkan's validation reported
it) — and the accumulation on NVIDIA, as FSR 2.2's port does.

**In the renderer.** With `rendering/fellow/upscaling/fsr3` on, a viewport in Godot's FSR 2 scaling mode runs FSR 3.1
in its place: the same inputs (colour, depth, velocity, the reactive mask from the colour's alpha, exposure when auto
exposure is on), jitter, sharpness (`fsr_sharpness`), render scale and output; the frame time in milliseconds (stock
passes FSR 2.2 seconds). The effect is created on first use. `fsr3_debug_view` draws the SDK's debug view; the SDK's
five tunables are settings (`fsr3_velocity_factor`, `fsr3_reactiveness_scale`, `fsr3_shading_change_scale`,
`fsr3_accumulation_added_per_frame`, `fsr3_min_disocclusion_accumulation`). Not used: the reactive mask generation
(Godot renders no opaque-only colour), the transparency and composition mask, a reset on camera cuts (Godot has none
for FSR 2.2 either).

## Verification

lavapipe with Vulkan's validation layer (`--gpu-validation`): FSR 3.1 adds no validation message (FSR 2.2's run has 36
`VUID-vkCmdPipelineBarrier-srcAccessMask-02815`, both runs the same); the 16-bit SPD pyramids' 12 messages led to
their full precision. Half against full precision: 8.6 % of the pixels differ, by 0.05 levels in the mean (480 × 270).
The SDK's debug view: still motion vectors zero, the depth, the reactive mask on the transparent panels.

## Results

lavapipe, PBR Lab at 960 × 540, the sequences of `temporal.py` (60 frames each after a 32-frame pre-roll) against a
ground truth of 8 jittered renders a frame without TAA; the fixed cameras' stills after 96 frames
(`tools/lavapipe/upscaler_check.sh`). TAA is FellowEngine's (resolve v2) at the native resolution.

Performance mode (render scale 0.5, 480 × 270 → 960 × 540):

| sequence | error in motion FSR 2.2 → 3.1 | flicker FSR 2.2 → 3.1 | PSNR FSR 2.2 → 3.1 | SSIM FSR 2.2 → 3.1 |
|---|---:|---:|---:|---:|
| camera pan | 0.01156 → **0.01057** (−9 %) | 0.00987 → **0.00887** (−10 %) | 33.47 → **34.06** dB | 0.949 → **0.954** |
| foliage strafe | 0.02827 → **0.02469** (−13 %) | 0.02383 → **0.02055** (−14 %) | 27.28 → **28.26** dB | 0.856 → **0.881** |
| moving object | 0.02282 → **0.01742** (−24 %) | 0.00155 → **0.00133** (−14 %) | 34.66 → **35.50** dB | 0.958 → **0.963** |
| thin dolly | 0.02972 → **0.02662** (−10 %) | 0.01166 → **0.01051** (−10 %) | 30.01 → **30.89** dB | 0.914 → **0.926** |

Quality mode (0.667, 640 × 360): error in motion −7 to −19 %, flicker −8 to −14 %, PSNR +0.67 to +0.88 dB in all four
(FSR 3.1: 35.50, 29.72, 37.27, 32.40 dB; FellowEngine's TAA at the native resolution: 35.97, 30.13, 38.39, 32.94 dB —
within 0.4-1.1 dB at 44 % of the pixels). Native AA (1.0): flicker −1 to −31 %, PSNR +1.31, +0.18, −0.10, +0.02 dB
(against TAA: +1.30, +1.31, −0.29, +0.95 dB, flicker −7 to −18 %).

The stills (PSNR against their ground truth): in performance mode FSR 3.1 is softer than FSR 2.2 — 34.97 against
36.27 dB and 34.25 against 35.58 dB in two views, 26.43 against 26.50 dB in the third; its gradient energy is 0.0165
where the ground truth's is 0.0177 and FSR 2.2's 0.0190 (sharpened, with aliasing: the chain-link fence becomes a
pattern of crosses, FSR 3.1 reconstructs its diamonds). In quality mode −0.05 to −0.30 dB; at the native resolution
FSR 3.1 is ahead, +0.26 to +1.07 dB. Run to run the stills differ in a few thousand pixels (pipeline compilation);
their PSNRs within 0.01 dB.

Memory at 960 × 540 (scale 0.5): all textures 207.2 MiB with FSR 3.1, 215.7 MiB with FSR 2.2 (−8.5 MiB).

Cost on lavapipe (960 × 540, render scale 0.5, the two in alternating blocks of one run, `--profile`): the upscaler's
passes 81.7 ms against FSR 2.2's 98.3 ms (lavapipe runs the compute shaders on the CPU: the numbers rank the shaders'
work, not a GPU's time), recording 0.083 against 0.074 ms of the render thread (0.213 ms before the SDK's job
descriptions were cut to 16 slots).

**On the RTX** (milestone 49, jobs f335-f337; RTX 5070 Ti Laptop, Windows, 1920 × 1080 output, autotune off, 2 runs,
frame time):

| | TAA (native) | FSR 2.2 50 % | FSR 3.1 50 % | FSR 2.2 67 % | FSR 3.1 67 % | FSR 2.2 100 % | FSR 3.1 100 % |
|---|---:|---:|---:|---:|---:|---:|---:|
| Bistro exterior | 4.126 ms | 2.716 | **2.719** | 3.199 | **3.221** | 4.711 | **4.748** |
| Sponza | 5.962 ms | 3.564 | **3.570** | 4.363 | **4.366** | 6.584 | **6.554** |

FSR 3.1 costs what FSR 2.2 costs, within 0.7 % either way: at half the pixels −34 to −40 % against native TAA, at 67 %
−22 to −27 %, as native AA +10 to +15 % (0.6 ms more than TAA's resolve at 1080p). Not measured: 1440p and 4K
output.

It first crashed there, at its first frame, in every run (heap corruption; nothing on lavapipe, under Wine or with
clang): FSR 2.2's SDK (stock's FSR 2 backend) and FSR 3.1's each define `FfxResourceDescription` (28 and 32 bytes),
and both backends kept a `LocalVector<FfxResourceDescription>` — one template to the linker, an ODR violation that let
FSR 2.2's `reserve()` allocate FSR 3.1's resource table too small. AddressSanitizer (clang 18, lavapipe) showed the
overflow at the context's creation; FSR 3.1's table now has an element type of its own (commit `9717601`), with no
error under ASan and none on the RTX.

## Decision

**KEEP**, on by default from milestone 49. In motion — what an upscaler is for — FSR 3.1 is better than FSR 2.2 in every sequence and mode
measured: less error where things move (up to −24 %), less flicker (up to −31 %), +0.6 to +1.3 dB; in quality mode it
comes within about 0.5 dB of the native-resolution TAA at 44 % of the pixels. It is softer on still views at 2×
upscaling (−1.3 dB in two of three), where FSR 2.2's sharper image aliases. On the RTX it costs what FSR 2.2 costs
(Bistro and Sponza at 1080p, within 0.7 %), so a viewport in the FSR 2 scaling mode now runs FSR 3.1 unless the
setting is off. Next for upscaling: DLSS and XeSS behind the same scaling mode (vendor
libraries, their hardware), and frame generation (FSR 3's frame interpolation is in the same SDK; it needs a present
path that shows generated frames between rendered ones).
