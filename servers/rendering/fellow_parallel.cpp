/**************************************************************************/
/*  fellow_parallel.cpp                                                   */
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

#include "fellow_parallel.h"

#include <thread>

void FellowParallelFor::_process(Loop *p_loop) {
	while (true) {
		const uint32_t chunk = p_loop->next.postincrement();
		if (chunk >= p_loop->count) {
			return;
		}
		p_loop->func(p_loop->userdata, chunk);
		p_loop->done.increment();
	}
}

void FellowParallelFor::_worker(void *p_loop, uint32_t p_index) {
	_process(static_cast<Loop *>(p_loop));
}

void FellowParallelFor::run(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers) {
	if (p_count == 0) {
		return;
	}
	Loop *loop = &loops[slot];
	slot = (slot + 1) % SLOTS;
	if (loop->group != WorkerThreadPool::INVALID_TASK_ID) {
		// This slot's previous worker tasks (normally long finished: they found no chunk left).
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(loop->group);
		loop->group = WorkerThreadPool::INVALID_TASK_ID;
	}
	loop->count = p_count;
	loop->func = p_func;
	loop->userdata = p_userdata;
	loop->done.set(0);
	loop->next.set(0);
	const uint32_t helpers = MIN(p_count - 1, MIN(p_max_helpers, (uint32_t)WorkerThreadPool::get_singleton()->get_thread_count()));
	if (helpers > 0) {
		loop->group = WorkerThreadPool::get_singleton()->add_native_group_task(&FellowParallelFor::_worker, loop, (int)helpers, (int)helpers, true, SNAME("FellowParallelFor"));
	}
	_process(loop);
	// Chunks claimed by workers may still be in progress (one per worker at most).
	while (loop->done.get() < p_count) {
		std::this_thread::yield();
	}
}

void FellowParallelFor::release() {
	for (Loop &loop : loops) {
		if (loop.group != WorkerThreadPool::INVALID_TASK_ID) {
			WorkerThreadPool::get_singleton()->wait_for_group_task_completion(loop.group);
			loop.group = WorkerThreadPool::INVALID_TASK_ID;
		}
	}
}
