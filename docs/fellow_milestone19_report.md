# FellowEngine — milestone 19 report (more of the look, and its cost)

Status: **DONE** (`fellow192`, laptop measurement `m19_f258`, 2026-10-25). Branch `fellow/geometry`. Benchmark machine
as before (RTX 5070 Ti Laptop GPU). Decision records `docs/decisions/0112`-`0116`, updates to 0107 and 0108;
user-facing summary `docs/FELLOWENGINE_OPTIONS.md` ("Visual features").

## 0. Result

Four more visual features after Unreal's, and three optimizations of milestone 18's. Every feature is off by default,
and off the image is the previous build's bit for bit (lavapipe: City Lab, Crowd Lab, Lights Lab, City Lab with auto
exposure and the bench's dirt map: 0 differing pixels against milestone 17's build).

| setting | what it is | record |
|---|---|---|
| `post/bloom/enabled` | an energy-conserving bloom of the whole image (no threshold, no cap): the sun glows in proportion to its brightness; the lens dirt lies on it | 0112 |
| `post/bloom/lens_flare` | ghosts and a halo of the bright sources | 0113 |
| `post/white_balance/temperature`, `tint` | a camera's white balance (Bradford adaptation to D65) | 0114 |
| `post/light_shafts/intensity` | rays from the sun: the bright sky around it blurred towards it, broken by roofs and clouds | 0116 |

| optimization (RTX 5070 Ti laptop, 1920 × 1080, `m19_f258`) | before → after | record |
|---|---|---|
| clouds traced a quarter of the pixels per frame | City Lab's clouds 0.505 → 0.325 ms | 0115 |
| the atmosphere's sky no longer redraws Godot's radiance every frame | "Setup Sky" 0.25 → 0.002 ms in every scene | 0108 |
| contact shadows in 8 steps | City Lab 0.159 → 0.104 ms, Sponza 0.282 → 0.179 ms | 0107 |

With all of milestone 18's and 19's features on, the per-frame saving of the three is about 0.5 ms in City Lab
(0.25 + 0.18 + 0.06) and 0.37 ms in Sponza (0.27 + 0.10).

## 1. Development

* **Bloom.** Karis's average in the first downsample (against fireflies) removed a sun disk a few pixels wide from the
  bloom: the box holding it weighs 65 000 times less than its neighbours. The input is TAA's; dropped.
* **The sun after TAA.** Godot's TAA blends in a tone-mapped domain (x / (1 + x)): a sun disk two or three pixels wide,
  half covered from jitter to jitter, leaves it at a few units instead of tens of thousands (lavapipe, 960 × 540: 5).
  Lens flare and light shafts take that into account in their thresholds (8 and 0.5); at 1080p the disk is wider.
* **Light shafts.** With a threshold of 2 only a disk around the sun counted, and the blur made a glow; at 0.5 the low
  sky near the sun counts, and the roofs break it into rays.
* **Interleaved clouds.** The first version gave a pixel not traced this frame, without history (uncovered by a pan),
  its own trace of up to three frames ago — from where the screen was then: blocky trails at the leading edge. Such a
  pixel now takes the mean of this frame's traces around it; on a fast sky pan (60° per second, lavapipe) every frame
  matches the full trace at 47.7-49 dB.
* **The sky's radiance.** The bench's sky shader read `POSITION` (the camera's height), which makes Godot redraw the
  sky's radiance every frame; the height now comes from a global shader parameter the engine sets.

## 2. Laptop

`m19_f258` (`fellow192`, in-process A/B, paired 30-frame blocks, auto exposure on, the atmosphere's sky, GPU time):

| configuration | City Lab (sun 8°) | Sponza |
|---|---:|---:|
| none (ms per frame) | 2.03 | 6.44 |
| bloom | +0.14 (its passes 0.116, the tone mapper +0.016) | — |
| bloom + lens flare | +0.15 | +0.13 (with the lens dirt: passes 0.142, tone mapper +0.032) |
| light shafts | +0.06 (where the sun is on the screen) | — |
| white balance | −0.002 (n.s.) | — |
| clouds, every pixel traced | +0.60 (clouds 0.505) | +0.18 (0.139) |
| clouds, interleaved | +0.39 (0.325) | +0.15 (0.134) |
| contact shadows, 16 steps | +0.16 (0.159) | +0.25 (0.282) |
| contact shadows, 8 steps | +0.12 (0.104) | +0.17 (0.179) |

The clouds' remaining cost where little sky is seen (Sponza: 0.13 ms) is the temporal pass at half resolution and the
composite at full resolution, which run for every pixel. "Setup Sky" is 0.002 ms in both scenes (0.25 and 0.27 ms in
`m18_f257`): the "none" frames are shorter than there (City Lab 2.03 against 2.40 ms, Sponza 6.44 against 6.87 ms — the
sky's redraw and the laptop's run-to-run variation).

Captures (`m19_f258_capture`, `m19_f258_city3`, `m19_f258_city35`; every feature of milestones 18 and 19, the white
balance at 7000 K): City Lab facing a 3° sun — rays over the roofs, warm clouds, the bloom around the sun, the lens
dirt's specks; Sponza's sky-lit floor grey instead of blue and Bistro's red awnings red in the shade (the white balance
at 7000 K takes out the clear sky's blue, milestone 18 §3).

## 3. Open

* Godot's TAA dims small bright sources (above); a bloom before TAA would need its own anti-flicker.
* The clouds' temporal and composite passes run for every pixel (0.1 ms where little sky is seen).
* The clouds' and the exposure's textures are the renderer's, not the viewport's.

## 4. Next

As the user decides.
