# FellowEngine — milestone 121 report (any frame's GPU passes in the editor; a span of frames as a graph; the agent toolkit's passes)

Status: **FINAL** (2026-11-15). Branch `fellow/geometry`. Milestones 119-120 brought a running game's long frames into
the editor's debugger and drew a long GPU frame's passes on a time line; the readiness assessment's editor row still
said "no time line of every frame's passes".

## 0. Result

* **Capture GPU Frame** (decision 0243): the debugger's FellowEngine Hitches tab asks the running game for its next
  frame's passes, with the hitch sampler on or off; the game captures its pass timestamps only until that frame is read
  back. A `Frame` row, its passes longest first and on the time line by queue.
* **Record GPU Frames** (decision 0244): every frame of the next seconds (0.5-30 s, up to 3 000 frames) sent in batches
  as they are read back: a graph of their times — the interval since the frame before behind, the GPU's passes in
  front, lines at 16.7 and 33.3 ms, spikes clipped and marked — the slowest selected; clicking or dragging over it shows
  any frame's passes on the time line and its figures (the CPU's recording of its passes, its wait for the GPU).
* **`fellow_agent.py run --passes`** (decision 0245): an agent gets the GPU's time by render pass, longest first, in
  `report.json` (milliseconds a frame and share).
* Nothing renders differently and nothing is captured until asked: the regression suite as its goldens.

## 1. Capture GPU Frame (decision 0243)

A `fellow_hitch:capture` message from the tab; while a request is pending `gpu_wanted()` makes the renderer capture
its timestamps; the first frame recorded after it is sent as `fellow_hitch:frame` with decision 0242's time line, and
the request cleared. Lavapipe (the sampler off, 8 s in): one row, 42 timestamps, the passes 10.92 ms, "Tonemap" 3.97 ms
and "Render Opaque Pass" 2.88 ms the longest. Found on the way: a capture with the sampler off was timed "0.00 s in"
(it set the sampler's start); passes without the sampler's averages were all orange — now two greys in turn.

## 2. Record GPU Frames (decision 0244)

Each recorded frame: its number, when, the interval since the previous frame's first timestamp was recorded, the CPU's
recording of its passes, its wait for the GPU before it, its passes' time and time line. Lavapipe, a project with a
long GPU frame every 40 frames and a 250 ms script sleep every 60, recorded 2 s with the sampler off: 91 frames, the
GPU's passes 15.23 ms on average, the slowest a 282 ms interval whose passes took 11.6 ms and whose CPU waited 0.00 ms
for the GPU — the script's sleep, a CPU frame, told apart at a glance from the heavy GPU frames (blue bars). The first
graph scaled to the spike (the ordinary frames a few pixels high): the scale now stops at three times the 90th
percentile.

## 3. The agent toolkit's passes (decision 0245)

`run --passes` turns on `profiling/pass_timestamps` for the run and averages each frame's passes as they come back.
The toolkit's `fellow_3d.tscn` on lavapipe: 38 passes over 16 frames, the water's simulation 38 % of the frame, the
transparent pass 25 %, the shadow pyramids 19 %. `selftest.py` checks it.

## 4. Checks

The regression suite (36 scenes, the release template against the editor build, lavapipe): the same as its goldens;
the editor check passes; the class reference unchanged (no bound API added). The first regression run of the
milestone failed from its 13th scene on: the agent toolkit's script, which the suite runs, was edited while it ran (a
type the parser could not infer) — fixed, and the suite run again from the start.

## 5. Next

Stress in structural destruction (the readiness assessment's physics row: support is connectivity to the ground, not
strength): milestone 122.
