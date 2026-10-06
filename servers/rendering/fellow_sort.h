/**************************************************************************/
/*  fellow_sort.h                                                         */
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

#include "core/templates/local_vector.h"
#include "core/templates/sort_array.h"
#include "servers/rendering/fellow_parallel.h"

// FellowEngine parallel sort (docs/decisions/0016-parallel-render-lists.md): stock's SortArray::sort() on one or more
// arrays, with the independent parts of the work done by worker threads. The result is the one SortArray::sort() gives
// each array, ties included:
//
// - Introsort splits a range with one partition step and then sorts the two parts separately; what it does to one part
//   depends only on that part's elements and the depth budget it is given. The partition steps above the grain size
//   are taken level by level, exactly as introsort's loop takes them (the parts of one level, of all arrays, in
//   parallel); the parts left (leaves) are given to introsort itself with the depth budget they have.
// - SortArray::sort() then runs one insertion pass over the whole array. A partition step leaves no element of its
//   right part less than an element of its left part, so that pass never moves an element across a leaf boundary: it
//   does to each leaf what an insertion pass over that leaf alone does (final_insertion_sort(), run per leaf).
//   Checked against SortArray::sort() on random arrays with many ties, sorted and reversed input and exhausted depth
//   budgets (heap sort leaves), and in the engine with rendering/fellow/cpu/verify.
//
// Leaves are processed in any order, on any thread; an optional callback gets each finished leaf (to copy results out
// of the sorted range while it is in the cache).
template <typename T, typename Comparator>
class FellowParallelSort {
public:
	typedef SortArray<T, Comparator> Sorter;
	typedef void (*LeafDone)(void *p_user, const T *p_array, int64_t p_first, int64_t p_last);

	void clear() {
		level.clear();
		total = 0;
	}

	// p_user is passed to the leaf callback with this array's leaves.
	void add(T *p_array, int64_t p_size, void *p_user = nullptr) {
		if (p_size < 1) {
			return;
		}
		level.push_back({ p_array, p_user, 0, p_size, Sorter().bitlog(p_size) * 2 });
		total += p_size;
	}

	int64_t get_total() const { return total; }

	// Ranges of up to p_grain elements are sorted by one thread.
	void run(FellowParallelFor &p_for, uint32_t p_max_helpers, int64_t p_grain, LeafDone p_leaf_done = nullptr) {
		run_partitions(p_for, p_max_helpers, p_grain);
		run_leaves(p_for, p_max_helpers, p_leaf_done);
	}

	// The two halves of run() (for timestamps between them): the partition levels, then the leaves.
	void run_partitions(FellowParallelFor &p_for, uint32_t p_max_helpers, int64_t p_grain) {
		leaves.clear();
		while (!level.is_empty()) {
			split.clear();
			for (const Range &r : level) {
				if (r.last - r.first <= p_grain || r.depth == 0) {
					leaves.push_back(r); // introsort(first, last, depth) does the rest, heap sort included
				} else {
					split.push_back(r);
				}
			}
			level.clear();
			if (split.is_empty()) {
				break;
			}
			children.resize(split.size() * 2);
			p_for.run(split.size(), &_split, this, p_max_helpers);
			for (const Range &r : children) {
				if (r.last > r.first) {
					level.push_back(r);
				}
			}
		}
	}

	void run_leaves(FellowParallelFor &p_for, uint32_t p_max_helpers, LeafDone p_leaf_done = nullptr) {
		leaf_done = p_leaf_done;
		// Larger leaves first (load balance; any order gives the same result).
		leaves.template sort_custom<LeafBySize>();
		p_for.run(leaves.size(), &_leaf, this, p_max_helpers);
		total = 0;
	}

private:
	struct Range {
		T *array;
		void *user;
		int64_t first;
		int64_t last;
		int64_t depth;
	};
	struct LeafBySize {
		_FORCE_INLINE_ bool operator()(const Range &A, const Range &B) const {
			return (A.last - A.first) > (B.last - B.first);
		}
	};
	LocalVector<Range> level;
	LocalVector<Range> split;
	LocalVector<Range> children;
	LocalVector<Range> leaves;
	int64_t total = 0;
	LeafDone leaf_done = nullptr;

	static void _split(void *p_self, uint32_t p_index) {
		// One step of SortArray::introsort()'s loop on this range: the right part is what it recurses into, the left
		// part what the loop continues with, both with the decremented depth budget.
		FellowParallelSort *self = static_cast<FellowParallelSort *>(p_self);
		const Range r = self->split[p_index];
		Sorter sorter;
		const int64_t depth = r.depth - 1;
		const int64_t cut = sorter.partitioner(r.first, r.last, sorter.median_of_3_index(r.array, r.first, r.first + (r.last - r.first) / 2, r.last - 1), r.array);
		self->children[p_index * 2 + 0] = { r.array, r.user, cut, r.last, depth };
		self->children[p_index * 2 + 1] = { r.array, r.user, r.first, cut, depth };
	}

	static void _leaf(void *p_self, uint32_t p_index) {
		FellowParallelSort *self = static_cast<FellowParallelSort *>(p_self);
		const Range r = self->leaves[p_index];
		Sorter sorter;
		sorter.introsort(r.first, r.last, r.array, r.depth);
		sorter.final_insertion_sort(r.first, r.last, r.array);
		if (self->leaf_done) {
			self->leaf_done(r.user, r.array, r.first, r.last);
		}
	}
};
