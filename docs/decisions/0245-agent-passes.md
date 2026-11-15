# 0245 — The agent toolkit's `run --passes`: the GPU's time by render pass

Status: **KEEP** (an option of `tools/fellow_agent/fellow_agent.py run`) · Branch `fellow/geometry` · Code:
`tools/fellow_agent/fellow_agent.{py,gd}` (`_run()`), `tools/fellow_agent/README.md`, `AGENTS.project.md` · Checks:
the toolkit's test project on lavapipe.

## Problem

An agent measuring a scene with `run` (decision 0152) gets frame times and the GPU's total; when the GPU is the limit it
cannot tell which pass costs it — the question the editor's Visual Profiler or decisions 0242-0244's time line answer
for a person. FellowBench answers it (`--profile=1`) for its own scenes only.

## Change

`run --passes` turns on `rendering/fellow/profiling/pass_timestamps` for the run, reads each frame's timestamps as
they come back (`RenderingDevice.get_captured_timestamp_*`), and reports `gpu_passes`: every pass's milliseconds a
frame and share of the frame's passes, longest first (markers that open or close a group left out), with
`gpu_passes_frames` and `gpu_passes_total_ms`; `--quiet` prints the eight longest with the frame times. Without
FellowEngine (or on a renderer without timestamps) `gpu_passes_note` says none came back. The timestamps order the
GPU's work: frame times with `--passes` are a little longer, as the option's help says.

## Verification

The toolkit's test project on lavapipe (`fellow_3d.tscn`: water, wind, three meshes; 640 × 360, 3 s): 16 frames, each
frame's timestamps read (`gpu_passes_frames` 16), 38 passes, the frame's passes 196 ms; the longest "FellowWater:Begin"
(the water's simulation) 74.8 ms (38 %), "Render 3D Transparent Pass" 48.0 ms (25 %), "FellowEngine Shadow Pyramids"
37.0 ms (19 %). `selftest.py` checks it (`run --passes`: more than three passes, more than ten frames, longest first).
The regression suite's captures go through the same script (unchanged).

## Decision

Keep: an agent finds the pass that costs a GPU-bound scene its frame with one option.
