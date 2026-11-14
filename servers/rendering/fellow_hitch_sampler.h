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

} // namespace FellowHitchSampler
