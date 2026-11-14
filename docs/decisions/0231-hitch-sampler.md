# 0231 — A hitch sampler: where a long frame step went

Status: **KEEP** (`rendering/fellow/debug/hitch_sampler`, off by default: a diagnostic) · Branch `fellow/geometry` ·
Code: `servers/rendering/fellow_hitch_sampler.{h,cpp}`, `main/main.cpp` (`Main::iteration()`),
`servers/rendering/rendering_server_default.cpp` (`_draw()`, `finish()`) · Checks: a Linux scene of four kinds of
hitches (a script's loop, a sleep, native work, a wait for a worker thread), the laptop's Sponza streamed (jobs
f447, f448, f450).

## Problem

Milestones 114-116 measured Sponza streamed's rare long frames (25-34 ms, one in four 20-second flythroughs, the GPU's
5-9 ms) with every counter FellowEngine had — fence waits, transfer waits, pipeline compilations, the present thread's
waits, frame by frame — and found none of them in those frames: "CPU time elsewhere". A counter answers only the
question it was written for. A game studio finds a hitch with a sampling profiler or a hitch detector that captures the
game thread's stack when a frame runs long (Unreal's hitch detector and its thread heartbeat); Godot has neither in a
release build, and Windows' own samplers need administrator rights.

## Change

`rendering/fellow/debug/hitch_sampler` (off) and `hitch_sampler_ms` (33.3): a watchdog thread (high priority) follows
the main thread's frame steps (`Main::iteration()`, up to the frame delay) and, with Godot's separate render thread, its
draws. Once a step has run for half the threshold:

* **The stepping thread's stack, about every millisecond**, with its CPU time (Windows: `QueryThreadCycleTime()`; Linux:
  the thread's CPU clock): the share of the step it was on a CPU tells work from waiting.
* **The other threads in turn** (listed between hitches; sampled within 0.4 ms after each sample of the stepping
  thread), with theirs: what a waiting step waits for.
* **Windows x86-64**: the thread suspended only to copy its registers and stack (no lock is taken meanwhile: a suspended
  thread may hold the heap's or the loader's), then unwound from the images' unwind tables (`RtlVirtualUnwind()`) on the
  copy, its stack pointers moved to it. **Linux**: a signal whose handler takes the backtrace.
* A step longer than the threshold is a **hitch**; up to 64 are kept and reported when the engine exits, named —
  Windows: a PDB, else the linker map beside the executable (`linkflags=/MAP`: a release template has no PDB), else
  exports; Linux: `addr2line` on the executable's symbol table (`FELLOW_KEEP_SYMBOLS=1` at link) and the libraries'
  exports. For each: its length, when, the share on a CPU, the hottest call path (the most common function at each
  depth), the innermost functions, the other threads busy or not waiting with theirs, and a line for each waiting
  thread.
* FellowEngine's statistics: `hitches`, `hitch_samples`. Off, a frame step reads one atomic.

## Verification

Linux (the editor build, headless), a scene with four hitches over 30 ms: a GDScript loop of 60 ms (the thread on a CPU
100 %, the loop's functions), a 60 ms sleep (3 %, `nanosleep`), a native sort (99 %, `SortArray::introsort`), waiting
160 ms for a worker thread's task (2 %, `WorkerThreadPool::wait_for_task_completion`, the worker on a CPU 99 % in its
sort) — each named.

The laptop (Windows 11, the RTX 5070 Ti laptop GPU, a release template linked with a map), Sponza streamed, the sampler
at 16.7 ms (jobs f447, f448: six runs each):

* **Loading** named as such: the first frame's 239 ms in lazily created shader variants (`ShaderRD::_fellow_create_lazy`
  → the driver's SPIR-V processing), a worker filling the others meanwhile.
* **The flythrough's long frames (25-46 ms)**: the render thread was off the CPU (0-7 %), in `vkQueueSubmit()` — the
  async compute split's submission in `_pre_opaque_render()` — waiting for a lock inside the NVIDIA driver
  (`RtlAcquireSRWLockExclusive`), or, with the present thread, waiting for it to have issued the previous frame's
  submission. The present thread itself was idle; a worker thread was on a CPU 100 % in the kernel's
  `NtGdiDdDDIDestroyAllocation2`: freeing device memory, holding the lock. FellowEngine's statistics, now counting the
  driver's allocations and frees (job f450), put a number on it: each such frame freed one 256 MiB block of the memory
  allocator (28-30 ms in `vkFreeMemory()`), the frame after decision 0225's staging trim had released the transfer
  workers' staging buffers in it — decision 0233.
* Frames of 16.7-22 ms waiting for a frame's fence: the GPU's own long frames (the sampler shows the wait, not its
  cause).

The other threads were first sampled all at once after the stepping thread's first sample: on Windows one round of 48
threads (and the system-wide thread list) outlasted the hitch — one sample a hitch (job f447). The list is now made
between hitches and the others sampled in turn within 0.4 ms after each sample of the stepping thread (job f448: 2-11
samples a hitch). Suspending the threads costs the frames that are sampled; the sampler is a diagnostic, not on in a
measurement.

## Decision

Keep, off by default: a diagnostic for a project's hitches in a release build, which named Sponza's.
