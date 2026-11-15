/**************************************************************************/
/*  fellow_hitch_sampler.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/typedefs.h"

class String;

// FellowEngine hitch sampler (milestone 117, docs/decisions/0231-hitch-sampler.md).
//
// With rendering/fellow/debug/hitch_sampler on, a watchdog thread follows the main thread's frame steps
// (Main::iteration()) and, when Godot's separate render thread draws, its draws. Once a step has run for half of
// rendering/fellow/debug/hitch_sampler_ms, the stepping thread's call stack is sampled about every millisecond, with
// the CPU time it got, and the other threads' stacks every fourth sample. A step longer than the threshold is a hitch:
// its samples are kept (up to 64 hitches) and reported, with function names, when the engine exits: the hottest call
// path, the innermost functions, the share of the step the thread spent on a CPU (a thread that waits or is not
// scheduled gets none), and what the busiest other threads were doing meanwhile.
//
// Windows x86-64 (stacks copied while the thread is suspended, unwound from its unwind tables; names from a PDB, or
// from a linker map next to the executable: build with linkflags=/MAP) and Linux (a signal; names from the
// executable's symbol table: FELLOW_KEEP_SYMBOLS=1 at link). Elsewhere the setting does nothing.
namespace FellowHitchSampler {

enum Slot {
	SLOT_MAIN, // Main::iteration()
	SLOT_DRAW, // RenderingServerDefault::_draw() on a thread other than the main thread
	SLOT_MAX,
};

void step_begin(Slot p_slot);
void step_end(Slot p_slot);
// Stops the watchdog and prints the report (RenderingServerDefault::finish(), before the RenderingDevice goes).
void finish();

// FellowEngine's statistics: the steps reported as hitches, and the stepping threads' stacks sampled.
uint64_t get_hitch_count();
uint64_t get_sample_count();

// The GPU's side (milestone 119, docs/decisions/0237-gpu-hitches.md), on every platform: with the sampler on, the
// renderer's pass timestamps are captured every frame (as with rendering/fellow/profiling/pass_timestamps). A frame
// whose passes took the GPU longer than the threshold from the first timestamp to the last, or whose first timestamp came
// that long after the previous frame's last (beyond what the CPU's own time explains), is a GPU hitch: kept (up to 64)
// with the passes longest past their average
// and reported with the CPU's hitches.
bool gpu_wanted(); // the sampler is on: capture the timestamps
// A frame's timestamps as RenderingServer reads them back: names, GPU times in nanoseconds, the CPU's times (ticks in
// microseconds) when the first and the last were recorded, the render thread's wait for the GPU when the frame began;
// p_frame the frame number RenderingDevice gave them. A gap the CPU explains (its own time between recording the two
// frames: the GPU waited for the frame) is not the GPU's hitch.
void gpu_frame(uint64_t p_frame, uint32_t p_count, const String *p_names, const uint64_t *p_gpu_nsec, uint64_t p_cpu_usec, uint64_t p_cpu_last_usec, uint64_t p_cpu_wait_usec);
uint64_t get_gpu_hitch_count();

// The editor's debugger (milestone 119, docs/decisions/0238-hitches-in-the-debugger.md): when the game runs from the
// editor, each hitch is named on a thread of its own as it happens and sent to the debugger's FellowEngine Hitches tab
// ("fellow_hitch:cpu", "fellow_hitch:gpu"); the tab can start the sampler in a running game ("fellow_hitch:start").
void register_debugger(); // with the debugger active, the "fellow_hitch" message capture (FellowEngine's settings)

// A script language's call stack, sampled with the native one (milestone 119, docs/decisions/0239-script-frames.md):
// GDScript's debug builds keep a linked list of call levels on the stack of the thread that runs them.
struct ScriptStackHooks {
	void **(*current_head_slot)() = nullptr; // the calling thread's innermost call level pointer's address
	uint32_t level_size = 0; // a call level's size, and in it the offsets of
	uint32_t prev_offset = 0; // the next call level out
	uint32_t function_offset = 0; // the function (opaque)
	uint32_t line_offset = 0; // a pointer to its current line (an int)
	// The function's script and name, if it still exists (on any thread; a language lock taken). Not called after the
	// language has cleared the hooks.
	bool (*describe)(void *p_function, String &r_name) = nullptr;
};
void set_script_stack_hooks(const ScriptStackHooks &p_hooks); // cleared with ScriptStackHooks() before the language goes

} // namespace FellowHitchSampler
