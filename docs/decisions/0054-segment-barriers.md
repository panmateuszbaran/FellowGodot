# 0054 — Render graph: barriers between a frame's command buffers

Status: **KEEP, on by default (1) from `fellow134`** (synchronization; no measurable cost) · Branch `fellow/geometry` · Setting `rendering/fellow/debug/segment_barriers`
(runtime: 0 off, 1 resources (default), 2 also a full barrier per command buffer) · Code:
`servers/rendering/rendering_device_graph.{h,cpp}` (`fellow_frame_first_graph`, `fellow_write_frame`),
`servers/rendering/rendering_device.{h,cpp}` (`_fellow_segment_full_barrier`), `fellow_settings.*`.

## Problem

Light tile culling split around async compute (0049 v6, `debug/tile_cull_split` 0) renders City Lab differently from
one run to the next on the RTX (`m11_f154`, 60-frame sequence, two runs of the same configuration): 42 frames differ,
by one level in 1-6 pixels each, in the distance. The whole pass (split 2) is bit for bit between runs and against
culling off. On lavapipe, which runs submissions one after another, split and whole are identical (City Lab captures
and its 60-frame sequence, with async compute emulated on the main queue: 0 differing pixels): the computation is the
same, and the difference comes from the order in which the RTX executes work. Split mode is worth having: −1.9 % GPU in
Bistro interior against −0.2 % for the whole pass (`m11_f153_tcs_ab`).

FellowEngine records a frame as several render graphs: async compute (0015) ends the graph before the async part (the
prefix), records the async part in its own, then the work beside it, then the rest; early submission (0009) splits
before the swap chain image is acquired. Each graph becomes a command buffer submitted to the main queue, and a graph
begins as if a new frame began: its resource trackers keep their usage but forget the commands that wrote them. A
barrier is pushed when the usage changes, or for a write after a write recorded in the same graph. A resource written
in one graph and used again **with the same usage** in a later one gets none: no memory barrier and no execution
dependency between the two command buffers, which the GPU may overlap. Stock Godot does the same across frames (one
graph per frame).

On lavapipe (Lights Lab, 81 frames with async compute emulated, `FELLOW_SEGMENT_LOG`), 1 220 uses of this kind: storage
buffers read in the work beside the async part and after it (written by copies and compute passes in an earlier
command buffer), textures sampled again after being transitioned in an earlier one (the shadow atlas, the depth buffer),
buffers copied to in two command buffers. Vulkan's synchronization validation reports none of them (its submit-time
checks did not report a deliberately missing barrier either: not a test of this).

## Change

* **1, resources** (default): every resource tracker records the graph in which a command last wrote it (a layout
  transition counts) and the stages of those commands. A command that uses a resource written in an earlier graph of
  the same frame gets a memory barrier (all writes → its accesses) from those stages, in addition to what the usage
  tracking gives it. Not in the async part, whose submission waits on a semaphore signalled after the earlier graphs.
  Barriers are grouped per level of the graph as before: a level that already has one gets wider masks, not another.
* **2, full** (diagnostics): also a full barrier (all commands, all memory) at the start of every main-queue command
  buffer after the first of the frame.
* Stats: `rd_graph_segment_hazards` (uses of this kind, counted with any setting), `rd_segment_full_barriers`.

## Verification

lavapipe with synchronization validation, Lights Lab captures, async compute emulated on the main queue: no errors with
0, 1 or 2. Images: barriers do not change what is computed; the RTX A/A sequences below are the test.

## Measured

**Cost** (`m11_f156_sb_ab`, in-process A/B, every scene, autotune off, light tile culling on): 1 against 0, frame
−0.4 to +0.2 %, GPU −0.4 to +0.2 %, none significant (p ≥ 0.16); 2 (a full barrier at the start of the two later
command buffers) −0.9 to +0.2 %, also none significant. The barriers are free at this granularity: the levels that get
one already had one, or the GPU had nothing to overlap there.

**Determinism** (`m11_f156_aa`, City Lab and Lights Lab 60-frame sequences, ten runs: whole pass ×2, split with 1 ×2,
with 2 ×2, with 0 ×2, split without async compute ×2): in City Lab nine runs are identical bit for bit, **including the
two split runs without segment barriers**, and one run of the whole pass (`w1`) differs from all the others, by one
level in 1-6 pixels in 27 of 60 frames, at the same pixels as `m11_f154`'s deviating split run (e.g. (403, 248)).
Lights Lab: every pair at its usual floor (91.8-100.5 dB). The deviation of `m11_f154` was not the split's, then, nor
the missing barriers': one run in several of any configuration deviates at the same places. The likely cause is
outside the frame's synchronization: a pipeline compiled while the sequence is drawn (Godot draws with the ubershader
until a specialized pipeline is ready, and a sequence's camera brings new light and material combinations into view);
on lavapipe one specialization compiles during City Lab's sequence (pre-roll 8). `--temporal_warm=1` (the sequence run
once before it is captured) removes that; `m11_f160` measures the A/A pairs with it.

KEEP: the barriers close a real gap in the render graph (a command buffer could start reading what an earlier one of
the same frame was still writing, with nothing ordering them), at no measurable cost; 0 stays available.
