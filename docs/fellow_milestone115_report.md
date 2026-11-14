# FellowEngine — milestone 115 report (the frame's waits on uploads; cloth layers thin and with friction)

Status: **FINAL** (2026-11-14; the cloud only — the RTX's measurement of decision 0227 is to come). Branch
`fellow/geometry`. Milestone 114 traced texture streaming's long frames to the render thread waiting, at the frame's end,
for a transfer worker another thread was copying a texture into; milestone 112 left cloth self-collision by particles
only (thick layers) and without friction between its layers. And the regression suite's one intermittent difference
(`render_thread/rain`) was found.

## 0. Result

* **The frame's waits on uploads counted, and a fix ready to measure** (decision 0227, off until the RTX has measured
  it): uploads above 4 MiB made off the render thread submit their worker's earlier work and their own copy themselves.
  On lavapipe's Streaming Lab the frame's end waited 17 ms in a flythrough instead of 73, the same pictures.
* **Cloth layers a quarter of an edge apart without passing through each other** (decision 0228): particles kept off the
  other layers' triangles too — a heap's particles through another layer 71 → 0 at 2.8 cm (decision 0221 needs 8.5 cm);
  +0.1 ms a flag of 400 particles.
* **Friction between a cloth's layers** (decision 0229): a heap fallen on a 25° slope held there by its own layers
  (downhill 34 cm/s → 0).
* **The regression suite's intermittent difference explained and fixed**: captures with Godot's separate render thread
  read one frame or the next depending on the render thread's lag; they now read the frame drawn last. Six runs of the
  render-thread scenes under two CPU burners: all the same as their goldens.

## 1. The frame waiting on transfer workers (decision 0227)

RenderingDevice's uploads from several threads are batched in one transfer worker's recording; the frame's end must
take a worker to submit an upload it draws with, and a streamed texture just swapped in is such an upload, while another
streaming thread may hold the worker copying its own texture (a 4K BC7 chain is 21 MiB) into the staging buffer. The
frame's end now counts the workers it found held (`transfer_submit_waits`) and its time waiting
(`transfer_submit_wait_usec`). `rendering/fellow/frame/transfer_large_own_submit` (off): an upload above 4 MiB made off
the render thread first submits and waits for what its worker had recorded, copies, then submits and waits for its own
copy before releasing the worker; the frame's end, finding a worker held, waits only while the uploads it draws with
are not processed. A worker's texture barriers are now handed to the frame before its operations are marked processed.

lavapipe, Streaming Lab's flythrough (480 × 270, streaming on, about 1 800 loads and 870 downgrades a run):

| | the frame's end found a worker held | its waits in all | ended early | large uploads on their thread |
|---|---:|---:|---:|---:|
| off | 68 times | 73.0 ms | — | — |
| on | 47 times | 17.4 ms | 34 | 358 |

The pictures without TAA the same (0 pixels); the regression suite (36 scenes) the same as its goldens. Lavapipe's
frames are its CPU's, so the frame times wait for the RTX: Sponza streamed, p99.9 and the longest frames (milestone 114:
13-22 ms against 9-11 without streaming), and decision 0225's shorter idle times again.

## 2. The regression suite's capture with the separate render thread

Milestone 113's full run had `render_thread/rain`'s release picture differ from the editor build's (max 149 on 9.8 % of
the pixels); four reruns alone did not show it, a rerun beside another regression run did (one of three). With Godot's
thread model 2, `frame_post_draw` reaches the main thread deferred — before or after the next frame is queued, as the
render thread lags — and the capture read either frame. `fellow_agent.gd` captures, with that thread model, at the next
process frame: the frame queued last is then the one drawn last (reading the texture syncs the render thread), the same
frame `frame_post_draw` gives without the thread. Six runs of `render_thread/rain`, `water` and `fire` under two CPU
burners on the cloud's two cores: all the same as their goldens.

## 3. Cloth layers kept off each other's triangles (decision 0228)

Decision 0221 keeps a cloth's particles apart; a particle of another layer passes between two of a layer's unless the
distance is about 0.7 of a cell, so its layers lay 8.5 cm apart on a sheet of 10 cm cells. `self_collision_triangles`
(off) keeps each particle at the distance from the other layers' triangles too: once a frame the triangles' bounds,
grown by twice the distance, are hashed into the cells they cover, and each particle keeps the triangles within reach
now that were not at rest (with the side it is on); in each substep a particle nearer than the distance on its side is
pushed out along the triangle's normal, the particle and the corners by their inverse masses and weights (the distance
defaults to a quarter of the mean edge). `cloth_check.gd`'s heap in still air at 2.8 cm, at 4 s:

| | nearest two particles | particles within half the distance of another layer's triangle | nearest such |
|---|---:|---:|---:|
| particles only (decision 0221's way) | 2.76 cm | 71 | 0 (through) |
| and the triangles | 3.32 cm | 0 | 2.55 cm |
| decision 0221 at 8.5 cm | 8.47 cm | 0 | 6.19 cm |

The cost (64 flags in the wind, the release template, the cloud's worker threads a frame): none 38.2-39.4 ms,
particles 45.5-47.7, and triangles 52.4-53.2. The triangles' bounds test and cells twice the edge took it from 61-64 ms.

## 4. Friction between the layers (decision 0229)

`self_collision_friction` (0-1, 0): after a pair is pushed apart (two particles, or a particle and a triangle), that
share of their sliding along each other in the substep is taken away, by the push's weights. The sheet let go over a 25°
slope (its friction with the slope 1) falls in a heap on it; without friction between its layers the upper ones slide
down over the lowest and pull it along — 34.4 cm/s downhill at 3.5-4 s, spread over 3.7 m² — with friction 1 it stays
(−0.4 cm/s, 1.2 m²). Below 1 a heavy heap still creeps (a scratch scene: 0, 0.3 and 0.6 slid within 3-6 s). No cost of
its own.

All of `cloth_check.gd`'s earlier lanes the same, and the regression scenes `cloth` and `cape`; the editor build and the
release template the same.

## 5. Next

Decision 0227 on the RTX (Sponza streamed: its long frames); cloth as a skinned garment's simulated part (its pins
blended by the skin's weights, a distance from the skinned shape — decision 0184's "not done"); cloth edges against
edges.
