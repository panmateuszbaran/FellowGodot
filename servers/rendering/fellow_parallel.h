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
#include "core/templates/local_vector.h"
#include "core/templates/safe_refcount.h"

#include <atomic>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#endif

// Hint to load the cache line holding p_address (no effect on results).
_FORCE_INLINE_ void fellow_prefetch(const void *p_address) {
#if defined(__GNUC__) || defined(__clang__)
	__builtin_prefetch(p_address);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	_mm_prefetch(static_cast<const char *>(p_address), _MM_HINT_T0);
#endif
}

// Spin-wait hint (no effect on results).
_FORCE_INLINE_ void fellow_cpu_relax() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	_mm_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
	__builtin_ia32_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__aarch64__) || defined(__arm__))
	__asm__ __volatile__("yield");
#endif
}

// FellowEngine parallel loops for the render thread (docs/decisions/0016-parallel-render-lists.md).
//
// run() processes p_count chunks: the calling thread claims chunks itself, and up to p_max_helpers WorkerThreadPool
// tasks claim chunks too if they start in time. The caller returns when every chunk is done; it waits only for chunks a
// worker is processing, never for a worker to wake up (a group task's waiter sleeps until the last of its tasks has
// run, which on a laptop with E-cores can take hundreds of microseconds).
//
// Two ways to have helpers:
// - linger 0: each loop posts its worker tasks; they leave when they find no chunk left (released lazily, when the
//   loop's slot comes round again).
// - linger > 0 (hot workers): the worker tasks stay after their last chunk, spinning, for up to that many microseconds
//   after the last loop or prewarm(), and take the chunks of the next loops as soon as they are published (the frame's
//   loops follow each other within microseconds). Only missing helpers are posted. A loop is published by an epoch
//   counter: a worker claims a chunk with a compare-and-swap on (epoch, next chunk), so a worker that read a finished
//   loop's state can never claim a chunk of the next one with stale parameters.
//
// The chunks may run in any order and on any thread: callers give each chunk its own output.
class FellowParallelFor {
public:
	typedef void (*Func)(void *p_userdata, uint32_t p_chunk);

	void set_linger_usec(uint64_t p_usec) { linger_usec = p_usec; }
	uint64_t get_linger_usec() const { return linger_usec; }

	void run(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers);
	// Hot workers: posts the missing helpers now, so that they are awake when the next loop starts.
	void prewarm(uint32_t p_helpers);

	// Asynchronous loop: begin_async() publishes the chunks and returns at once; helpers take them as in run(). The
	// caller goes on with other work, runs chunks itself with help() while it waits for some of them (through
	// completion flags of its own), and calls finish() (runs the chunks left and waits for all) before any other loop of
	// this instance. A run() while an asynchronous loop is open runs its chunks on the caller alone.
	void begin_async(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers);
	bool help(); // runs one chunk nobody has claimed yet; false when none is left
	void finish();
	bool is_async_open() const { return async_open; }
	void release();

	~FellowParallelFor() { release(); }

	// One instance shared by the render thread's users (scene cull, render lists): its hot workers serve them all.
	static FellowParallelFor *shared_acquire();
	static void shared_release();

	// Helpers are optional: when disabled, every loop runs on its caller alone. Main::cleanup() disables them before the
	// WorkerThreadPool stops running new tasks (exit_languages_threads()): a task posted after that would block its
	// poster until the pool exits, and the renderer still records and flushes frames while it is finalized.
	static void set_helpers_enabled(bool p_enabled) { helpers_enabled.store(p_enabled, std::memory_order_release); }
	static bool are_helpers_enabled() { return helpers_enabled.load(std::memory_order_acquire); }

private:
	static inline std::atomic<bool> helpers_enabled{ true };

	uint64_t linger_usec = 0;

	// linger 0
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

	static void _process(Loop *p_loop, bool p_helper);
	static void _worker(void *p_loop, uint32_t p_index);

	// Hot workers.
	static constexpr uint64_t HOT_INDEX_MASK = 0xFFFFFFFFull;
	static constexpr uint64_t HOT_CLOSED = 0xFFFFFFFFull; // index of a state with no open loop
	std::atomic<uint64_t> hot_state{ HOT_CLOSED }; // (epoch << 32) | next chunk
	std::atomic<uint32_t> hot_count{ 0 };
	std::atomic<Func> hot_func{ nullptr };
	std::atomic<void *> hot_userdata{ nullptr };
	std::atomic<uint32_t> hot_done{ 0 };
	std::atomic<uint32_t> hot_alive{ 0 }; // helper tasks posted and not finished
	std::atomic<uint64_t> hot_keepalive{ 0 }; // usec ticks of the last loop or prewarm
	std::atomic<uint64_t> hot_linger{ 0 };
	std::atomic<bool> hot_exit{ false };
	uint64_t hot_epoch = 0;
	LocalVector<WorkerThreadPool::GroupID> hot_groups;

	// Asynchronous loop (caller's side).
	bool async_open = false;
	bool async_hot = false;
	uint32_t async_count = 0;
	Func async_func = nullptr;
	void *async_userdata = nullptr;
	uint64_t async_base = 0; // hot: the loop's epoch
	Loop *async_loop = nullptr; // linger 0

	void _run_hot(uint32_t p_count, Func p_func, void *p_userdata, uint32_t p_max_helpers);
	void _hot_post(uint32_t p_helpers);
	static void _hot_helper(void *p_self, uint32_t p_index);
};
