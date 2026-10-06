/**************************************************************************/
/*  fellow_parallel.h                                                     */
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

#include "core/object/worker_thread_pool.h"
#include "core/templates/safe_refcount.h"

// FellowEngine parallel loops for the render thread (docs/decisions/0016-parallel-render-lists.md).
//
// run() processes p_count chunks: the calling thread claims chunks itself, and up to p_max_helpers WorkerThreadPool
// tasks posted alongside claim chunks too if they start in time. The caller returns when every chunk is done; it waits
// only for chunks a worker is processing, never for a worker to wake up (a group task's waiter sleeps until the last
// of its tasks has run, which on a laptop with E-cores can take hundreds of microseconds). The worker tasks of a loop
// are released when its slot comes round again (they find no chunk left and finish at once).
//
// The chunks may run in any order and on any thread: callers give each chunk its own output.
class FellowParallelFor {
public:
	typedef void (*Func)(void *p_userdata, uint32_t p_chunk);

	void run(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers);
	void release();

	~FellowParallelFor() { release(); }

private:
	struct Loop {
		SafeNumeric<uint32_t> next; // next chunk to claim
		SafeNumeric<uint32_t> done; // chunks finished
		uint32_t count = 0;
		Func func = nullptr;
		void *userdata = nullptr;
		WorkerThreadPool::GroupID group = WorkerThreadPool::INVALID_TASK_ID;
	};
	static constexpr uint32_t SLOTS = 32;
	Loop loops[SLOTS];
	uint32_t slot = 0;

	static void _process(Loop *p_loop);
	static void _worker(void *p_loop, uint32_t p_index);
};
