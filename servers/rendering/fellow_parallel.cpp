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

#include "core/os/os.h"
#include "servers/rendering/fellow_settings.h"

#include <thread>

void FellowParallelFor::_process(Loop *p_loop, bool p_helper) {
	uint32_t chunks = 0;
	while (true) {
		const uint32_t chunk = p_loop->next.postincrement();
		if (chunk >= p_loop->count) {
			break;
		}
		p_loop->func(p_loop->userdata, chunk);
		p_loop->done.increment();
		chunks++;
	}
	if (p_helper && chunks > 0) {
		Fellow::stats().cpu_helper_chunks.add(chunks);
	}
}

void FellowParallelFor::_worker(void *p_loop, uint32_t p_index) {
	_process(static_cast<Loop *>(p_loop), true);
}

void FellowParallelFor::run(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers) {
	if (p_count == 0) {
		return;
	}
	if (linger_usec > 0) {
		_run_hot(p_count, p_func, p_userdata, p_max_helpers);
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
	const uint32_t helpers = are_helpers_enabled() ? MIN(p_count - 1, MIN(p_max_helpers, (uint32_t)WorkerThreadPool::get_singleton()->get_thread_count())) : 0;
	if (helpers > 0) {
		loop->group = WorkerThreadPool::get_singleton()->add_native_group_task(&FellowParallelFor::_worker, loop, (int)helpers, (int)helpers, true, SNAME("FellowParallelFor"));
	}
	if (p_count > 1) {
		Fellow::stats().cpu_loops.increment();
		Fellow::stats().cpu_loop_chunks.add(p_count);
	}
	_process(loop, false);
	// Chunks claimed by workers may still be in progress (one per worker at most).
	while (loop->done.get() < p_count) {
		std::this_thread::yield();
	}
}

void FellowParallelFor::_run_hot(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers) {
	hot_linger.store(linger_usec, std::memory_order_relaxed);
	hot_keepalive.store(OS::get_singleton()->get_ticks_usec(), std::memory_order_relaxed);
	if (p_count == 1) {
		p_func(p_userdata, 0);
		return;
	}

	// Publish the loop: its parameters first, then the state with the new epoch (release: a worker that reads this
	// state sees the parameters). The parameters are only written while the state is closed.
	hot_count.store(p_count, std::memory_order_relaxed);
	hot_func.store(p_func, std::memory_order_relaxed);
	hot_userdata.store(p_userdata, std::memory_order_relaxed);
	hot_done.store(0, std::memory_order_relaxed);
	hot_epoch = (hot_epoch + 1) & 0xFFFFFFFFull;
	const uint64_t base = hot_epoch << 32;
	hot_state.store(base, std::memory_order_release);
	Fellow::stats().cpu_loops.increment();
	Fellow::stats().cpu_loop_chunks.add(p_count);

	const uint32_t helpers = MIN(p_count - 1, MIN(p_max_helpers, (uint32_t)WorkerThreadPool::get_singleton()->get_thread_count()));
	if (helpers > 0) {
		_hot_post(helpers);
	}

	// Claim chunks alongside the workers.
	while (true) {
		uint64_t state = hot_state.load(std::memory_order_acquire);
		const uint32_t index = uint32_t(state & HOT_INDEX_MASK);
		if (index >= p_count) {
			break;
		}
		if (hot_state.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
			p_func(p_userdata, index);
			hot_done.fetch_add(1, std::memory_order_release);
		}
	}
	// Chunks claimed by workers may still be in progress (one per worker at most).
	while (hot_done.load(std::memory_order_acquire) < p_count) {
		fellow_cpu_relax();
	}
	hot_state.store(base | HOT_CLOSED, std::memory_order_release);
	hot_keepalive.store(OS::get_singleton()->get_ticks_usec(), std::memory_order_relaxed);
}

void FellowParallelFor::prewarm(uint32_t p_helpers) {
	if (linger_usec == 0 || p_helpers == 0) {
		return;
	}
	hot_linger.store(linger_usec, std::memory_order_relaxed);
	hot_keepalive.store(OS::get_singleton()->get_ticks_usec(), std::memory_order_relaxed);
	_hot_post(MIN(p_helpers, (uint32_t)WorkerThreadPool::get_singleton()->get_thread_count()));
}

void FellowParallelFor::_hot_post(uint32_t p_helpers) {
	if (!are_helpers_enabled()) {
		return; // The caller claims every chunk itself (helpers that are still alive may take some).
	}
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	// Free the groups whose helpers have all left.
	for (uint32_t i = 0; i < hot_groups.size();) {
		if (pool->is_group_task_completed(hot_groups[i])) {
			pool->wait_for_group_task_completion(hot_groups[i]);
			hot_groups.remove_at_unordered(i);
		} else {
			i++;
		}
	}
	const uint32_t alive = hot_alive.load(std::memory_order_acquire);
	if (alive >= p_helpers) {
		return;
	}
	const uint32_t add = p_helpers - alive;
	hot_alive.fetch_add(add, std::memory_order_acq_rel);
	hot_groups.push_back(pool->add_native_group_task(&FellowParallelFor::_hot_helper, this, (int)add, (int)add, true, SNAME("FellowParallelFor")));
}

void FellowParallelFor::_hot_helper(void *p_self, uint32_t p_index) {
	FellowParallelFor *self = static_cast<FellowParallelFor *>(p_self);
	uint64_t last_work = OS::get_singleton()->get_ticks_usec();
	uint32_t idle = 0;
	while (!self->hot_exit.load(std::memory_order_acquire)) {
		uint64_t state = self->hot_state.load(std::memory_order_acquire);
		const uint32_t index = uint32_t(state & HOT_INDEX_MASK);
		if (index != HOT_CLOSED && index < self->hot_count.load(std::memory_order_relaxed)) {
			// The parameters read here belong to this state's loop if the compare-and-swap succeeds (the state cannot
			// have changed in between, and the parameters only change after the state is closed).
			const Func func = self->hot_func.load(std::memory_order_relaxed);
			void *userdata = self->hot_userdata.load(std::memory_order_relaxed);
			if (self->hot_state.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
				func(userdata, index);
				self->hot_done.fetch_add(1, std::memory_order_release);
				Fellow::stats().cpu_helper_chunks.increment();
				idle = 0;
				last_work = 0; // read the clock at the next idle check
			}
			continue;
		}
		if (++idle < 64) {
			fellow_cpu_relax();
			continue;
		}
		idle = 0;
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		if (last_work == 0) {
			last_work = now;
		}
		const uint64_t keepalive = MAX(last_work, self->hot_keepalive.load(std::memory_order_relaxed));
		if (now > keepalive + self->hot_linger.load(std::memory_order_relaxed)) {
			break;
		}
	}
	self->hot_alive.fetch_sub(1, std::memory_order_acq_rel);
}

void FellowParallelFor::release() {
	for (Loop &loop : loops) {
		if (loop.group != WorkerThreadPool::INVALID_TASK_ID) {
			WorkerThreadPool::get_singleton()->wait_for_group_task_completion(loop.group);
			loop.group = WorkerThreadPool::INVALID_TASK_ID;
		}
	}
	if (!hot_groups.is_empty()) {
		hot_exit.store(true, std::memory_order_release);
		for (WorkerThreadPool::GroupID group : hot_groups) {
			WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);
		}
		hot_groups.clear();
		hot_exit.store(false, std::memory_order_release);
	}
}

static FellowParallelFor *fellow_shared_parallel = nullptr;
static uint32_t fellow_shared_parallel_users = 0;

FellowParallelFor *FellowParallelFor::shared_acquire() {
	if (fellow_shared_parallel_users++ == 0) {
		fellow_shared_parallel = memnew(FellowParallelFor);
	}
	return fellow_shared_parallel;
}

void FellowParallelFor::shared_release() {
	ERR_FAIL_COND(fellow_shared_parallel_users == 0);
	if (--fellow_shared_parallel_users == 0) {
		memdelete(fellow_shared_parallel);
		fellow_shared_parallel = nullptr;
	}
}
