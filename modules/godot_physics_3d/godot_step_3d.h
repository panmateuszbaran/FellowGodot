/**************************************************************************/
/*  godot_step_3d.h                                                       */
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

#include "godot_space_3d.h"

#include "core/templates/local_vector.h"

#include <atomic>

class GodotStep3D {
	uint64_t _step = 1;

	int iterations = 0;
	real_t delta = 0.0;

	LocalVector<LocalVector<GodotBody3D *>> body_islands;
	LocalVector<LocalVector<GodotConstraint3D *>> constraint_islands;
	LocalVector<GodotConstraint3D *> all_constraints;

	void _populate_island(GodotBody3D *p_body, LocalVector<GodotBody3D *> &p_body_island, LocalVector<GodotConstraint3D *> &p_constraint_island);
	void _populate_island_soft_body(GodotSoftBody3D *p_soft_body, LocalVector<GodotBody3D *> &p_body_island, LocalVector<GodotConstraint3D *> &p_constraint_island);
	void _setup_constraint(uint32_t p_constraint_index, void *p_userdata = nullptr);
	void _pre_solve_island(LocalVector<GodotConstraint3D *> &p_constraint_island) const;
	void _solve_island(uint32_t p_island_index, void *p_userdata = nullptr);
	void _check_suspend(const LocalVector<GodotBody3D *> &p_body_island) const;

	// FellowEngine (milestone 17, decision 0089): a large island's iterations solved on several threads. Every
	// constraint's solve() in stock's sequence (iteration by iteration, the island's order) is an operation; an operation
	// waits for the operation before it on each of its dynamic bodies (and only those), so every body sees the same
	// updates in the same order as in stock, and operations on disjoint bodies (which commute exactly) run at once.
	struct FellowSolve {
		GodotConstraint3D **constraints = nullptr;
		uint32_t count = 0; // constraints in the island
		uint32_t iterations = 0;
		uint32_t generation = 0;
		uint32_t grab = 32; // operations a thread takes from the queue at a time
		LocalVector<uint32_t> order; // position in an iteration -> constraint index (by dependency level)
		LocalVector<uint32_t> dep; // per position and body slot: the operation waited for (see _fellow_solve_ops())
		LocalVector<uint32_t> level; // scratch: per constraint, then per level
		LocalVector<uint32_t> slot_level; // scratch: per body slot
		LocalVector<uint32_t> slot_last; // scratch: per body slot, the position of its last operation in an iteration
		LocalVector<uint32_t> con_slots; // scratch: per constraint, its two dynamic bodies' slots (UINT32_MAX: none)
		std::atomic<uint32_t> next = { 0 };
		std::atomic<uint32_t> *done = nullptr; // per operation: == generation when done
		uint32_t done_capacity = 0;
	} fellow_solve;
	bool _fellow_solve_island_parallel(LocalVector<GodotConstraint3D *> &p_constraint_island, uint32_t p_count);
	void _fellow_solve_participant(uint32_t p_index, void *p_userdata = nullptr);
	void _fellow_solve_ops();
	LocalVector<uint32_t> fellow_small_islands;
	bool fellow_constraint_lists = false; // decision 0091, read per step
	void _fellow_solve_small_island(uint32_t p_index, void *p_userdata = nullptr);

public:
	void step(GodotSpace3D *p_space, real_t p_delta);
	GodotStep3D();
	~GodotStep3D();
};
