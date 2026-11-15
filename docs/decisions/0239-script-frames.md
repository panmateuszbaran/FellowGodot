# 0239 — Script frames in the hitch sampler's reports

Status: **KEEP** (with `rendering/fellow/debug/hitch_sampler`; GDScript's debug builds — the editor and games run from
it, debug templates) · Branch `fellow/geometry` · Code: `servers/rendering/fellow_hitch_sampler.{h,cpp}`
(`ScriptStackHooks`, `walk_script()`, `script_profile()`), `modules/gdscript/gdscript.{h,cpp}`
(`fellow_register_hitch_hooks()`, `fellow_describe_function()`) · Checks: a project whose `_process()` calls a function
that sleeps 250 ms every 60 frames, played on lavapipe.

## Problem

Decision 0231's sampler names native functions. A game's long frame is often its own script's, and the native stack of
a GDScript function is the interpreter's (`GDScriptFunction::call()` again and again): the report says a script ran, not
which. Decision 0238 shows the hitches to the developer who wrote those scripts.

## Change

GDScript's debug builds keep each thread's call levels as a linked list on its stack (`GDScriptLanguage::_call_stack`,
for the debugger's stack traces). At start-up GDScript hands the sampler the address of the calling thread's list head,
the list's layout (offsets of the next level, the function, the pointer to its current line) and a function that names
a function if it still exists (under GDScript's lock, against its list of live functions: a script reloaded since the
sample is not read); it clears them when it finishes, before taking any lock.

* A stepping thread's slot keeps its list head's address (taken on the thread itself when it registers).
* **Windows**: the head read while the thread is suspended; the levels walked in the stack's copy (their pointers were
  moved to it with the rest), under the structured exception guard. **Linux**: walked in the signal handler, on the
  thread itself. Up to 16 levels a sample, reads only.
* The functions are named when the step ends (on the watchdog thread), not at exit: the scripts are gone by then.
* The report adds the script call path and innermost functions ("`_sleepy_function (res://main.gd:25)`") under the
  native ones; the editor's list (decision 0238) shows the innermost script function of the hottest path when there is
  one.

## Verification

The project on lavapipe (the editor build), the sampler at 150 ms: each sleep's hitch has "script call levels in 2 of
2 samples", the path `_process (res://main.gd:21)` → `_sleepy_function (res://main.gd:25)`, innermost
`_sleepy_function` 100 %; the GPU's hitches and the first frame's (no script running) have none. The regression suite
and the editor check: the same.

## Decision

Keep: a script's long frame named by its script function and line.
