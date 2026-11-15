# 0244 — Every frame of a span: a graph of their times, any frame's passes on the time line

Status: **KEEP** (the debugger's FellowEngine Hitches tab) · Branch `fellow/geometry` · Code:
`servers/rendering/fellow_hitch_sampler.cpp` (the `record` message, `gpu_wanted()`, `gpu_frame()`),
`modules/fellow_editor/fellow_hitch_debugger.{h,cpp}` (`FellowFrameGraph`, `_record_frames()`, `_frame_text()`) ·
Checks: a project played from the editor on lavapipe with the sampler off, a recording of 2 s; a screenshot of the
tab.

## Problem

Decision 0243 shows one frame's passes when asked; decisions 0237-0242 the long ones the sampler catches. Between them
is what a frame-time graph answers at a glance — how the frames of a stretch of play compare, whether the slow ones are
the GPU's or the CPU's, how often — and then, for any of them, where its time went: the view a profiler of every frame
opens on (Unreal Insights' frame track, PIX's timing captures, Unity's profiler's CPU and GPU charts). Godot's visual
profiler draws a frame's CPU and GPU time as a graph but its GPU passes only as an average tree, not where they ran.

## Change

* The tab's **Record GPU Frames** sends `fellow_hitch:record` with the seconds of the spin box beside it (0.5-30 s;
  2 by default). While a recording is asked for the renderer captures its pass timestamps (`gpu_wanted()`), with the
  sampler on or off.
* Each frame recorded in the span, as its timestamps are read back: its number, when, the interval since the previous
  frame's first timestamp was recorded, the CPU's time recording its passes, its wait for the GPU before it, its passes'
  time, and its time line (decision 0242's: name, queue, start, length, average when the sampler has one). Sent in
  batches of 30 frames by the main thread's outbox (decision 0238; up to 3 000 frames); the first frame recorded after
  the span sends the rest and clears the request: no timestamps afterwards unless the sampler is on.
* The tab: a `Frames` row (when it began, the slowest frame's time, the frame count and the GPU's average) whose graph
  (`FellowFrameGraph`) draws a bar a frame — the interval behind in grey, the GPU's passes in front in blue — with lines
  at 16.7 and 33.3 ms and tooltips; the slowest frame is selected when the recording ends, and clicking or dragging over
  the bars selects another: its passes on the time line and, in the text, the frame's figures and its 16 longest passes
  (by name and queue, the asynchronous part's start and length).

## Verification

Lavapipe, the project of decision 0238 (a long GPU frame every 40 frames, a 250 ms sleep in a script every 60) played
from the editor with Record off; Record GPU Frames for 2 s, 8 s in: a `Frames` row "8.82 s, 282.3 ms, 91 frames, the
GPU's passes 15.23 ms on average"; every frame with its 39-41 passes; the first frame's interval unknown, the others
1.5-21 ms and, at the sleeps, 282 ms; the slowest selected at the end — "Frame 401, 10.15 s in: 282.30 ms after the
previous frame; the GPU's passes 11.60 ms; the CPU recorded them over 1.46 ms and waited 0.00 ms for the GPU before
it" (a CPU frame: the script's sleep) — its passes on the time line. The graph (screenshot): the ordinary frames' bars
at 7-20 ms, the sleeps' grey bars and the heavy frames' blue ones clipped at the scale with a red tip. The editor check
passes; the regression suite is unchanged.

How it was found to work: the first graph scaled to the slowest frame (287 ms: the ordinary frames a few pixels high)
and drew the 16.7 and 33.3 ms labels over each other; the scale now stops at three times the 90th percentile (at least
33.3 ms) and a label is drawn only where it does not meet another.

## Decision

Keep: a span of play's frames as a graph, any frame's GPU passes where they ran, in the editor, with nothing captured
until it is asked for.
