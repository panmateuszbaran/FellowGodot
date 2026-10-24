/**************************************************************************/
/*  jolt_job_system.cpp                                                   */
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

#include "jolt_job_system.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "servers/rendering/fellow_settings.h"

#include <Jolt/Physics/PhysicsSettings.h>

void JoltJobSystem::Job::_execute(void *p_user_data) {
	Job *job = static_cast<Job *>(p_user_data);

#ifdef DEBUG_ENABLED
	const uint64_t time_start = Time::get_singleton()->get_ticks_usec();
#endif

	job->Execute();

#ifdef DEBUG_ENABLED
	const uint64_t time_end = Time::get_singleton()->get_ticks_usec();
	const uint64_t time_elapsed = time_end - time_start;

	timings_lock.lock();
	timings_by_job[job->name] += time_elapsed;
	timings_lock.unlock();
#endif

	job->Release();
}

JoltJobSystem::Job::Job(const char *p_name, JPH::ColorArg p_color, JPH::JobSystem *p_job_system, const JPH::JobSystem::JobFunction &p_job_function, JPH::uint32 p_dependency_count) :
		JPH::JobSystem::Job(p_name, p_color, p_job_system, p_job_function, p_dependency_count)
#ifdef DEBUG_ENABLED
		,
		name(p_name)
#endif
{
}

JoltJobSystem::Job::~Job() {
	if (task_id != -1) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task_id);
	}
}

void JoltJobSystem::Job::push_completed(Job *p_job) {
	Job *prev_head = nullptr;

	do {
		prev_head = completed_head.load(std::memory_order_relaxed);
		p_job->completed_next = prev_head;
	} while (!completed_head.compare_exchange_weak(prev_head, p_job, std::memory_order_release, std::memory_order_relaxed));
}

JoltJobSystem::Job *JoltJobSystem::Job::pop_completed() {
	Job *prev_head = nullptr;

	do {
		prev_head = completed_head.load(std::memory_order_relaxed);
		if (prev_head == nullptr) {
			return nullptr;
		}
	} while (!completed_head.compare_exchange_weak(prev_head, prev_head->completed_next, std::memory_order_acquire, std::memory_order_relaxed));

	return prev_head;
}

void JoltJobSystem::Job::queue() {
	AddRef();

	// Ideally we would use Jolt's actual job name here, but I'd rather not incur the overhead of a memory allocation or
	// thread-safe lookup every time we create/queue a task. So instead we use the same cached description for all of them.
	static const String task_name("Jolt Physics");

	task_id = WorkerThreadPool::get_singleton()->add_native_task(&_execute, this, true, task_name);
}

int JoltJobSystem::GetMaxConcurrency() const {
	// FellowEngine (milestone 17, decision 0090): Jolt splits each phase of its step into this many jobs. Its results do
	// not depend on the number (Jolt is deterministic regardless of the job count; Physics Lab hashes the same from 1 to
	// 24 workers), and beyond about 8 the jobs' dispatch and barriers cost more than they gain (laptop, 8 P- and
	// 16 E-cores: Area3D case 4.7 ms with 24, 2.1 ms with 8; but the 2 000-body pile 3.5 ms with 24, 4.2 ms with 8).
	// -1 (the default): by the work, a job per 128 active rigid bodies, at least 4 (laptop: Physics Lab pile, 2 000
	// bodies, best with 16; areas and stack best with 4-8, chars with 4).
	const int cap = Fellow::physics_jolt_max_jobs();
	if (cap < 0) {
		return CLAMP(fellow_active_bodies / MAX(1, Fellow::physics_jolt_bodies_per_job()), MIN(4, thread_count), thread_count);
	}
	return cap > 0 ? MIN(thread_count, cap) : thread_count;
}

JPH::JobHandle JoltJobSystem::CreateJob(const char *p_name, JPH::ColorArg p_color, const JPH::JobSystem::JobFunction &p_job_function, JPH::uint32 p_dependency_count) {
	Job *job = nullptr;

	while (true) {
		JPH::uint32 job_index = jobs.ConstructObject(p_name, p_color, this, p_job_function, p_dependency_count);

		if (job_index != JPH::FixedSizeFreeList<Job>::cInvalidObjectIndex) {
			job = &jobs.Get(job_index);
			break;
		}

		WARN_PRINT_ONCE("Jolt Physics job system exceeded the maximum number of jobs. This should not happen. Please report this. Waiting for jobs to become available...");

		OS::get_singleton()->delay_usec(100);

		_reclaim_jobs();
	}

	// This will increment the job's reference count, so must happen before we queue the job
	JPH::JobHandle job_handle(job);

	if (p_dependency_count == 0) {
		QueueJob(job);
	}

	return job_handle;
}

void JoltJobSystem::QueueJob(JPH::JobSystem::Job *p_job) {
	if (Fellow::physics_jolt_job_pumps()) {
		_fellow_queue(static_cast<Job *>(p_job));
		return;
	}
	static_cast<Job *>(p_job)->queue();
}

// FellowEngine (milestone 17, decision 0092). A bounded MPMC queue (D. Vyukov's): jobs are queued by the stepping thread
// and by workers (a finished job's dependents), taken by the pumps. Jolt's barrier also runs ready jobs on the waiting
// thread (a job runs once whoever takes it: JPH::JobSystem::Job::Execute() is guarded), so a job left in the queue
// while no pump runs still gets done; the queue's reference is dropped when the job is taken.
bool JoltJobSystem::_fellow_push(Job *p_job) {
	uint32_t pos = fellow_enqueue_pos.load(std::memory_order_relaxed);
	FellowCell *cell;
	while (true) {
		cell = &fellow_cells[pos & (FELLOW_QUEUE_SIZE - 1)];
		const uint32_t seq = cell->sequence.load(std::memory_order_acquire);
		const int32_t dif = int32_t(seq - pos);
		if (dif == 0) {
			if (fellow_enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
				break;
			}
		} else if (dif < 0) {
			return false; // full
		} else {
			pos = fellow_enqueue_pos.load(std::memory_order_relaxed);
		}
	}
	cell->job = p_job;
	cell->sequence.store(pos + 1, std::memory_order_release);
	return true;
}

bool JoltJobSystem::_fellow_pop(Job *&r_job) {
	uint32_t pos = fellow_dequeue_pos.load(std::memory_order_relaxed);
	FellowCell *cell;
	while (true) {
		cell = &fellow_cells[pos & (FELLOW_QUEUE_SIZE - 1)];
		const uint32_t seq = cell->sequence.load(std::memory_order_acquire);
		const int32_t dif = int32_t(seq - (pos + 1));
		if (dif == 0) {
			if (fellow_dequeue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
				break;
			}
		} else if (dif < 0) {
			return false; // empty
		} else {
			pos = fellow_dequeue_pos.load(std::memory_order_relaxed);
		}
	}
	r_job = cell->job;
	cell->sequence.store(pos + FELLOW_QUEUE_SIZE, std::memory_order_release);
	return true;
}

void JoltJobSystem::_fellow_queue(Job *p_job) {
	p_job->fellow_take_reference();
	if (!_fellow_push(p_job)) {
		// Full (not expected: Jolt has at most cMaxPhysicsJobs jobs): stock's way.
		p_job->fellow_run();
		return;
	}
	// A pump per job up to the concurrency less one (the stepping thread runs jobs too, in Jolt's barrier).
	const int max_pumps = MAX(1, GetMaxConcurrency() - 1);
	int pumps = fellow_pumps.load(std::memory_order_acquire);
	while (pumps < max_pumps) {
		if (fellow_pumps.compare_exchange_weak(pumps, pumps + 1, std::memory_order_acq_rel)) {
			static const String task_name("Jolt Physics pump");
			const int64_t task = WorkerThreadPool::get_singleton()->add_native_task(&_fellow_pump, this, true, task_name);
			fellow_pump_tasks_lock.lock();
			fellow_pump_tasks.push_back(task);
			fellow_pump_tasks_lock.unlock();
			break;
		}
	}
}

static _FORCE_INLINE_ void _fellow_jolt_pause() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
	_mm_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
	__builtin_ia32_pause();
#endif
}

void JoltJobSystem::_fellow_pump(void *p_user_data) {
	JoltJobSystem *js = static_cast<JoltJobSystem *>(p_user_data);
	// Idle spins before leaving: about 20-80 us (Jolt queues each phase's jobs a few microseconds after the last).
	const int idle_limit = Fellow::physics_jolt_pump_spins();
	int idle = 0;
	while (true) {
		Job *job = nullptr;
		if (js->_fellow_pop(job)) {
			job->fellow_run();
			idle = 0;
			continue;
		}
		if (++idle < idle_limit) {
			_fellow_jolt_pause();
			continue;
		}
		// Leave; a job queued meanwhile either finds the pump count below the limit and starts a pump, or is taken by
		// the stepping thread's barrier.
		js->fellow_pumps.fetch_sub(1, std::memory_order_acq_rel);
		return;
	}
}

void JoltJobSystem::_fellow_drain() {
	// The pumps leave once the queue stays empty; then any job still queued (run meanwhile by the barrier) is released.
	LocalVector<int64_t> tasks;
	fellow_pump_tasks_lock.lock();
	for (int64_t t : fellow_pump_tasks) {
		tasks.push_back(t);
	}
	fellow_pump_tasks.clear();
	fellow_pump_tasks_lock.unlock();
	for (int64_t task : tasks) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
	}
	Job *job = nullptr;
	while (_fellow_pop(job)) {
		job->fellow_run();
	}
}

void JoltJobSystem::QueueJobs(JPH::JobSystem::Job **p_jobs, JPH::uint p_job_count) {
	for (JPH::uint i = 0; i < p_job_count; ++i) {
		QueueJob(p_jobs[i]);
	}
}

void JoltJobSystem::FreeJob(JPH::JobSystem::Job *p_job) {
	Job::push_completed(static_cast<Job *>(p_job));
}

void JoltJobSystem::_reclaim_jobs() {
	while (Job *job = Job::pop_completed()) {
		jobs.DestructObject(job);
	}
}

JoltJobSystem::JoltJobSystem() :
		JPH::JobSystemWithBarrier(JPH::cMaxPhysicsBarriers),
		thread_count(MAX(1, WorkerThreadPool::get_singleton()->get_thread_count())) {
	jobs.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsJobs);
	static_assert(FELLOW_QUEUE_SIZE > JPH::cMaxPhysicsJobs && (FELLOW_QUEUE_SIZE & (FELLOW_QUEUE_SIZE - 1)) == 0);
	fellow_cells = memnew_arr(FellowCell, FELLOW_QUEUE_SIZE);
	for (uint32_t i = 0; i < FELLOW_QUEUE_SIZE; i++) {
		fellow_cells[i].sequence.store(i, std::memory_order_relaxed);
		fellow_cells[i].job = nullptr;
	}
}

JoltJobSystem::~JoltJobSystem() {
	_fellow_drain();
	memdelete_arr(fellow_cells);
}

void JoltJobSystem::pre_step() {
	// Nothing to do.
}

void JoltJobSystem::post_step() {
	if (fellow_cells) {
		_fellow_drain();
	}
	_reclaim_jobs();
}

#ifdef DEBUG_ENABLED

void JoltJobSystem::flush_timings() {
	const StringName profiler_name = SNAME("servers");

	EngineDebugger *engine_debugger = EngineDebugger::get_singleton();

	if (engine_debugger->is_profiling(profiler_name)) {
		Array timings;

		for (const KeyValue<const void *, uint64_t> &E : timings_by_job) {
			timings.push_back(static_cast<const char *>(E.key));
			timings.push_back(USEC_TO_SEC(E.value));
		}

		timings.push_front("physics_3d");

		engine_debugger->profiler_add_frame_data(profiler_name, timings);
	}

	for (KeyValue<const void *, uint64_t> &E : timings_by_job) {
		E.value = 0;
	}
}

#endif
