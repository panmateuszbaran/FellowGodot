# 0242 — A long GPU frame's passes on a time line in the editor

Status: **KEEP** (the debugger's FellowEngine Hitches tab) · Branch `fellow/geometry` · Code:
`servers/rendering/fellow_hitch_sampler.cpp` (`gpu_frame()`: the message's passes), `modules/fellow_editor/fellow_hitch_debugger.{h,cpp}`
(`FellowGpuTimeline`) · Checks: a project played from the editor on lavapipe (a long GPU frame every 40 frames), a
screenshot of the tab.

## Problem

Decision 0238's tab gives a GPU hitch as text: the passes longest past their averages. Where in the frame they ran, and
what ran beside them on the other queue — the question decision 0241 had to answer from numbers — is what a GPU
profiler's time line shows at a glance (PIX's, Nsight's, Unreal Insights'), and what the text cannot.

## Change

A GPU hitch's message to the editor carries the frame's passes: name, queue, start and length from the frame's earliest
timestamp, and the pass's running average, and the gap before the frame. Selecting a GPU hitch in the tab draws them
above its text: a row for the main queue and one for the asynchronous compute queue, a scale whose labelled ticks are 1,
2 or 5 times a power of ten milliseconds at least 60 pixels apart, each pass grey at its average or below, reddening to
1 ms past it, orange if it never ran in an ordinary frame; its name inside where it fits, and in a tooltip with its
length, start and average.

## Verification

The project of decision 0238 played from the editor on lavapipe, Record at 150 ms: a GPU hitch selected draws the main
queue's row with "Render Opaque Pass" red across 297 of the frame's 304 ms, the scale labelled every 50 ms; the
asynchronous row empty (lavapipe has no second queue). The editor check passes.

## Decision

Keep: where a long GPU frame's time went, and on which queue, seen.
