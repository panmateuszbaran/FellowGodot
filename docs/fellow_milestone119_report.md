# FellowEngine — milestone 119 report (downloads returned whole; the GPU's hitches, scripts' frames, and hitches in the editor)

Status: **DRAFT** (2026-11-15). Branch `fellow/geometry`. Milestone 118 made texture downloads' staging as tight as
uploads' and left the GPU's rare long frames (19-22 ms fence waits in Sponza streamed) out of the sampler's sight.

## 0. Result

* **Downloads returned whole** (decision 0236, with FellowEngine on): checking milestone 118's download staging found
  two faults of stock Godot's in `RenderingDevice.texture_get_data_async()` and `buffer_get_data_async()` — a texture
  that is not square came back without its last mips, and a download larger than what the download staging ring had
  left lost the parts copied before the ring's wait (a 144 MiB buffer 89 % wrong). Fixed; the same on the RTX and on
  lavapipe (`bench/readback_check.gd`, every byte).
* **The GPU's hitches** (decision 0237): with the hitch sampler on, the renderer's pass timestamps every frame; a GPU
  frame whose passes, or the gap before them beyond what the CPU's own time explains, exceed the threshold is reported
  with the passes longest past their averages. Within the runs' noise on the RTX.
* **Hitches in the editor's debugger** (decision 0238): a game run from the editor lists its hitches as they happen in a
  FellowEngine Hitches tab, named, with the full text; Record starts the sampler in a running game.
* **Scripts' frames** (decision 0239): a CPU hitch in a debug build names the GDScript functions and lines it was in
  (`_sleepy_function (res://main.gd:25)`), not only the interpreter's native frames.
* **Frontier Lab** (the 4 km open world, streamed, at 45 m/s; job f459): no frame step and no GPU frame over 16.7 ms in
  the flights — the hitches are all in the first 2 s after its load.

## 1. Downloads returned whole (decision 0236)

(see the decision) The readback check's results:

| | synchronous | asynchronous, 30 in a frame | larger than the ring |
|---|---:|---:|---:|
| FellowEngine off (stock; lavapipe and the RTX alike) | 30 of 30 | 12: the 18 not square wrong (17 in their last 1-16 bytes, BC5 4096 × 2048 63 %) | the buffer 89 % wrong, a texture 40 % |
| on | 30 of 30 | 30 of 30 | 3 of 3 |

## 2. The GPU's hitches (decision 0237)

(pending the final text)

## 3. Hitches in the editor's debugger (decision 0238), scripts' frames (decision 0239)

(pending)

## 4. Checks

The regression suite (36 scenes, the release template against the editor build, lavapipe): the same as its goldens;
the editor check passes; the class reference and the settings' descriptions regenerated.

## 5. Next

(pending)
