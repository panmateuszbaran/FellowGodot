# 0165 — FellowEngine's nodes with the separate render thread

Status: **KEEP** (a fix: the same frames; with the default thread model nothing changes) · Branch `fellow/geometry` ·
Code: `modules/fellow_fluid/fellow_water_3d.{h,cpp}`, `fellow_rain_3d.{h,cpp}`, `fellow_shallow_water_3d.{h,cpp}`
(`_rt_destroy()`).

## Problem

Godot's separate render thread (`--render-thread separate`, project setting `rendering/driver/threads/thread_model`)
runs the frame's rendering while the main thread processes the next frame: the main thread's scene work and the render
thread's then overlap (Crowd Lab −25 % with it, decision 0077). Milestone 60 ran the slice with it on lavapipe (the
crowd in process groups too, decision 0164): two minutes without an error during play, then at exit 80 errors —
`free_rid` "can only be called from the render thread" — and leaks (43 textures, 30 uniform sets, 7 shaders, 7
compute pipelines). `FellowWater3D`, `FellowRain3D` and `FellowShallowWater3D` keep their RenderingDevice resources
in the node and freed them in the node's destructor, on the main thread. With the default thread model the main
thread is the render thread; with a separate one the device refuses, and the resources stay. The same happened to
any of those nodes freed during play.

## Change

The destructors hand the frees to the render thread (`_rt_destroy()`, what the destructor did) and, when the render
thread is a separate one, wait for it (`RenderingServer::sync()`): the calls queued for the node before — bound to the
node, like its per-frame dispatches — run first, and none runs on a node half destroyed. With the default thread model
the call runs at once on the same thread, as before.

## Verification

* lavapipe, separate render thread: the slice with water and rain, 30 s and its exit: no error, nothing leaked.
* The image regression suite (default thread model; its scenes' exits included): exact, no error.

## Also found: a deadlock in Godot's shader compilation with few worker threads (not fixed)

On the cloud's two vCPUs the slice with the separate render thread stopped after loading in two runs of four, every
thread asleep. The render thread is a task of the WorkerThreadPool (one of its two threads); drawing a surface
it asked for a material's shader variant (`ShaderRD::version_get_shader()`), whose version was compiling: it waited
for the version's compile group (`_compile_version_end()`) while holding the version's mutex. The pool's other thread
was compiling a pipeline of the same material and waited for that mutex. Nothing was left to run the compile group.
This is stock Godot 4.7.2's code: a thread waits for a group of pool tasks while holding a lock that pool tasks take.
It needs every pool thread to be blocked that way, so it is likely only where the pool is as small as the cloud's (the
laptop's has 24); with the default thread model the main thread, which is not one of the pool's, waits instead. A fix
would release the version's mutex while waiting (several threads then waiting for one group, which the pool does not
allow) or let a waiting pool thread run the group's remaining variants itself. Left for when the separate render
thread is made the default.

## Decision

**KEEP.** The separate render thread is measured on the RTX in job f361 (Crowd Lab and Slice Lab,
`--rs_metrics=render_thread`).
