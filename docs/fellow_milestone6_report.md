# FellowEngine — milestone 6 report (visually lossless options; dGPU only)

Status: **FINAL** (`fellow87`-`fellow90`, measured 2026-10-08/09; `fellow91` carries the decisions below). Branch `fellow/geometry`. Benchmark machine as in
milestone 5 (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, Windows 11, 1920×1080, "high"
preset), **now with the NVIDIA GPU only (MUX switch)**: a frame takes its GPU time plus ~0.06 ms, so frame times measure
the GPU directly. Decision records `docs/decisions/0026`-`0033`; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

FellowEngine `fellow87` against stock Godot 4.7.2, between processes (`m6_f94_final`: 3 interleaved runs of 20 s per
build and scene, 5 s warm-up, 30 s heat soak), frame time at 1920 × 1080:

| scene | stock 4.7.2 | FellowEngine defaults (exact) | + visually lossless options |
|---|---:|---:|---:|
| Sponza | 13.48 ms | **7.37 ms (−45.3 %)** | **7.12 ms (−47.2 %)** |
| Bistro exterior | 6.54 ms | **4.87 ms (−25.6 %)** | **4.70 ms (−28.1 %)** |
| City Lab | 2.84 ms (one slow run, CV 12 %) | 2.16 ms (−24.0 %, p 0.07) | 2.09 ms (−26.4 %, p 0.06) |
| Bistro interior | 4.17 ms | **3.86 ms (−7.5 %)** | **3.51 ms (−15.9 %)** |
| Lights Lab | 3.02 ms | **2.75 ms (−8.9 %)** | **2.07 ms (−31.5 %)** |
| PBR Lab | 2.56 ms | 2.52 ms (−1.5 %, within noise) | 2.35 ms (−8.4 %, p 0.05) |

(bold: p < 0.02, every pair of runs faster.) The defaults change no pixel against stock (milestones 1-5). The visually
lossless options are new in this milestone and **off by default**: adaptive shading rate (under autotune), temporal
SSAO, temporal SSIL and volumetric fog at half rate. Together they take another **−24.9 %** off Lights Lab, −9.0 %
off Bistro interior, −7.0 % off PBR Lab and −3.2 to −3.4 % off the other three scenes, against the defaults in the same
session; every one of them stays at 50-82 dB against the stock image on still captures and moving sequences (§2-§4;
the user-facing summary is `docs/FELLOWENGINE_OPTIONS.md`).

In that session autotune left adaptive shading on in Sponza, where it costs about what it saves and where a fast pan
shows its largest difference (49.8 dB). From `fellow91` it starts off and is turned on only where autotune measures it
faster (§2). One more exact change came out of this milestone: the TAA no longer copies its output back into the
internal colour texture (0033, −0.5 to −1.2 % per frame in every scene, on by default from `fellow91`).

## 1. The track

Milestones 1-5 changed nothing in the image: every default is exact. In this milestone the user agreed to a second
kind of option: changes to the image that measurements show to be invisible (PSNR against the stock image on still
captures and on 60-frame camera sequences, the largest per-pixel difference, the share of pixels off by more than two
levels; ≥ 50-55 dB and almost no pixel above 2 levels as the bar). They are off by default; defaults stay exact.

## 2. Adaptive shading rate (0027)

Godot already supports a fragment shading rate attachment (Vulkan `VK_KHR_fragment_shading_rate`); FellowEngine gives
every 3D viewport one and fills it itself (`shading/adaptive_vrs`, start-up option). Per 16 × 16 tile:

* **Estimate** (after a frame's final image): the luma gradient per axis from squared differences at strides 1, 2 and
  4 (the largest kept, so a tile shaded coarsely is not mistaken for flat), and the largest step between neighbours;
  a fragment covering 2 pixels misses ~0.5 gradients per pixel, 4 pixels ~1.12, and the coarsest rate whose errors stay
  under the threshold (0.006 luma) and the largest-step limit (0.02) is kept. The tile's view-depth range is kept too.
* **Apply** (the next frame, after its depth pre-pass, on the async compute queue with the SS effects): five points of
  each tile are reprojected with this frame's depth and the camera motion into the estimated frame, and the tile takes
  the finest rate of the tiles found there plus one around; points that were off screen or outside their tile's depth
  range (disocclusion), and glossy surfaces whose normals vary between neighbours (aliased highlights), get full rate;
  every tile is shaded at full rate one frame in 8 so that detail lost to coarse shading is seen again.
* Materials that discard, use alpha antialiasing or write depth keep one fragment per pixel (pipeline rate 1 × 1).
* **Autotune** (decision 0020) runs it on and off in alternating blocks of frames and keeps it only where it saves GPU
  time: its two passes cost ~0.1 ms, more than they save where little of the screen is smooth (Sponza 5 % of the tiles)
  or the opaque pass is cheap (City Lab).

Versions measured: v1 (`fellow77`) blocks on thin highlights; v2 the largest-step limit and the refresh; v3 the gloss
guard; v3 fast the estimate's reduction in parallel (0.12-0.19 → 0.05-0.07 ms; one thread had summed 64 × 8 partials);
v4 the reprojection; v4.1 the disocclusion test; then the apply pass on the async queue, and autotune.

In one process (`m6_f92b_all_ab`, threshold 0 against 0.006, the attachment present in every block, autotune off):

| scene | opaque pass | frame |
|---|---:|---:|
| Lights Lab | 1.356 → 0.649 ms | **−21.1 %** |
| PBR Lab | 0.964 → 0.757 ms | **−5.2 %** |
| Bistro interior | 1.490 → 1.305 ms | **−2.8 %** |
| City Lab | 0.532 → 0.479 ms | +1.4 % |
| Bistro exterior | 1.953 → 1.909 ms | +1.0 % |
| Sponza | 2.325 → 2.306 ms | +0.9 % |

Image against full-rate shading (`m6_f89`, `m6_f92b`): still captures 52.8-74.1 dB (mean per scene 54.3-74.1; at most
1.5 % of the pixels off by more than 2 levels, Bistro interior); 60-frame sequences Lights Lab walk 60.1 dB, City Lab
pan 75.1 dB, Bistro exterior path 56.9 dB (1.2 % > 2), Sponza 45°/s pan 49.8 dB (5.3 % > 2). In a still view TAA
averages each coarse fragment over its 16 jittered frames; in a fast pan it cannot, and texture detail in tiles of
moderate contrast softens. `adaptive_vrs_max_step` 0.01 keeps every pan above 62 dB (Bistro exterior 82 dB) at the
cost of Bistro interior's gain (`m6_f93`); the motion guard (`adaptive_vrs_motion_px`) does the same for moving tiles
only, but costs much of Lights Lab's gain. Defaults stay at 0.02 with autotune: the scenes that lose detail in pans
are the ones where adaptive shading does not pay.

Autotune's rule for it (`m6_f96`, between processes, every option on): started on and kept on while undecided
(`fellow87`), it stayed on in Sponza (4946 frames of a run), where it saves nothing; started off and switched off
whenever an evaluation was undecided (`fellow89`), it stayed off in Sponza and City Lab as intended but also lost much
of PBR Lab's gain (+4.4 %, on in 487-4113 of 8850 frames), because PBR Lab's path has stretches where the difference is
small. `fellow91`: it starts off, turns on only when an evaluation measures it faster and then stays on unless one
measures it slower (to be confirmed between processes with the next full run).

## 3. Temporal SSAO and SSIL (0028, 0030)

Both gathers (four deinterleaved slices, mirrored tap pairs, 12 taps at high quality) take every other tap per frame,
alternating halves; the half from the previous frame is read from a per-slice history at the pixel where this pixel's
point was (reprojected with the previous camera; the same surface within 2 % of depth), otherwise the own half counts
twice. Low to high quality, full and half size (Godot's default), one view. Ultra's adaptive gather is not split.

| | SSAO (`postfx/ssao_temporal`) | SSIL (`postfx/ssil_temporal`, half size as in the preset) |
|---|---|---|
| captures against stock | 59.3-82.3 dB mean (min 57.5) | 58.6-62.9 dB (min 57.8) |
| sequences | 58.3-75.3 dB (Sponza min 50.6, 0.37 % > 2) | Bistro exterior 57.7, Sponza 58.7 dB (min 52.1) |
| stock's half size against full, for scale | 46-47 dB | |
| gather pass | 0.39-0.52 → 0.27-0.45 ms | 0.20-0.22 → 0.13-0.14 ms |
| frame, in one process | −1.5 to −3.1 % (Sponza +0.7 %, noise of async scheduling) | −1.0 to −1.5 % where SSIL is used |

With async compute (on by default on NVIDIA) the screen-space effects run beside the shadows, and where they are the
longer queue (City Lab, PBR Lab, Bistro interior) their savings come off the frame directly.

## 4. Volumetric fog at half rate (0031)

The density pass lights each froxel and blends it with the reprojected previous value (stock's temporal reprojection,
history weight 0.9). With `postfx/fog_half_rate`, the froxels of one parity of a 3D checkerboard of 4 × 4 × 4 blocks
(whole workgroups) are lit in even frames, the others in odd frames; a froxel not lit keeps its reprojected value, one
lit blends with the history weight squared (stock's blend over two frames), and its jitter steps through the whole
Halton table. The first version used a per-froxel checkerboard and saved nothing (every warp stayed half busy for the
full time, `m6_f89`). Fog pass 0.243 → 0.182 ms in Lights Lab, 0.094 → 0.075 ms in Sponza and Bistro exterior (frame
−1.7 % in Lights Lab, −0.1 % elsewhere). Image: captures 61.4-71.3 dB mean, sequences 60.1-72.2 dB (Lights Lab min
56.5, max |d| 10).


## 5. TAA direct output (0033, exact)

With the history ping-pong (milestone 1) the resolve writes its output into this frame's history texture and one copy
was left: history → internal colour texture, which auto exposure, glow and tone mapping read. Now they read the history
texture itself (one view, no depth of field, no debug draw; otherwise the copy is made as before). The same texels, so
the same images: lavapipe 0 differing pixels, RTX sequences bit-identical in Bistro exterior and City Lab and at the
floor in Lights Lab (`m6_f97`). The TAA region −0.034 to −0.041 ms in every scene, frame −0.5 to −1.2 % (in one
process). On by default from `fellow91`.

## 6. Diagnostics: what the GPU compiles

`fellow88` can dump every pipeline's driver statistics (`VK_KHR_pipeline_executable_properties`; the environment
variable `FELLOW_PIPELINE_STATS` names the file). On the RTX 5070 Ti (`m6_f95_pstats_*`): the specialized opaque-pass
fragment shaders use 80 registers (96 with the directional shadow pyramid switched off), i.e. 24 of 48 warps per SM; the
ubershader 168. The post-processing and screen-space passes are not register-bound (TAA resolve 73, SSAO gather 40-56,
SSIL 48-54, volumetric fog 96, tone mapping 35). No change was made from this yet; it sizes what register-pressure work
on the scene shader could give (80 → 72 registers would allow 28 warps).

## 7. Measured and rejected

* **Occlusion culling of the opaque pass (0026).** Exact (lavapipe 0 differing pixels; RTX bit-identical in Bistro
  exterior), but behind a full depth pre-pass the opaque pass already rejects hidden fragments by its EQUAL depth test,
  so culling saves only vertex work: City Lab culls 77 % of its indices and saves 0.027 ms against 0.054 ms of pyramid
  and test. Frame +0.1 to +5.6 %, only Bistro exterior −1.0 %.
* **Depth pre-pass with the simple elements first (0029).** The pre-pass costs the same in either order (Sponza's
  1.39 ms are 12.8 million triangles of vertex work, its materials all opaque or blended); the second walk costs the
  render thread 2.5 % in Bistro exterior.
* **Soft shadows with half their taps under TAA (0032, not merged).** Lights Lab 80-90 dB, but PBR Lab's thin
  penumbrae 47 dB with differences up to 75 levels: TAA clips its history to the current frame's neighbourhood and does
  not average the extra noise away.

## 8. Incidents

* **Full disk (2026-10-08 15:05).** Six milestones of raw benchmark frames (capture and temporal PNGs, 137 GB in 353
  sessions) filled the laptop's C: drive; three queued jobs failed. With the user's permission the raw frames were
  deleted (their PSNR tables and JSON results were kept); every job now deletes its frames once their differences are
  computed.
* **VRS v4.1 shaded everything at full rate** (`fellow83`): its disocclusion test compared the raw clip w (scaled by the
  unprojection's homogeneous w) with view depths. Caught by the captures (93-101 dB where v4 gave 54-74 dB), fixed in
  `fellow84`.

## 9. Next

* **Vegetation drawn with MultiMesh (milestone 7).** A new benchmark scene, Foliage Lab (`m6_f98`): 150 k grass clumps,
  6 k trees, 6 k bushes and 4 k rocks on a 512 m terrain, either in 16 m / 64 m chunks (a careful project) or one
  MultiMesh per type. Godot culls a MultiMesh only as a whole. Chunked: stock 3.61 ms, FellowEngine 3.40 ms. One
  MultiMesh per type: stock 10.89 ms, FellowEngine 8.63 ms (−21 %, all of it the shadow cache), of which 2.4 ms depth
  pre-pass and 2.8 ms opaque pass drawing 24 million triangles, most of them outside the view. In progress: GPU culling
  of MultiMesh instances per pass (camera and every shadow pass; visible instances compacted in order, indirect draws),
  exact for materials that do not move vertices or read `INSTANCE_ID` (decision 0034). First lavapipe tests: identical
  images without TAA in both layouts, 48 % of the tested instances culled with one MultiMesh per type; with TAA a
  difference along silhouettes (52.6 dB) that is being traced before anything is measured on RTX.
* **The scene shader's register pressure** (§6): 80 registers in the opaque pass's specialized shaders.
* **Sponza's cypress trees** (2.0 ms of 7.4: 1.41 M triangles, the blended leaves 1.25 ms of transparent pass, already
  drawn with a depth pre-pass for their opaque texels and alpha-zero quads skipped): the remaining cost is their shading
  and their small triangles; no exact lever found yet.

