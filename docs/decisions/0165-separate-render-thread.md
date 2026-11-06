# 0165 — FellowEngine with the separate render thread: GPU resources freed on it, a shader compilation deadlock fixed

Status: **KEEP** (fixes: the same frames and shaders; with the default thread model nothing changes but who compiles a
shader's last variants) · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_water_3d.{h,cpp}`,
`fellow_rain_3d.{h,cpp}`, `fellow_shallow_water_3d.{h,cpp}` (`_rt_destroy()`);
`servers/rendering/renderer_rd/shader_rd.{h,cpp}` (`FellowGroupClaims`, `_fellow_compile_claimed()`,
`_fellow_reap()`).

## Problem

Godot's separate render thread (`--render-thread separate`, project setting `rendering/driver/threads/thread_model`)
runs the frame's rendering while the main thread processes the next frame: the main thread's scene work and the render
thread's then overlap (Crowd Lab −25 % with it, decision 0077). Milestone 60 ran the slice with it on lavapipe (the
crowd in process groups too, decision 0164), and found two faults.

**GPU resources freed on the wrong thread.** Two minutes without an error during play, then at exit 80 errors —
`free_rid` "can only be called from the render thread" — and leaks (43 textures, 30 uniform sets, 7 shaders, 7 compute
pipelines). `FellowWater3D`, `FellowRain3D` and `FellowShallowWater3D` keep their RenderingDevice resources in the
node and freed them in the node's destructor, on the main thread. With the default thread model the main thread is
the render thread; with a separate one the device refuses, and the resources stay. The same happened to any of those
nodes freed during play.

**A deadlock in Godot's shader compilation.** On the cloud's two vCPUs the slice stopped after loading in two runs of
four, every thread asleep. The render thread is a task of the WorkerThreadPool (one of its two threads). Drawing a
surface, it asked for a material's shader variant (`ShaderRD::version_get_shader()`) whose version was compiling, and
waited for the version's compile group (`_compile_version_end()`) while holding the version's mutex; the pool's other
thread was compiling a pipeline of the same material and waited for that mutex. Nothing was left to run the compile
group. This is stock Godot 4.7.2's code: a thread waits for a group of pool tasks while holding a lock that pool tasks
take. It needs every pool thread blocked that way — likely where the pool is as small as the cloud's, possible with
any pool when as many pipeline tasks of one material wait for its version as there are threads, and with the default
thread model too (the main thread waiting, every worker blocked on the mutex).

## Change

* The three nodes' destructors hand the frees to the render thread (`_rt_destroy()`, what the destructor did) and,
  when the render thread is a separate one, wait for it (`RenderingServer::sync()`): the calls queued for the node
  before — bound to the node, like its per-frame dispatches — run first, and none runs on a node half destroyed. With
  the default thread model the call runs at once on the same thread, as before.
* A version group's variants are claimed one by one (an atomic flag each): a compile task compiles a variant only if
  it claims it, and the thread that waits for the group no longer only waits — it claims and compiles every variant no
  task has started, then waits for the ones started, which are running and wait for nothing. Its wait therefore never
  depends on a free worker. The group's tasks that start later find their variants claimed and return; the group is
  waited for (which frees it) once they have all run, at a later compile or when the shader is destroyed. The same
  SPIR-V and shaders, from the same function (`_compile_variant()`).

## Verification

* lavapipe, separate render thread: the slice with water and rain, 30 s and its exit: no error, nothing leaked.
* The image regression suite (default thread model; its scenes' exits included), with its shader cache warm and with
  it cold (every version compiled, the main thread helping): exact, no error.
* The slice with the separate render thread and the crowd in groups (release template, two vCPUs), its shader cache
  cold, then warm, then twice more: four runs of four to the end and their exit, no error (before: two of four hung).

## Decision

**KEEP.** The separate render thread is measured on the RTX in job f361 (Crowd Lab and Slice Lab,
`--rs_metrics=render_thread`).
