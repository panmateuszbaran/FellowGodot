# FellowEngine — milestone 217 report (the rooftops' frame pass by pass; gas volumes asleep out of view)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. GPU: the RTX measured the rooftops' frame pass by pass in six
views and the gas volumes asleep and awake at 1920x1080 (jobs f611-f613). Decision 0359.

## 0. Result

* **Where the rooftops' frame goes** at 1080p on the RTX laptop (`climb_look.gd --passes=1`, new: the engine's own pass
  markers): the opaque pass 2-3.5 ms, the depth pre-pass 1.2-1.8, the sun's shadows 0.7-1.5, the clouds 0.8-1.4, the
  fire lights' shadows 0.9 (1.9 in the osteria); the whole frame 9-10 ms (6.6 looking over the roofs).
* **The gas simulation** — the brazier's, three chimneys', the osteria's fireplace — looked like the largest pass,
  3 ms in every view, but only with each pass fenced: in the real frame the GPU overlaps it with the other work (the
  frame with and without the brazier and the chimneys: 0.8 ms apart) and the render thread spends 1 ms recording it.
* **`FellowGasVolume3D.sleep_offscreen`**: a volume out of the camera's view for that long is not stepped (kept,
  drawn, its sound on), and goes on when it comes back into view. The rooftops' chimneys use it: no GPU time is saved
  on this GPU (it overlapped them already); the render thread records 0.1-0.3 ms less.
* `climb_look.gd --perf=1` draws every frame again (milestone 217's own change had broken it for a few hours).

## 1. Open

* The gas's 1 ms of the render thread: some 250 small dispatches a frame, each recorded on its own.
* Out of view is the frustum's: two chimneys seen through the osteria's arches' frustum stay awake behind its walls.
