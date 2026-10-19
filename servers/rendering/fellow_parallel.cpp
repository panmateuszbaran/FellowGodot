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

#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#include <intrin.h>
#define FELLOW_HAS_WAITPKG_CODE 1
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>
#include <x86intrin.h>
#define FELLOW_HAS_WAITPKG_CODE 1
#endif

#ifdef _WIN32
// The thread priority calls without <windows.h> (its macros collide with the engine's names).
extern "C" __declspec(dllimport) void *__stdcall GetCurrentThread();
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void *hThread, int nPriority);
#endif

// cpu/hot_workers_idle_priority (decision 0065): an idle hot worker spins at the lowest priority, so that any thread of
// normal priority that becomes ready preempts it at once on whichever core it spins (a yield only lets the threads
// queued on its own core run). Back to normal before it claims a chunk (never preempted holding one by a thread that
// would not have preempted it before). Windows only (elsewhere a thread cannot raise its priority back unprivileged).
static void fellow_hot_priority(bool p_low) {
#ifdef _WIN32
	SetThreadPriority(GetCurrentThread(), p_low ? -2 : 0); // THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_NORMAL
#else
	(void)p_low;
#endif
}

bool fellow_waitpkg_supported() {
#ifdef FELLOW_HAS_WAITPKG_CODE
	static const bool supported = []() {
#if defined(_MSC_VER)
		int regs[4] = {};
		__cpuid(regs, 0);
		if (regs[0] < 7) {
			return false;
		}
		__cpuidex(regs, 7, 0);
		return (regs[2] & (1 << 5)) != 0; // CPUID.(EAX=7,ECX=0):ECX[5] WAITPKG
#else
		unsigned int a = 0, b = 0, c = 0, d = 0;
		if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) {
			return false;
		}
		return (c & (1u << 5)) != 0;
#endif
	}();
	return supported;
#else
	return false;
#endif
}

#ifdef FELLOW_HAS_WAITPKG_CODE
#if !defined(_MSC_VER)
__attribute__((target("waitpkg")))
#endif
static bool _fellow_umwait(const void *p_address, uint64_t p_seen, bool p_deep, uint64_t p_max_tsc) {
	// Arm the monitor first, then check the value again: a write after the check wakes the wait.
	_umonitor(const_cast<void *>(p_address));
	if (reinterpret_cast<const std::atomic<uint64_t> *>(p_address)->load(std::memory_order_acquire) != p_seen) {
		return true;
	}
	// Control bit 0: 0 = C0.2 (lower power, slower wake), 1 = C0.1.
	_umwait(p_deep ? 0u : 1u, __rdtsc() + p_max_tsc);
	return true;
}
#endif

bool fellow_wait_change(const void *p_address, uint64_t p_seen, bool p_deep, uint64_t p_max_tsc) {
#ifdef FELLOW_HAS_WAITPKG_CODE
	if (fellow_waitpkg_supported()) {
		return _fellow_umwait(p_address, p_seen, p_deep, p_max_tsc);
	}
#endif
	return false;
}

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
	if (async_open) {
		DEV_ASSERT(false && "FellowParallelFor::run() while an asynchronous loop is open.");
		for (uint32_t i = 0; i < p_count; i++) {
			p_func(p_userdata, i);
		}
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
	hot_wait_mode.store(Fellow::cpu_hot_workers_wait(), std::memory_order_relaxed);
	hot_yield.store(Fellow::cpu_hot_workers_yield(), std::memory_order_relaxed);
	hot_idle_priority.store(Fellow::cpu_hot_workers_idle_priority(), std::memory_order_relaxed);
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

void FellowParallelFor::begin_async(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers) {
	ERR_FAIL_COND(async_open);
	async_open = true;
	async_count = p_count;
	async_func = p_func;
	async_userdata = p_userdata;
	if (p_count == 0) {
		async_hot = false;
		async_loop = nullptr;
		return;
	}
	Fellow::stats().cpu_loops.increment();
	Fellow::stats().cpu_loop_chunks.add(p_count);
	// The caller has other work to do: every chunk may go to a helper.
	const uint32_t helpers = are_helpers_enabled() ? MIN(p_count, MIN(p_max_helpers, (uint32_t)WorkerThreadPool::get_singleton()->get_thread_count())) : 0;
	if (linger_usec > 0) {
		async_hot = true;
		hot_linger.store(linger_usec, std::memory_order_relaxed);
		hot_wait_mode.store(Fellow::cpu_hot_workers_wait(), std::memory_order_relaxed);
		hot_yield.store(Fellow::cpu_hot_workers_yield(), std::memory_order_relaxed);
		hot_idle_priority.store(Fellow::cpu_hot_workers_idle_priority(), std::memory_order_relaxed);
		hot_keepalive.store(OS::get_singleton()->get_ticks_usec(), std::memory_order_relaxed);
		// Published as in _run_hot().
		hot_count.store(p_count, std::memory_order_relaxed);
		hot_func.store(p_func, std::memory_order_relaxed);
		hot_userdata.store(p_userdata, std::memory_order_relaxed);
		hot_done.store(0, std::memory_order_relaxed);
		hot_epoch = (hot_epoch + 1) & 0xFFFFFFFFull;
		async_base = hot_epoch << 32;
		hot_state.store(async_base, std::memory_order_release);
		if (helpers > 0) {
			_hot_post(helpers);
		}
		return;
	}
	async_hot = false;
	Loop *loop = &loops[slot];
	slot = (slot + 1) % SLOTS;
	if (loop->group != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(loop->group);
		loop->group = WorkerThreadPool::INVALID_TASK_ID;
	}
	loop->count = p_count;
	loop->func = p_func;
	loop->userdata = p_userdata;
	loop->done.set(0);
	loop->next.set(0);
	if (helpers > 0) {
		loop->group = WorkerThreadPool::get_singleton()->add_native_group_task(&FellowParallelFor::_worker, loop, (int)helpers, (int)helpers, true, SNAME("FellowParallelFor"));
	}
	async_loop = loop;
}

bool FellowParallelFor::help() {
	if (!async_open || async_count == 0) {
		return false;
	}
	if (async_hot) {
		while (true) {
			uint64_t state = hot_state.load(std::memory_order_acquire);
			const uint32_t index = uint32_t(state & HOT_INDEX_MASK);
			if (index >= async_count) {
				return false;
			}
			if (hot_state.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
				async_func(async_userdata, index);
				hot_done.fetch_add(1, std::memory_order_release);
				return true;
			}
		}
	}
	const uint32_t chunk = async_loop->next.postincrement();
	if (chunk >= async_loop->count) {
		return false;
	}
	async_func(async_userdata, chunk);
	async_loop->done.increment();
	return true;
}

void FellowParallelFor::finish() {
	if (!async_open) {
		return;
	}
	while (help()) {
	}
	if (async_count > 0) {
		if (async_hot) {
			while (hot_done.load(std::memory_order_acquire) < async_count) {
				fellow_cpu_relax();
			}
			hot_state.store(async_base | HOT_CLOSED, std::memory_order_release);
			hot_keepalive.store(OS::get_singleton()->get_ticks_usec(), std::memory_order_relaxed);
		} else {
			while (async_loop->done.get() < async_count) {
				std::this_thread::yield();
			}
		}
	}
	async_open = false;
	async_loop = nullptr;
}

void FellowParallelFor::prewarm(uint32_t p_helpers) {
	if (linger_usec == 0 || p_helpers == 0) {
		return;
	}
	hot_linger.store(linger_usec, std::memory_order_relaxed);
	hot_wait_mode.store(Fellow::cpu_hot_workers_wait(), std::memory_order_relaxed);
	hot_yield.store(Fellow::cpu_hot_workers_yield(), std::memory_order_relaxed);
	hot_idle_priority.store(Fellow::cpu_hot_workers_idle_priority(), std::memory_order_relaxed);
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
	uint64_t waits = 0;
	uint64_t yields = 0;
	bool low = false;
	uint32_t idle_checks = 0; // idle checks (every 64 pauses) since the last chunk
	while (!self->hot_exit.load(std::memory_order_acquire)) {
		uint64_t state = self->hot_state.load(std::memory_order_acquire);
		const uint32_t index = uint32_t(state & HOT_INDEX_MASK);
		if (index != HOT_CLOSED && index < self->hot_count.load(std::memory_order_relaxed)) {
			if (low) {
				fellow_hot_priority(false);
				low = false;
				continue; // the state may have moved meanwhile
			}
			// The parameters read here belong to this state's loop if the compare-and-swap succeeds (the state cannot
			// have changed in between, and the parameters only change after the state is closed).
			const Func func = self->hot_func.load(std::memory_order_relaxed);
			void *userdata = self->hot_userdata.load(std::memory_order_relaxed);
			if (self->hot_state.compare_exchange_weak(state, state + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
				func(userdata, index);
				self->hot_done.fetch_add(1, std::memory_order_release);
				Fellow::stats().cpu_helper_chunks.increment();
				idle = 0;
				idle_checks = 0;
				last_work = 0; // read the clock at the next idle check
			}
			continue;
		}
		const int wait_mode = self->hot_wait_mode.load(std::memory_order_relaxed);
		if (wait_mode > 0 && idle >= 16 && fellow_wait_change(&self->hot_state, state, wait_mode == 2, FELLOW_UMWAIT_TSC)) {
			// Low-power wait (cpu/hot_workers_wait) until the state changes (a loop is published or closed) or ~10 µs
			// pass; then the linger is checked as below.
			waits++;
			idle = 64;
		}
		if (++idle < 64) {
			fellow_cpu_relax();
			continue;
		}
		idle = 0;
		// Lowered after about 10-20 µs without work: the loops of a frame follow each other within microseconds.
		if (!low && ++idle_checks >= 4 && self->hot_idle_priority.load(std::memory_order_relaxed)) {
			fellow_hot_priority(true);
			low = true;
			Fellow::stats().cpu_hot_lowered.increment();
		}
		if (self->hot_yield.load(std::memory_order_relaxed)) {
			// cpu/hot_workers_yield (decision 0065): a thread waiting for this core runs now, not after a quantum.
			std::this_thread::yield();
			yields++;
		}
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		if (last_work == 0) {
			last_work = now;
		}
		const uint64_t keepalive = MAX(last_work, self->hot_keepalive.load(std::memory_order_relaxed));
		if (now > keepalive + self->hot_linger.load(std::memory_order_relaxed)) {
			break;
		}
	}
	if (low) {
		fellow_hot_priority(false); // a pool thread: other tasks run on it next
	}
	if (waits > 0) {
		Fellow::stats().cpu_hot_waits.add(waits);
	}
	if (yields > 0) {
		Fellow::stats().cpu_hot_yields.add(yields);
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
