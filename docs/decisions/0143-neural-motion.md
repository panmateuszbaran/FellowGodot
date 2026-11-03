# 0143 — Neural motion: MotionBricks on the RenderingDevice

Status: **KEEP** (a new module; nothing changes without it) · Branch `fellow/geometry` · Module:
`modules/fellow_motion` (`FellowMotionModel`, `FellowMotionCharacter3D`; `fellow_motion.glsl`,
`fellow_motion_rig.{h,cpp}`, `fellow_motion_service.{h,cpp}`; `tools/fmb_convert.py`, `fmb_plan.py`, `fmb_usd.py`,
`fmb_soma.py`, `fmb_bvh.py`; `tests/`) · Vulkan: `VK_KHR_cooperative_matrix` (`drivers/vulkan`,
`RD::SUPPORTS_FELLOW_COOPERATIVE_MATRIX_F16`) · Bench: `benchmarks/project/scenes/motion_lab.gd` (`--scene motion`).

## Problem

Characters in the engine move by clips: an `AnimationPlayer` or `AnimationTree` blends authored animation, and every
gait, turn, start and stop is a clip somebody made and a blend somebody tuned (Crowd Lab's 256 characters play four).
NVIDIA's MotionBricks (2026; github.com/NVlabs/motionbricks, the model at huggingface.co/nvidia/MotionBricks) generates
whole-body human motion in real time instead: given the four poses a character last showed and up to four target poses
(or only where its root should be), a transformer picks the length (24-64 frames at 30 Hz) and tokens of the motion
and a convolutional decoder turns them into poses — a locomotion controller with no clips. Its runtime is Python on
CUDA with TensorRT (1.3-2.5 ms a generation on an RTX 4090); none of that runs in a game engine.

## Change

**The model on the RenderingDevice.** `tools/fmb_convert.py` turns the released SOMA checkpoints (three networks: a
root transformer, 6 + 2 layers of width 512 with a convolutional decoder; a pose transformer, 16 layers of width 1024;
the VQ-VAE decoder — 188 M matrix weights, 6.5 GFLOP a request) into one `.fmb` file: the matrices in fp16 (or fp32),
the small parameters in fp32, and a flat plan of 251 ops over one activation buffer — GEMMs with row gathers or
convolution taps (upsampling folded in), split-K for small batches and fused epilogues (bias, residual, ReLU or leaky
ReLU, the token mask), attention with the key mask, layer norm, and twelve glue kernels (the token count's argmax, the
dense conditions, the global-to-local root, the pose tokens' argmax and codebook). Constant parts are folded at
conversion (the mask token's embedding, the root condition's two projections, the token count's embedding through the
input projection). `FellowMotionModel` runs the plan on a local RenderingDevice — a Vulkan device and queue of its own
beside the renderer's — one op per compute list (the render graph puts the barriers), the activations fp32 and
channels-last. The model's input and output handling (normalisation, the capture/standard T-pose rotations,
integrating the root, the root module's height check) is on the CPU, as in the ONNX graph. `tools/fmb_plan.py`
executes the same plan in NumPy op for op: it is the reference the shaders were written against.

**The products.** On a device with 16×16×16 fp16 cooperative matrices (`VK_KHR_cooperative_matrix`, now enabled by the
Vulkan driver when present: an RTX's tensor cores) and a fixed subgroup size, with fp16 weights, the GEMMs run on
them: the A tile rounded to fp16 in shared memory as NVIDIA's mixed-FP16 model does, fp32 accumulation, four subgroups
of 16 columns each (`cooperative_matrix`, on by default; `get_info()["products"]` says which runs). Elsewhere (lavapipe,
fp32 weights, or off) they are fp32 FMA tiles. Both kernels keep, per thread, one column of the A tile and its rows'
addresses for the whole product and fetch the next K step's A and W into registers while the current one is
multiplied (the first version's loads went out one after another: 4 TFLOPS on tensor cores, 3 in fp32), and read A
through a read-only view of the activation buffer. The outputs are copied on the GPU into a small read-back buffer
(`buffer_get_data` stages the whole buffer it reads: 26-35 ms at a largest batch of 64 before).

**The weights stay NVIDIA's.** The checkpoints are under the NVIDIA Open Model Agreement ("Licensed by NVIDIA
Corporation under the NVIDIA Open Model Agreement", which the `.fmb` carries and `get_info()` returns), the SOMA
skeleton and body mesh (`fmb_usd.py` reads MotionBricks' USD asset into the `.fmb` and a skinned `.glb` mannequin)
under CC-BY-4.0, the sample clips under an evaluation-only license: none is in this repository, the converted files
are kept outside it, and the engine needs no clip (below).

**A character.** `FellowMotionCharacter3D` takes a desired velocity and does the rest, following MotionBricks' runtime:
the poses it shows are recorded (32, with their times) and the context is sampled from them at 30 Hz; the target is
placed `lead_time` (0.97 s) ahead along the smoothed velocity (MovementControl's critically damped springs: the
direction in 0.2 s, the speed in `speed_halflife` 0.1 s, slower in sharp turns), facing it and moving at it; a new
segment is requested at the old one's transition point (`transition_early_frames` 8 before its end) or when the speed
or direction changes enough (the reference's thresholds), and every 0.33 s while the speed shown is far from the
target's (`catch_up`: the model approaches a target over its whole segment, up to 1.9 s); the result is
endpoint-corrected and crossfaded in (0.15 s) when it arrives. **Root-only targets:** MotionBricks' locomotion places
target poses sampled from clips; here the target gives only the root (position, a standing height for the speed,
heading, velocity) and the model fills in the body — from a relaxed stance (the model's standard T-pose with the arms
lowered) it walks, jogs and runs with its arms swinging. Standing still it comes to rest in that stance and keeps
generating small idle segments. All characters of a model share one service: their requests run as one batch on a
worker thread (`INFERENCE_ASYNC`), at an explicit flush (`INFERENCE_FLUSH`: deterministic, one step of latency) or at
once (`INFERENCE_IMMEDIATE`). The node moves itself (its origin the hips on the ground, its +Z the facing) and poses
the mannequin's skeleton.

**Falls.** The model's training data has lying and crawling, and in a crowd some characters went down and stayed
down, crawling on their hands (the user's report). Three causes, three changes: the target's hips were at the
height of the moment (frequent re-planning sank them lower each time) — now a standing height; a target ahead of a
character on the ground keeps it there — `get_up` (on by default): on the ground (the hips below 3/4 of the standing
height or the head below 4/5) the next target is a standing pose a little ahead, and that segment plays to its
transition point; and the model falls far more often running fast — `max_speed` 3.5 m/s by default (was 4.5).

## Verification

* **The plan against NVIDIA's PyTorch reference** (`tests/fmb_parity_torch.py`; the exporter's trace inputs and six
  requests built from SOMA clips — walk to jog, a turn, idle to walk, a stop, one target pose, a turn back): with fp32
  weights every token count and every pose token is the reference's, the outputs within 1e-5 (2.5e-5 in the motion).
  With fp16 weights 2 of 70 pose tokens of 2 of the 6 clip requests differ (0.18 at most in a motion feature): NVIDIA's
  own mixed-FP16 model "can choose different motion tokens" too.
* **The GPU executor against the plan** (`tests/fmb_gpu_run.gd`, `fmb_parity_gpu.py`): fp32 products — lavapipe and
  the RTX — the same tokens, within 1e-4 (fp32 and fp16 weights, batches of 1, 3 and 6: the split-K and tile
  variants); the rewritten kernels give the earlier ones' results bit for bit (RTX: `m43_f320`, `f324`), and the final
  code on lavapipe the plan's tokens within 1e-4 again (fp16 and fp32 weights; the RTX's motion within 2.4e-5 and
  9.2e-4). Tensor cores
  (RTX): the same token counts; 10 of 405 pose tokens of the six clip requests differ from the plan's, all where the
  two best logits are within 0.08 — rounding A to fp16 in the plan alone (`fmb_plan.HALF_A`) changes 9, and the tensor
  cores differ from that plan in 7.
* **The glue** (`tools/fmb_soma.py`, then the C++ port): decoded, the reference's motion starts within 5 mm of the last
  context pose and ends within 1-2 cm of the first target pose before the endpoint correction.
* **A character** (`tests/fmb_character_run.gd`, `fmb_character_check.py`; stand, walk ahead, turn walking, jog, run a
  curve, stop): on tensor cores (`m43_f324`) the walk 0.95-1.17 m/s of 1.3, the jog 2.52 of 2.8, the run 3.06 of 3.5
  (each phase's second half), the hips at 0.97-0.98 m, the lowest joint at 0.0 m (median; −4 to +9 cm), the stop
  within 0.12 m/s; with fp32 products (lavapipe and the RTX, the same motion on both) the walk 1.15-1.26, the jog
  2.84, the run 3.71 of 4.0. A different numerical path gives different tokens and so a different motion; both are
  the model's.
* **A crowd** (`tests/fmb_crowd_run.gd`, `fmb_crowd_check.py`; Motion Lab's 32 characters for 40 s, the hips' and the
  head's heights; a fall: below 0.75 m at the hips or 1.2 m at the head after at least 1 s up): with the hips' target
  at a standing height alone 13 falls, 5 characters down for more than 3 s (`m43_f319`: 7.6 % of the
  character-steps); without getting up, 17 falls running at up to 4.2 m/s, 8 at up to 3.5, 3 at up to 3.0
  (`m43_f323`; crossfades, the target's lead and the transition point made no difference); final (`m43_f324`): 8
  falls of 5 characters, all back up (10 get-ups, 0.4 s on the ground at most, 0.15 % of the character-steps).
  lavapipe, 4 characters for 10 s, the final code: the character that lay down for 7 of the 10 s before falls once
  and is back up within 0.5 s.

## Results

RTX 5070 Ti Laptop; every time below was taken with the laptop in use (the GPU not idle), so they are upper bounds.

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

Motion Lab, 1080p, 2 runs each (`m43_f318_motion` … `m43_f324_motion`), the mean frame:

| Motion Lab, 1080p, the mean frame (2 runs) | `m43_f318` | `m43_f319` | `m43_f320` | `m43_f324` (final) |
|---|---|---|---|---|
| 16 characters, flush | 5.55 ms | 5.04 | 4.40 | **4.32** |
| 64 characters, flush | 40.36 | 12.85 | 9.50 | **9.41** |
| 64 characters, asynchronous | 5.78 (batches of 32 queueing) | 4.70 (queueing) | 5.05 | **5.04** |

Flush runs each step's requests (1.3 a batch with 16 characters, 2.7 with 64) on the main thread before the next step:
the frame waits for them (6.2-6.6 ms a batch). Asynchronous runs them on the service's worker thread as they come
(20-23 a batch at 40-46 ms in `m43_f324`; before the faster products every batch was the largest, 32, the requests
queueing): the frame does not wait, the renderer's GPU time rises from 1.7 to 3.8 ms beside the model's work.

## Decision

**KEEP.** A new module, nothing changes without it. Tensor cores on by default where the device has them.

## Notes

* Tried and reverted: K steps of 64 on tensor cores with four consecutive k per thread (vec4 loads) and each
  subgroup's own epilogue (`m43_f322`: batch 16 23.5 ms against 14.5); the target placed as far ahead as the character
  gets changing its speed evenly (`m43_f321`: falls as often, 13 in 40 s).
* Not done: the transformer's products are still far from the GPU's limits (9.3 TFLOPS on tensor cores at a batch of
  16; the pose transformer's weights alone would take 0.4 ms at the memory's bandwidth): a deeper pipeline (two
  stages in shared memory), wider column tiles (A is read again by every 64 columns) or fp16 activations are the next
  steps.
