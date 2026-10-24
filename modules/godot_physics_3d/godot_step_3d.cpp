/**************************************************************************/
/*  godot_step_3d.cpp                                                     */
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

#include "godot_step_3d.h"

#include "godot_constraint_3d.h"

#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "servers/rendering/fellow_settings.h"

#include <thread>
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

// FellowEngine (decision 0089): islands with at least this many constraints are solved on several threads.
static constexpr uint32_t FELLOW_PARALLEL_SOLVE_MIN_CONSTRAINTS = 256;

#define BODY_ISLAND_COUNT_RESERVE 128
#define BODY_ISLAND_SIZE_RESERVE 512
#define ISLAND_COUNT_RESERVE 128
#define ISLAND_SIZE_RESERVE 512
#define CONSTRAINT_COUNT_RESERVE 1024

void GodotStep3D::_populate_island(GodotBody3D *p_body, LocalVector<GodotBody3D *> &p_body_island, LocalVector<GodotConstraint3D *> &p_constraint_island) {
	p_body->set_island_step(_step);

	if (p_body->get_mode() > PhysicsServer3D::BODY_MODE_KINEMATIC) {
		// Only rigid bodies are tested for activation.
		p_body_island.push_back(p_body);
	}

	for (const KeyValue<GodotConstraint3D *, int> &E : p_body->get_constraint_map()) {
		GodotConstraint3D *constraint = E.key;
		if (constraint->get_island_step() == _step) {
			continue; // Already processed.
		}
		constraint->set_island_step(_step);
		p_constraint_island.push_back(constraint);

		all_constraints.push_back(constraint);

		// Find connected rigid bodies.
		for (int i = 0; i < constraint->get_body_count(); i++) {
			if (i == E.value) {
				continue;
			}
			GodotBody3D *other_body = constraint->get_body_ptr()[i];
			if (other_body->get_island_step() == _step) {
				continue; // Already processed.
			}
			if (other_body->get_mode() == PhysicsServer3D::BODY_MODE_STATIC) {
				continue; // Static bodies don't connect islands.
			}
			_populate_island(other_body, p_body_island, p_constraint_island);
		}

		// Find connected soft bodies.
		for (int i = 0; i < constraint->get_soft_body_count(); i++) {
			GodotSoftBody3D *soft_body = constraint->get_soft_body_ptr(i);
			if (soft_body->get_island_step() == _step) {
				continue; // Already processed.
			}
			_populate_island_soft_body(soft_body, p_body_island, p_constraint_island);
		}
	}
}

void GodotStep3D::_populate_island_soft_body(GodotSoftBody3D *p_soft_body, LocalVector<GodotBody3D *> &p_body_island, LocalVector<GodotConstraint3D *> &p_constraint_island) {
	p_soft_body->set_island_step(_step);

	for (GodotConstraint3D *E : p_soft_body->get_constraints()) {
		GodotConstraint3D *constraint = E;
		if (constraint->get_island_step() == _step) {
			continue; // Already processed.
		}
		constraint->set_island_step(_step);
		p_constraint_island.push_back(constraint);

		all_constraints.push_back(constraint);

		// Find connected rigid bodies.
		for (int i = 0; i < constraint->get_body_count(); i++) {
			GodotBody3D *body = constraint->get_body_ptr()[i];
			if (body->get_island_step() == _step) {
				continue; // Already processed.
			}
			if (body->get_mode() == PhysicsServer3D::BODY_MODE_STATIC) {
				continue; // Static bodies don't connect islands.
			}
			_populate_island(body, p_body_island, p_constraint_island);
		}
	}
}

void GodotStep3D::_setup_constraint(uint32_t p_constraint_index, void *p_userdata) {
	GodotConstraint3D *constraint = all_constraints[p_constraint_index];
	constraint->setup(delta);
}

void GodotStep3D::_pre_solve_island(LocalVector<GodotConstraint3D *> &p_constraint_island) const {
	uint32_t constraint_count = p_constraint_island.size();
	uint32_t valid_constraint_count = 0;
	for (uint32_t constraint_index = 0; constraint_index < constraint_count; ++constraint_index) {
		GodotConstraint3D *constraint = p_constraint_island[constraint_index];
		if (p_constraint_island[constraint_index]->pre_solve(delta)) {
			// Keep this constraint for solving.
			p_constraint_island[valid_constraint_count++] = constraint;
		}
	}
	p_constraint_island.resize(valid_constraint_count);
}

void GodotStep3D::_solve_island(uint32_t p_island_index, void *p_userdata) {
	LocalVector<GodotConstraint3D *> &constraint_island = constraint_islands[p_island_index];

	int current_priority = 1;

	uint32_t constraint_count = constraint_island.size();
	while (constraint_count > 0) {
		// FellowEngine (decision 0089): only called for large islands from the solving thread (step()); small islands
		// (and every island with the setting off) are solved by the worker threads as in stock.
		if (!(p_userdata == this && _fellow_solve_island_parallel(constraint_island, constraint_count))) {
			for (int i = 0; i < iterations; i++) {
				// Go through all iterations.
				for (uint32_t constraint_index = 0; constraint_index < constraint_count; ++constraint_index) {
					constraint_island[constraint_index]->solve(delta);
				}
			}
		}

		// Check priority to keep only higher priority constraints.
		uint32_t priority_constraint_count = 0;
		++current_priority;
		for (uint32_t constraint_index = 0; constraint_index < constraint_count; ++constraint_index) {
			GodotConstraint3D *constraint = constraint_island[constraint_index];
			if (constraint->get_priority() >= current_priority) {
				// Keep this constraint for the next iteration.
				constraint_island[priority_constraint_count++] = constraint;
			}
		}
		constraint_count = priority_constraint_count;
	}
}

void GodotStep3D::_check_suspend(const LocalVector<GodotBody3D *> &p_body_island) const {
	bool can_sleep = true;

	uint32_t body_count = p_body_island.size();
	for (uint32_t body_index = 0; body_index < body_count; ++body_index) {
		GodotBody3D *body = p_body_island[body_index];

		if (!body->sleep_test(delta)) {
			can_sleep = false;
		}
	}

	// Put all to sleep or wake up everyone.
	for (uint32_t body_index = 0; body_index < body_count; ++body_index) {
		GodotBody3D *body = p_body_island[body_index];

		bool active = body->is_active();

		if (active == can_sleep) {
			body->set_active(!can_sleep);
		}
	}
}

void GodotStep3D::step(GodotSpace3D *p_space, real_t p_delta) {
	p_space->lock(); // can't access space during this

	p_space->setup(); //update inertias, etc

	p_space->set_last_step(p_delta);

	iterations = p_space->get_solver_iterations();
	delta = p_delta;

	const SelfList<GodotBody3D>::List *body_list = &p_space->get_active_body_list();

	const SelfList<GodotSoftBody3D>::List *soft_body_list = &p_space->get_active_soft_body_list();

	/* INTEGRATE FORCES */

	uint64_t profile_begtime = OS::get_singleton()->get_ticks_usec();
	uint64_t profile_endtime = 0;

	int active_count = 0;

	const SelfList<GodotBody3D> *b = body_list->first();
	while (b) {
		b->self()->integrate_forces(p_delta);
		b = b->next();
		active_count++;
	}

	/* UPDATE SOFT BODY MOTION */

	const SelfList<GodotSoftBody3D> *sb = soft_body_list->first();
	while (sb) {
		sb->self()->predict_motion(p_delta);
		sb = sb->next();
		active_count++;
	}

	p_space->set_active_objects(active_count);

	// Update the broadphase to register collision pairs.
	const uint64_t fellow_broadphase_begin = OS::get_singleton()->get_ticks_usec();
	Fellow::stats().physics_godot_usec[0].add(fellow_broadphase_begin - profile_begtime);
	p_space->update();

	{ //profile
		profile_endtime = OS::get_singleton()->get_ticks_usec();
		Fellow::stats().physics_godot_usec[1].add(profile_endtime - fellow_broadphase_begin);
		p_space->set_elapsed_time(GodotSpace3D::ELAPSED_TIME_INTEGRATE_FORCES, profile_endtime - profile_begtime);
		profile_begtime = profile_endtime;
	}

	/* GENERATE CONSTRAINT ISLANDS FOR MOVING AREAS */

	uint32_t island_count = 0;

	const SelfList<GodotArea3D>::List &aml = p_space->get_moved_area_list();

	while (aml.first()) {
		for (GodotConstraint3D *E : aml.first()->self()->get_constraints()) {
			GodotConstraint3D *constraint = E;
			if (constraint->get_island_step() == _step) {
				continue;
			}
			constraint->set_island_step(_step);

			// Each constraint can be on a separate island for areas as there's no solving phase.
			++island_count;
			if (constraint_islands.size() < island_count) {
				constraint_islands.resize(island_count);
			}
			LocalVector<GodotConstraint3D *> &constraint_island = constraint_islands[island_count - 1];
			constraint_island.clear();

			all_constraints.push_back(constraint);
			constraint_island.push_back(constraint);
		}
		p_space->area_remove_from_moved_list((SelfList<GodotArea3D> *)aml.first()); //faster to remove here
	}

	/* GENERATE CONSTRAINT ISLANDS FOR ACTIVE RIGID BODIES */

	b = body_list->first();

	uint32_t body_island_count = 0;

	while (b) {
		GodotBody3D *body = b->self();

		if (body->get_island_step() != _step) {
			++body_island_count;
			if (body_islands.size() < body_island_count) {
				body_islands.resize(body_island_count);
			}
			LocalVector<GodotBody3D *> &body_island = body_islands[body_island_count - 1];
			body_island.clear();
			body_island.reserve(BODY_ISLAND_SIZE_RESERVE);

			++island_count;
			if (constraint_islands.size() < island_count) {
				constraint_islands.resize(island_count);
			}
			LocalVector<GodotConstraint3D *> &constraint_island = constraint_islands[island_count - 1];
			constraint_island.clear();
			constraint_island.reserve(ISLAND_SIZE_RESERVE);

			_populate_island(body, body_island, constraint_island);

			if (body_island.is_empty()) {
				--body_island_count;
			}

			if (constraint_island.is_empty()) {
				--island_count;
			}
		}
		b = b->next();
	}

	/* GENERATE CONSTRAINT ISLANDS FOR ACTIVE SOFT BODIES */

	sb = soft_body_list->first();
	while (sb) {
		GodotSoftBody3D *soft_body = sb->self();

		if (soft_body->get_island_step() != _step) {
			++body_island_count;
			if (body_islands.size() < body_island_count) {
				body_islands.resize(body_island_count);
			}
			LocalVector<GodotBody3D *> &body_island = body_islands[body_island_count - 1];
			body_island.clear();
			body_island.reserve(BODY_ISLAND_SIZE_RESERVE);

			++island_count;
			if (constraint_islands.size() < island_count) {
				constraint_islands.resize(island_count);
			}
			LocalVector<GodotConstraint3D *> &constraint_island = constraint_islands[island_count - 1];
			constraint_island.clear();
			constraint_island.reserve(ISLAND_SIZE_RESERVE);

			_populate_island_soft_body(soft_body, body_island, constraint_island);

			if (body_island.is_empty()) {
				--body_island_count;
			}

			if (constraint_island.is_empty()) {
				--island_count;
			}
		}
		sb = sb->next();
	}

	p_space->set_island_count((int)island_count);

	{ //profile
		profile_endtime = OS::get_singleton()->get_ticks_usec();
		p_space->set_elapsed_time(GodotSpace3D::ELAPSED_TIME_GENERATE_ISLANDS, profile_endtime - profile_begtime);
		Fellow::stats().physics_godot_usec[2].add(profile_endtime - profile_begtime); // FellowEngine (milestone 17)
		profile_begtime = profile_endtime;
	}

	/* SETUP CONSTRAINTS / PROCESS COLLISIONS */

	uint32_t total_constraint_count = all_constraints.size();
	WorkerThreadPool::GroupID group_task = WorkerThreadPool::get_singleton()->add_template_group_task(this, &GodotStep3D::_setup_constraint, nullptr, total_constraint_count, -1, true, SNAME("Physics3DConstraintSetup"));
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);

	{ //profile
		profile_endtime = OS::get_singleton()->get_ticks_usec();
		p_space->set_elapsed_time(GodotSpace3D::ELAPSED_TIME_SETUP_CONSTRAINTS, profile_endtime - profile_begtime);
		Fellow::stats().physics_godot_usec[3].add(profile_endtime - profile_begtime); // FellowEngine (milestone 17)
		profile_begtime = profile_endtime;
	}

	/* PRE-SOLVE CONSTRAINT ISLANDS */

	// WARNING: This doesn't run on threads, because it involves thread-unsafe processing.
	for (uint32_t island_index = 0; island_index < island_count; ++island_index) {
		_pre_solve_island(constraint_islands[island_index]);
	}

	/* SOLVE CONSTRAINT ISLANDS */

	// WARNING: `_solve_island` modifies the constraint islands for optimization purpose,
	// their content is not reliable after these calls and shouldn't be used anymore.
	const uint32_t fellow_min = Fellow::physics_parallel_island_solve() && WorkerThreadPool::get_singleton()->get_thread_count() > 1 ? FELLOW_PARALLEL_SOLVE_MIN_CONSTRAINTS : UINT32_MAX;
	bool fellow_large = false;
	for (uint32_t island_index = 0; island_index < island_count; ++island_index) {
		if (constraint_islands[island_index].size() >= fellow_min) {
			fellow_large = true;
			break;
		}
	}
	if (!fellow_large) {
		group_task = WorkerThreadPool::get_singleton()->add_template_group_task(this, &GodotStep3D::_solve_island, nullptr, island_count, -1, true, SNAME("Physics3DConstraintSolveIslands"));
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
	} else {
		// FellowEngine (decision 0089): the large islands one after another, each on every thread; then the others, as
		// stock solves them (islands share no body: the order between them changes nothing).
		fellow_small_islands.clear();
		for (uint32_t island_index = 0; island_index < island_count; ++island_index) {
			if (constraint_islands[island_index].size() >= fellow_min) {
				_solve_island(island_index, this);
			} else {
				fellow_small_islands.push_back(island_index);
			}
		}
		if (!fellow_small_islands.is_empty()) {
			group_task = WorkerThreadPool::get_singleton()->add_template_group_task(this, &GodotStep3D::_fellow_solve_small_island, nullptr, fellow_small_islands.size(), -1, true, SNAME("Physics3DConstraintSolveIslands"));
			WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group_task);
		}
	}

	{ //profile
		profile_endtime = OS::get_singleton()->get_ticks_usec();
		p_space->set_elapsed_time(GodotSpace3D::ELAPSED_TIME_SOLVE_CONSTRAINTS, profile_endtime - profile_begtime);
		Fellow::stats().physics_godot_usec[4].add(profile_endtime - profile_begtime); // FellowEngine (milestone 17)
		profile_begtime = profile_endtime;
	}

	/* INTEGRATE VELOCITIES */

	b = body_list->first();
	while (b) {
		const SelfList<GodotBody3D> *n = b->next();
		b->self()->integrate_velocities(p_delta);
		b = n;
	}

	/* SLEEP / WAKE UP ISLANDS */

	for (uint32_t island_index = 0; island_index < body_island_count; ++island_index) {
		_check_suspend(body_islands[island_index]);
	}

	/* UPDATE SOFT BODY CONSTRAINTS */

	sb = soft_body_list->first();
	while (sb) {
		sb->self()->solve_constraints(p_delta);
		sb = sb->next();
	}

	{ //profile
		profile_endtime = OS::get_singleton()->get_ticks_usec();
		p_space->set_elapsed_time(GodotSpace3D::ELAPSED_TIME_INTEGRATE_VELOCITIES, profile_endtime - profile_begtime);
		Fellow::stats().physics_godot_usec[5].add(profile_endtime - profile_begtime); // FellowEngine (milestone 17)
		profile_begtime = profile_endtime;
	}

	all_constraints.clear();

	p_space->unlock();
	_step++;
}

void GodotStep3D::_fellow_solve_small_island(uint32_t p_index, void *p_userdata) {
	_solve_island(fellow_small_islands[p_index]);
}

static _FORCE_INLINE_ void _fellow_pause() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
	_mm_pause();
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
	__builtin_ia32_pause();
#endif
}

static constexpr uint32_t FELLOW_DEP_NONE = UINT32_MAX;
static constexpr uint32_t FELLOW_DEP_TAIL = 0x80000000u; // the body's last operation in the previous iteration
static constexpr uint32_t FELLOW_OPS_PER_GRAB = 8;

bool GodotStep3D::_fellow_solve_island_parallel(LocalVector<GodotConstraint3D *> &p_constraint_island, uint32_t p_count) {
	FellowSolve &fs = fellow_solve;
	const uint32_t n = p_count;
	if (n < FELLOW_PARALLEL_SOLVE_MIN_CONSTRAINTS || iterations <= 0 || uint64_t(n) * uint64_t(iterations) >= uint64_t(FELLOW_DEP_TAIL)) {
		return false;
	}

	// Dynamic bodies (the only ones solve() writes) get slots; a soft body or more than two bodies: stock.
	static uint64_t stamps = 0;
	const uint64_t stamp = ++stamps;
	uint32_t slot_count = 0;
	fs.con_slots.resize(n * 2);
	for (uint32_t c = 0; c < n; c++) {
		GodotConstraint3D *con = p_constraint_island[c];
		if (con->get_soft_body_count() > 0 || con->get_body_count() > 2) {
			return false;
		}
		uint32_t *slots = &fs.con_slots[c * 2];
		slots[0] = slots[1] = UINT32_MAX;
		for (int k = 0; k < con->get_body_count(); k++) {
			GodotBody3D *b = con->get_body_ptr()[k];
			if (b->get_mode() <= PhysicsServer3D::BODY_MODE_KINEMATIC) {
				continue;
			}
			if (b->fellow_slot_stamp != stamp) {
				b->fellow_slot_stamp = stamp;
				b->fellow_slot = slot_count++;
			}
			if (slots[0] == b->fellow_slot) {
				continue; // the same body twice
			}
			slots[slots[0] == UINT32_MAX ? 0 : 1] = b->fellow_slot;
		}
	}

	// One iteration's dependency levels in stock's order (an operation: one past the deepest operation before it on its
	// bodies), the positions sorted by level (stably), each position's operations waited for.
	fs.slot_level.resize(slot_count);
	fs.slot_last.resize(slot_count);
	for (uint32_t i = 0; i < slot_count; i++) {
		fs.slot_level[i] = 0;
		fs.slot_last[i] = UINT32_MAX;
	}
	fs.level.resize(n);
	uint32_t max_level = 0;
	for (uint32_t c = 0; c < n; c++) {
		const uint32_t *slots = &fs.con_slots[c * 2];
		uint32_t lv = 0;
		for (int j = 0; j < 2; j++) {
			if (slots[j] != UINT32_MAX) {
				lv = MAX(lv, fs.slot_level[slots[j]]);
			}
		}
		lv++;
		for (int j = 0; j < 2; j++) {
			if (slots[j] != UINT32_MAX) {
				fs.slot_level[slots[j]] = lv;
			}
		}
		fs.level[c] = lv;
		max_level = MAX(max_level, lv);
	}
	// Counting sort by level: order[position] = constraint.
	LocalVector<uint32_t> &start = fs.slot_level; // reused: no longer needed per slot
	start.resize(MAX(slot_count, max_level + 2));
	for (uint32_t i = 0; i <= max_level + 1; i++) {
		start[i] = 0;
	}
	for (uint32_t c = 0; c < n; c++) {
		start[fs.level[c] + 1]++;
	}
	for (uint32_t i = 1; i <= max_level + 1; i++) {
		start[i] += start[i - 1];
	}
	fs.order.resize(n);
	for (uint32_t c = 0; c < n; c++) {
		const uint32_t pos = start[fs.level[c]]++;
		fs.order[pos] = c;
		fs.level[c] = pos; // now: the constraint's position
	}
	// Dependencies, walking the constraints in stock's order again: an operation waits for the previous operation on
	// each of its bodies in the same iteration, or, for the body's first in an iteration, its last in the previous one.
	fs.dep.resize(n * 2);
	for (uint32_t c = 0; c < n; c++) {
		const uint32_t *slots = &fs.con_slots[c * 2];
		const uint32_t pos = fs.level[c];
		for (int j = 0; j < 2; j++) {
			uint32_t d = FELLOW_DEP_NONE;
			if (slots[j] != UINT32_MAX) {
				const uint32_t prev = fs.slot_last[slots[j]];
				d = prev == UINT32_MAX ? FELLOW_DEP_TAIL : prev;
				fs.slot_last[slots[j]] = pos;
			}
			fs.dep[pos * 2 + j] = d;
		}
	}
	// The first operations' previous-iteration dependencies: the body's last position.
	for (uint32_t c = 0; c < n; c++) {
		const uint32_t *slots = &fs.con_slots[c * 2];
		const uint32_t pos = fs.level[c];
		for (int j = 0; j < 2; j++) {
			if (fs.dep[pos * 2 + j] == FELLOW_DEP_TAIL) {
				fs.dep[pos * 2 + j] = FELLOW_DEP_TAIL | fs.slot_last[slots[j]];
			}
		}
	}

	const uint32_t total = n * uint32_t(iterations);
	if (fs.done_capacity < total) {
		if (fs.done) {
			memdelete_arr(fs.done);
		}
		fs.done_capacity = total + total / 4;
		fs.done = memnew_arr(std::atomic<uint32_t>, fs.done_capacity);
		for (uint32_t i = 0; i < fs.done_capacity; i++) {
			fs.done[i].store(0, std::memory_order_relaxed);
		}
		fs.generation = 0;
	}
	fs.generation++;
	if (fs.generation == 0) {
		for (uint32_t i = 0; i < fs.done_capacity; i++) {
			fs.done[i].store(0, std::memory_order_relaxed);
		}
		fs.generation = 1;
	}
	fs.constraints = p_constraint_island.ptr();
	fs.count = n;
	fs.iterations = uint32_t(iterations);
	fs.next.store(0, std::memory_order_release);

	// Every worker thread may join; this thread works too (and alone would finish: an operation only waits for
	// earlier ones).
	const int participants = WorkerThreadPool::get_singleton()->get_thread_count();
	WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_template_group_task(this, &GodotStep3D::_fellow_solve_participant, nullptr, participants, participants, true, SNAME("Physics3DIslandSolve"));
	_fellow_solve_ops();
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);
	return true;
}

void GodotStep3D::_fellow_solve_participant(uint32_t p_index, void *p_userdata) {
	_fellow_solve_ops();
}

void GodotStep3D::_fellow_solve_ops() {
	FellowSolve &fs = fellow_solve;
	const uint32_t n = fs.count;
	const uint32_t total = n * fs.iterations;
	const uint32_t gen = fs.generation;
	while (true) {
		const uint32_t g0 = fs.next.fetch_add(FELLOW_OPS_PER_GRAB, std::memory_order_relaxed);
		if (g0 >= total) {
			return;
		}
		const uint32_t g1 = MIN(g0 + FELLOW_OPS_PER_GRAB, total);
		uint32_t k = g0 / n;
		uint32_t pos = g0 - k * n;
		for (uint32_t g = g0; g < g1; g++) {
			for (int j = 0; j < 2; j++) {
				const uint32_t d = fs.dep[pos * 2 + j];
				if (d == FELLOW_DEP_NONE) {
					continue;
				}
				uint32_t wait_op;
				if (d & FELLOW_DEP_TAIL) {
					if (k == 0) {
						continue;
					}
					wait_op = (k - 1) * n + (d & ~FELLOW_DEP_TAIL);
				} else {
					wait_op = k * n + d;
				}
				int spins = 0;
				while (fs.done[wait_op].load(std::memory_order_acquire) != gen) {
					if (++spins < 256) {
						_fellow_pause();
					} else {
						std::this_thread::yield();
					}
				}
			}
			fs.constraints[fs.order[pos]]->solve(delta);
			fs.done[g].store(gen, std::memory_order_release);
			if (++pos == n) {
				pos = 0;
				k++;
			}
		}
	}
}

GodotStep3D::GodotStep3D() {
	body_islands.reserve(BODY_ISLAND_COUNT_RESERVE);
	constraint_islands.reserve(ISLAND_COUNT_RESERVE);
	all_constraints.reserve(CONSTRAINT_COUNT_RESERVE);
}

GodotStep3D::~GodotStep3D() {
	if (fellow_solve.done) {
		memdelete_arr(fellow_solve.done);
	}
}
