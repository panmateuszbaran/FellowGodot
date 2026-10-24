/**************************************************************************/
/*  jolt_job_system.h                                                     */
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

#include "core/os/spin_lock.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

#include <atomic>

class JoltJobSystem final : public JPH::JobSystemWithBarrier {
	class Job : public JPH::JobSystem::Job {
		inline static std::atomic<Job *> completed_head = nullptr;

#ifdef DEBUG_ENABLED
		const char *name = nullptr;
#endif

		int64_t task_id = -1;

		std::atomic<Job *> completed_next = nullptr;

		static void _execute(void *p_user_data);

	public:
		Job(const char *p_name, JPH::ColorArg p_color, JPH::JobSystem *p_job_system, const JPH::JobSystem::JobFunction &p_job_function, JPH::uint32 p_dependency_count);
		Job(const Job &p_other) = delete;
		Job(Job &&p_other) = delete;
		~Job();

		static void push_completed(Job *p_job);
		static Job *pop_completed();

		void queue();
		// FellowEngine (decision 0092): queued for the job system's pumps instead of as a WorkerThreadPool task.
		void fellow_take_reference() { AddRef(); }
		void fellow_run() {
			Execute();
			Release();
		}

		Job &operator=(const Job &p_other) = delete;
		Job &operator=(Job &&p_other) = delete;
	};

#ifdef DEBUG_ENABLED
	// We use `const void*` here to avoid the cost of hashing the actual string, since the job names
	// are always literals and as such will point to the same address every time.
	inline static HashMap<const void *, uint64_t> timings_by_job;

	// TODO: Check whether the usage of SpinLock is justified or if this should be a mutex instead.
	inline static SpinLock timings_lock;
#endif

	JPH::FixedSizeFreeList<Job> jobs;

	int thread_count = 0;
	int fellow_active_bodies = 0; // FellowEngine (decision 0090): the stepping space's active rigid bodies

	// FellowEngine (milestone 17, decision 0092): ready jobs go to a lock-free queue drained by up to GetMaxConcurrency()
	// - 1 "pump" tasks, each running jobs until the queue has stayed empty for a while (stock: a WorkerThreadPool task
	// per job, each queued and later freed under the pool's mutex).
	static constexpr uint32_t FELLOW_QUEUE_SIZE = 4096; // a power of two above JPH::cMaxPhysicsJobs
	struct FellowCell {
		std::atomic<uint32_t> sequence;
		Job *job;
	};
	FellowCell *fellow_cells = nullptr;
	std::atomic<uint32_t> fellow_enqueue_pos = { 0 };
	std::atomic<uint32_t> fellow_dequeue_pos = { 0 };
	std::atomic<int> fellow_pumps = { 0 };
	SpinLock fellow_pump_tasks_lock;
	LocalVector<int64_t> fellow_pump_tasks;
	bool _fellow_push(Job *p_job);
	bool _fellow_pop(Job *&r_job);
	void _fellow_queue(Job *p_job);
	static void _fellow_pump(void *p_user_data);
	void _fellow_drain();

	virtual int GetMaxConcurrency() const override;

	virtual JPH::JobHandle CreateJob(const char *p_name, JPH::ColorArg p_color, const JPH::JobSystem::JobFunction &p_job_function, JPH::uint32 p_dependency_count = 0) override;
	virtual void QueueJob(JPH::JobSystem::Job *p_job) override;
	virtual void QueueJobs(JPH::JobSystem::Job **p_jobs, JPH::uint p_job_count) override;
	virtual void FreeJob(JPH::JobSystem::Job *p_job) override;

	void _reclaim_jobs();

public:
	// FellowEngine (decision 0090): set by the space before PhysicsSystem::Update().
	void fellow_set_active_bodies(int p_count) { fellow_active_bodies = p_count; }

	JoltJobSystem();
	~JoltJobSystem();

	void pre_step();
	void post_step();

#ifdef DEBUG_ENABLED
	void flush_timings();
#endif
};
