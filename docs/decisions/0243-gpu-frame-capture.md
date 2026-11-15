# 0243 — Any frame's GPU passes on the time line, when the editor asks

Status: **KEEP** (the debugger's FellowEngine Hitches tab) · Branch `fellow/geometry` · Code:
`servers/rendering/fellow_hitch_sampler.cpp` (the `capture` message, `gpu_wanted()`, `gpu_frame()`),
`modules/fellow_editor/fellow_hitch_debugger.{h,cpp}` (`_capture_frame()`) · Checks: a project played from the editor
on lavapipe with the sampler off, the button pressed 8 s in; a screenshot of the tab.

## Problem

Decision 0242 draws a GPU frame's passes on a time line, but only a long one's, and only with the sampler on: a
developer asking where an ordinary frame's GPU time goes — which pass is the longest, whether the asynchronous compute
queue's part overlaps the main queue's — has Godot's Visual Profiler (the main queue's passes as a tree, averaged) and
nothing that draws the passes where they ran. AAA engines' in-game profilers capture a frame on demand (Unreal's
`ProfileGPU`, Unity's Frame Debugger, PIX's GPU captures).

## Change

* The tab's **Capture GPU Frame** button sends the running game a `fellow_hitch:capture` message. The game notes when
  it was asked; while a request is pending `gpu_wanted()` is true, so the renderer captures its pass timestamps (as
  with the sampler on) without the sampler or `profiling/pass_timestamps`.
* The first frame recorded after the request whose timestamps are read back is sent as `fellow_hitch:frame`: its
  passes' time and count, when the asynchronous queue's part began and how long it took, the 16 longest passes, and the
  same time line as a GPU hitch's (name, queue, start, length, average when the sampler has one). The request is then
  cleared: no timestamps are captured afterwards unless the sampler is on.
* The tab lists it as a `Frame` row (when, its passes' time, its longest pass), selects it and draws the time line.
  With the sampler off the time is from the game's start; with it on, from the sampler's, like the hitches.

## Verification

Lavapipe, a project played from the editor with Record off (the sampler off); the button pressed 8 s in: one `Frame`
row, "captured 8.73 s in (read back at drawn frame 361)", 42 timestamps, the passes 10.92 ms, the longest "Tonemap"
3.97 ms and "Render Opaque Pass" 2.88 ms; the time line drawn in the tab (screenshot), its passes in two greys in turn
(no averages with the sampler off; decision 0242's colours with it on). Without the button no timestamps are captured
(`gpu_wanted()` false with the sampler off). The editor check passes; the regression suite is unchanged (nothing
renders differently).

How it was found to work: the first version timed a capture with the sampler off from the frame itself ("0.00 s in":
the sampler's start was taken as the capture's); a capture is now timed from the game's start then, and does not set
the sampler's. The tab's Kind column was widened for "Frame", and adjacent passes of a captured frame are told apart
by alternating greys and a dark edge (all orange before: "never in an ordinary frame" without the sampler's averages).

## Decision

Keep: an ordinary frame's GPU passes, on the queues where they ran, one click away in the editor, at no cost until it
is pressed.
