# 0238 — Hitches in the editor's debugger

Status: **KEEP** (the debugger's FellowEngine Hitches tab; the sampler off by default) · Branch `fellow/geometry` ·
Code: `servers/rendering/fellow_hitch_sampler.{h,cpp}` (`live_queue()`, `live_namer()`, `live_post()`, `live_flush()`,
`register_debugger()`), `modules/fellow_editor/fellow_hitch_debugger.{h,cpp}`, `fellow_editor_plugin.cpp` · Checks: a
project played from the editor on lavapipe (a long GPU frame every 40 frames, a 250 ms sleep every 60), the editor
check.

## Problem

Decisions 0231 and 0237 report hitches when the engine exits, in its output. A developer playing the game from the
editor wants them where the editor's profilers are, as they happen, next to the moment that caused them, and to start
the sampler without editing the project's settings and restarting. Godot's debugger has a profiler for scripts and a
visual profiler of the last frames, neither for the one frame that was long.

## Change

* **In the game**: with the debugger attached, a CPU hitch is handed to a low-priority thread of its own that names its
  addresses (decision 0231's `symbolize()`: on Windows the linker map is now read once, not at each call) and writes
  the same text the exit report prints; a GPU hitch's text is made where it is found. Both go into an outbox the main
  thread sends at its next frame step (`fellow_hitch:cpu`, `fellow_hitch:gpu`: the number, when, how long, where — the
  hottest path's innermost function of the executable's, or the GPU's pass longest past its average — and the text),
  so that nothing is sent after the main loop, when the debugger is gone. At most 8 CPU hitches wait to be named and
  64 messages to be sent; the rest are in the exit report.
* **The message `fellow_hitch:start`** [threshold in ms]: turns the sampler on in a running game (if its watchdog has
  not run) or changes its threshold.
* **In the editor**: the debugger's "FellowEngine Hitches" tab (an `EditorDebuggerPlugin` added by FellowEngine's
  editor plugin): Record (sends the start message now, and when the next game starts), the threshold, Clear; a list of
  the hitches (when, CPU or GPU, how long, where) and the selected one's text.

## Verification

A project played from the editor on lavapipe, the sampler off in its settings, Record ticked at 150 ms: the game
printed "started from the editor"; after 25 s the tab listed 81 hitches — the GPU's long frames ("Render Opaque Pass",
the full-screen shader looping) and the main thread's steps that waited for them (`RenderingDevice::_fellow_present_wait`),
the sleeps (`OS_Unix::delay_usec`) and the first frame's shader compilation (`compute_pipeline_create`) — each with
the exit report's text. The editor check passes; the regression suite is the same as its goldens.

## Decision

Keep: the sampler's findings where a developer looks while playing, without a restart.
