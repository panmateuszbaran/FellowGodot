# FellowEngine — milestone 43 report (neural motion: MotionBricks on the GPU)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m43_f318`-`f324`, `m43_s27`-`s31`, 2026-11-03; the
laptop in use during every RTX measurement, so the times are upper bounds). Branch `fellow/geometry`. Decision record
`docs/decisions/0143-neural-motion.md`.

## 0. Result

Characters that walk, jog, run, turn, stop and stand by themselves, with no animation clip: NVIDIA's MotionBricks
(SOMA, 2026) runs inside the engine on Vulkan compute — no Python, CUDA or TensorRT — and `FellowMotionCharacter3D`
drives a skinned mannequin with it from a desired velocity. One request (4 context poses, a target ahead, 24-64 frames
of motion out) takes 4.3 ms on the RTX's tensor cores, 16 at once 18 ms, 64 at once 62 ms; Motion Lab's 64
characters cost 9.4 ms a frame when the frame waits for their requests and 5.0 ms when a worker thread runs them. The
weights stay NVIDIA's (the NVIDIA Open Model Agreement) and outside the repository; the engine needs none of
MotionBricks' sample clips.

## 1. Development

* **The converter and the reference** (`tools/fmb_convert.py`, `fmb_plan.py`): the three SOMA checkpoints → one
  `.fmb` (376.6 MB in fp16, 751.8 MB in fp32): a plan of 251 ops over one activation buffer, constant parts folded; the
  NumPy executor of the same plan, against NVIDIA's PyTorch model.
* **The executor** (`FellowMotionModel`, `fellow_motion.glsl`): a local RenderingDevice, one compute list per op; GEMMs
  with row gathers and conv taps, split-K, fused epilogues; attention, layer norm, the glue.
* **Tensor cores**: `VK_KHR_cooperative_matrix` enabled in the Vulkan driver when the device has it
  (`RD::SUPPORTS_FELLOW_COOPERATIVE_MATRIX_F16`: 16×16×16 fp16 → fp32, subgroup scope); a GEMM kernel on it (A
  rounded to fp16 in shared memory). lavapipe has no cooperative matrices: that path is the RTX's only.
* **Faster products**: the first kernels' loads went out one after another (4 TFLOPS on tensor cores, 3 in fp32); each
  thread now keeps one column of the A tile and its rows' addresses and fetches the next K step during the current one
  (2× at batches of 8 and more), A read through a read-only view of the activations. Tried and reverted: K steps of
  64 with vec4 loads (`m43_f322`, slower: batch 16 23.5 ms against 14.5).
* **The read-back**: `buffer_get_data` stages the whole buffer it reads — 147 MB of activations at a largest batch of
  64, 26-35 ms — so the outputs are copied on the GPU into a small buffer first (0.2-4 ms).
* **The character** (`FellowMotionCharacter3D`, `fellow_motion_rig`, `fellow_motion_service`): MotionBricks' engine
  glue ported (context sampling, encoding, decoding, endpoint correction, MovementControl), root-only targets from a
  relaxed stance (no clips), idle segments, one batched service per model (async, flush or immediate), a skinned SOMA
  mannequin (`tools/fmb_usd.py`).
* **Getting to speed**: the first segment was generated at the speed of the moment (zero) — the target now at once at
  the desired speed, the speed's spring at 0.1 s, and a new segment every 0.33 s while the speed shown is far from the
  target's (`catch_up`).
* **Falls** (the user's report: characters sliding over the ground on their hands): the target's hips at the height of
  the moment let frequent re-planning sink them — a standing height; on the ground, a standing pose as the target
  (`get_up`); and fewer falls at lower speeds — `max_speed` 3.5 m/s (below).
* **Motion Lab** (`--scene=motion`): N characters wandering a plaza, steering round each other, stopping; three fixed
  cameras, a flythrough, the loop `motion_walk`.

## 2. Verification

* **Plan against PyTorch** (fp32 weights): every token the reference's, the outputs within 1e-5; fp16 weights: 2 of 70
  pose tokens of the clip requests differ, as NVIDIA's mixed-FP16 model's do.
* **GPU against the plan**: fp32 products (lavapipe and RTX) the same tokens, within 1e-4, batches 1, 3, 6; the
  rewritten kernels bit for bit the earlier ones' results (`m43_f320`, `f324`). Tensor cores: the same token counts, 10
  of 405 pose tokens different, all near ties (within 0.08 of the best logit); the plan with fp16 A operands changes
  9 itself. The final code on lavapipe (efe14d2e): the plan's tokens, within 1e-4, fp16 and fp32 weights.
* **A character** (RTX, tensor cores, `m43_f324`): stand, walk ahead, turn walking, jog, run a curve, stop — the
  walk 0.95-1.17 m/s of 1.3, the jog 2.52 of 2.8, the run 3.06 of 3.5, the hips at 0.97-0.98 m, the feet on the
  ground (the lowest joint's median 0.0 m), the stop within 0.12 m/s; 26 requests in 12 s, 4.8 ms each.
* **A crowd** (Motion Lab's characters, 32 for 40 s, RTX; a fall: below 0.75 m at the hips or 1.2 m at the head after
  at least 1 s up):

| crowd run (32 characters, 40 s) | falls | down for more than 3 s | character-steps on the ground |
|---|---|---|---|
| `m43_f319`: the hips' target at a standing height, up to 4.2 m/s | 13 | 5 | 7.6 % |
| `m43_f323`: the same, without getting up | 17 (9 characters) | 5 | 8.2 % |
| up to 3.5 m/s, without getting up | 8 (5) | 3 | 4.1 % |
| up to 3.0 m/s, without getting up | 3 (3) | 2 | 0.83 % |
| without crossfades / the target 1.5 s ahead / the transition 4 frames early | 12 / 14 / 18 | 6 / 4 / 7 | 7.1 / 4.8 / 10.2 % |
| `m43_f324`, final: up to 3.5 m/s, getting up | 8 (5), 10 get-ups | 0 (0.4 s at most) | 0.15 % |
| `m43_f324`, final, without getting up | 11 (5) | 0 (2.3 s at most) | 0.88 % |

On lavapipe (4 characters for 10 s) the character that lay on the ground for 7 of the 10 s with the first version
(74 % of its steps) falls once with the final code and is back up within 0.5 s.

## 3. Performance

One run of `FellowMotionModel.run()` (the median of runs 2-12, the CPU's wall time: recording, the GPU's work,
the read-back and decoding), fp16 weights, a largest batch of 64:

| requests at once | first version, fp32 products (`m43_f318`) | tensor cores (`m43_f319`) | tensor cores, fetching ahead (`m43_f324`) | fp32 products, fetching ahead (`m43_f324`) |
|---|---|---|---|---|
| 1 | 32.4 ms | 5.6 ms | **4.3 ms** | 5.9 ms |
| 2 | 32.0 | 7.6 | **5.2** | 7.0 |
| 4 | 37.7 | 11.2 | **7.0** | 10.8 |
| 8 | 47.3 | 18.2 | **10.2** | 17.6 |
| 16 | 67.8 | 31.5 | **18.1** | 30.9 |
| 32 | 110.7 | 57.9 | **30.8** | 56.6 |
| 64 | 170.6 | 112.2 | **62.3** | 107.2 |

The first version's times include the 26-35 ms read-back of the whole activation buffer. The GPU's time alone
(timestamps around every op, `m43_f324`): a request 3.4 ms on tensor cores (4.6 fp32), 16 13.5 ms (26.4 fp32) — at
16 the products 83 % of it (9.3 TFLOPS on tensor cores, 4.4 in fp32), attention 14 %. MotionBricks' own runtime:
1.3-2.5 ms a generation on an RTX 4090 with TensorRT.

| Motion Lab, 1080p, the mean frame (2 runs) | `m43_f318` | `m43_f319` | `m43_f320` | `m43_f324` (final) |
|---|---|---|---|---|
| 16 characters, flush | 5.55 ms | 5.04 | 4.40 | **4.32** |
| 64 characters, flush | 40.36 | 12.85 | 9.50 | **9.41** |
| 64 characters, asynchronous | 5.78 (batches of 32 queueing) | 4.70 (queueing) | 5.05 | **5.04** |

Flush runs each step's requests (1.3 a batch with 16 characters, 2.7 with 64) on the main thread before the next step:
the frame waits for them (6.2-6.6 ms a batch). Asynchronous runs them on the service's worker thread as they come
(20-23 a batch at 40-46 ms in `m43_f324`; before the faster products every batch was the largest, 32, the requests
queueing): the frame does not wait, the renderer's GPU time rises from 1.7 to 3.8 ms beside the model's work.

## 4. Showcase

Motion Lab with 24 characters (`m43_s31_motion`, `m43_s31_motion_loop`, fellow243): `motion_crowd_close.jpg`,
`motion_crowd_plaza.jpg` and the loop `loop_motion_walk.webp` (4 s following a walker) in `docs/showcase/`, with the
model's and the mannequin's notices in `docs/SHOWCASE.md`; two of them in the README. Earlier stills (`m43_s27`,
`s28`) showed a character crawling in the background — the falls of section 2; `s29`, `s30` were from intermediate
builds.

## 5. Incidents

* `m43_f318` was rejected by the queue once: its `.bat` had been copied into `.tools\` before queueing (the runner
  refuses a job whose name is already there); queued again.
* A cloud build without `disable_path_overrides=no` (the tests' `--path` needs it) recompiled the whole engine, and
  the one with it again (about 50 minutes each on two cores); the RTX's builds were incremental.

## 6. Next

* Products nearer the GPU's limits (two shared-memory stages, wider column tiles, fp16 activations): the pose
  transformer's weights alone would take 0.4 ms at the memory's bandwidth.
* Target poses (MotionBricks' styles: a crouch, a jump) from a small library of stances rather than clips.
* Characters on uneven ground (the hips' target height from the ground under them) and with obstacles in the target.
