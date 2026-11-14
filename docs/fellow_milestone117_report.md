# FellowEngine — milestone 117 report (a hitch sampler and what it found; a garment's distance shaped)

Status: **DRAFT** (2026-11-14; job f452 — what long frames remain with the trim off — the final regression run and the settings' class reference still to do). Branch `fellow/geometry`. Milestones 114-116 left Sponza streamed with a rare long CPU
frame (25-34 ms, about one 20-second flythrough in four, the GPU's 5-9 ms) that no counter of FellowEngine explained, and
milestone 116 a garment's distance from the skin one number for the whole garment.

## 0. Result

* **A hitch sampler** (decision 0231, off by default): when a frame step runs long, the stepping thread's stack about
  every millisecond with its CPU time, the other threads' in turn; the steps over the threshold reported, named, at exit
  — in a release template, on Windows and Linux.
* **Sponza's rare long frames found and gone** (decision 0233): the render thread waited in `vkQueueSubmit()` for a lock
  the NVIDIA driver held while a worker thread freed a 256 MiB block of the memory allocator (30-34 ms) — emptied by
  decision 0225's staging trim. The trim is now off by default: three frames of 32-36 ms in eight flythroughs with it,
  none without it (up to 512 MiB of host memory kept).
* **The driver's device memory allocations and frees counted** in FellowEngine's statistics, with their time.
* **A garment's distance from the skin a share a particle** (decision 0232): growing from its pins or painted.

## 1. The hitch sampler (decision 0231)

`rendering/fellow/debug/hitch_sampler` (off), `hitch_sampler_ms` (33.3): a watchdog thread follows `Main::iteration()`
(and the render thread's draws with Godot's separate render thread). Past half the threshold it samples the stepping
thread's stack about every millisecond — on Windows suspending it only to copy its registers and stack, then unwinding
the copy from the images' unwind tables; on Linux by a signal — with the thread's CPU time, and the other threads in
turn (listed between hitches, 0.4 ms of them after each sample). A step over the threshold is a hitch (up to 64); at
exit each is printed: its length, the share the thread was on a CPU, the hottest call path, the innermost functions, the
other threads busy or not waiting with theirs, and where each waiting thread waited. Names: on Windows a PDB, else the
linker map beside the executable (`linkflags=/MAP`), else exports; on Linux `addr2line` on the executable's symbol table
(`FELLOW_KEEP_SYMBOLS=1`).

A Linux scene of four hitches over 30 ms named each: a GDScript loop (on a CPU 100 %), a sleep (3 %, `nanosleep`), a
native sort (99 %, `SortArray::introsort`), a wait for a worker's task (2 %, `wait_for_task_completion`; the worker on a
CPU 99 % in its sort). On the laptop (job f447) the first version sampled the other threads all at once after the first
sample, and a round of 48 threads outlasted the hitch; job f448's version took 2-11 samples a hitch.

## 2. What it found: a freed memory block holds the frame up (decision 0233)

Jobs f447-f448 (Sponza streamed, the sampler at 16.7 ms): in each long frame the render thread was off the CPU (0-7 %)
in `vkQueueSubmit()` — the async compute split's submission in `_pre_opaque_render()` — waiting for a lock in the NVIDIA
driver; the present thread idle; a worker thread on a CPU 100 % in the kernel's `NtGdiDdDDIDestroyAllocation2`, freeing
device memory. FellowEngine's statistics now count the driver's allocations and frees as the memory allocator makes them
(`memory_allocations`, `memory_frees`, their bytes and time; Vulkan). Frame by frame (job f450, six runs): every long
frame freed one 256 MiB block, 28-30 ms in `vkFreeMemory()`, its `vkQueueSubmit()` as long; the frame before, decision
0225's trim had released the transfer workers' staging buffers in that block of host memory.

Job f451 (eight alternating runs each):

| Sponza streamed | frames over 29 ms | longest frame | frees of a 256 MiB block | staging at the end | the driver's memory at the end |
|---|---:|---:|---:|---:|---:|
| the trim on (decision 0225) | 3 (31.7-36.2 ms) | 14.5-36.2 ms | 3 (30-34 ms each) | 128-257 MiB | 2 470-2 598 MiB |
| the trim off | 0 | 14.4-20.2 ms | 0 | 512 MiB | 2 853 MiB |

The GPU's frame the same either way (6.6-6.8 ms). `rendering/fellow/frame/transfer_staging_trim` is now off by default;
a game short of memory can turn it on, at that cost. Milestone 114 had measured the trim's longest frames at 13.4-30.7
ms against 13.5-21.1 without it and called it the runs' spread; milestone 116's stalls "in every variant" were in
variants that all trimmed.

During jobs f447-f450 the laptop's GPU was also busy with something else part of the time (Sponza's GPU passes 1.5 times
longer at higher clocks; `nvidia-smi` found it 26-29 % busy with no FellowEngine running, later 7 %): those runs are
not compared with earlier jobs' times; the finding rests on the frames' own counters.

## 3. A garment's distance shaped (decision 0232)

`FellowCloth3D.skin_distance_mode`: `SKIN_DISTANCE_UNIFORM` (decision 0230's, the default), `SKIN_DISTANCE_FROM_PINS`
(each particle's share of `skin_max_distance` its path along the cloth from the nearest pin over the longest such path:
none at a pin, all at the farthest), `SKIN_DISTANCE_VERTEX_COLOR` (painted in the source mesh's vertex colours' green).
`cloth_check.gd`'s skirt at 12 cm, the farthest each ring went from its skinned place (cm; ring 1 under the waistband,
ring 9 the hem):

| ring | 1 | 3 | 5 | 7 | 9 |
|---|---:|---:|---:|---:|---:|
| uniform | 6.8 | 9.4 | 11.2 | 12.0 | 12.0 |
| from the pins | 1.3 | 4.0 | 6.8 | 9.3 | 12.0 |
| painted (the ring's place squared) | 0.1 | 1.3 | 3.7 | 7.3 | 12.0 |

Every ring within a millimetre of its share; the waistband on its skinned place; nothing into the hips; the earlier
lanes the same.

## 4. Checks

The regression suite (36 scenes, the release template against the editor build, lavapipe): the same as its goldens with
the cloth's and the sampler's changes (one scene, `rt/raytracing`, rerun alone after the editor build was replaced
during the run: the same). Still to do: the suite on the final build (the trim's default changes no picture), and
`ProjectSettings.xml` regenerated with the new settings' descriptions.

## 5. Next

Staging that stays small (uploads split into chunks through the staging buffer) so that a trim frees small allocations;
the memory allocator's empty device-local blocks kept while textures stream (a texture's eviction that empties a block
would cost the same); the hitch sampler in the editor's debugger; cloth edges against edges.
