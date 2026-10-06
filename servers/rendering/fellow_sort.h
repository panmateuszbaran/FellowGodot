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
// - A large range's partition step is itself spread over the workers (exact Hoare partition): the left scan of
//   SortArray::partitioner() stops at the elements not less than the pivot, the right scan at those the pivot is not
//   less than, and the k-th left stop is swapped with the k-th right stop while it lies to its left. Comparisons against
//   the pivot's value give the same stops as the sequential scans (an element the left scan meets after the scans
//   crossed was put there by a swap and stops it too), so the stops can be found by chunks in parallel (original
//   values), the number of swaps K by a binary search (left stops increase, right stops decrease), the swaps done in
//   parallel (they are disjoint), and the cut is the left scan's last stop: min(L[K], R[K - 1]).
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

	// Ranges of up to p_grain elements are sorted by one thread; partition steps of ranges of p_parallel_partition
	// elements or more are spread over the workers (0: never).
	void run(FellowParallelFor &p_for, uint32_t p_max_helpers, int64_t p_grain, LeafDone p_leaf_done = nullptr, int64_t p_parallel_partition = 0) {
		run_partitions(p_for, p_max_helpers, p_grain, p_parallel_partition);
		run_leaves(p_for, p_max_helpers, p_leaf_done);
	}

	// The two halves of run() (for timestamps between them): the partition levels, then the leaves.
	void run_partitions(FellowParallelFor &p_for, uint32_t p_max_helpers, int64_t p_grain, int64_t p_parallel_partition = 0) {
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
			big.clear();
			small.clear();
			for (uint32_t i = 0; i < split.size(); i++) {
				if (p_parallel_partition > 0 && split[i].last - split[i].first >= p_parallel_partition) {
					big.push_back(i);
				} else {
					small.push_back(i);
				}
			}
			if (big.is_empty()) {
				p_for.run(split.size(), &_split, this, p_max_helpers);
			} else {
				_partition_big(p_for, p_max_helpers);
			}
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

	// Exact parallel partition of the large ranges of a level (see the comment at the top).
	struct BigRange {
		uint32_t split_index = 0;
		T pivot;
		uint32_t chunk_first = 0;
		uint32_t chunk_count = 0;
		int64_t left_count = 0; // left stops (elements not less than the pivot)
		int64_t right_count = 0; // right stops (elements the pivot is not less than)
		int64_t swaps = 0; // K
		int64_t cut = 0;
	};
	struct ScanChunk {
		uint32_t big = 0;
		int64_t from = 0;
		int64_t to = 0;
		LocalVector<int64_t> left; // positions, ascending
		LocalVector<int64_t> right; // positions, descending
		int64_t left_before = 0; // left stops in the range's earlier chunks
		int64_t right_after = 0; // right stops in the range's later chunks
	};
	struct SwapTask {
		uint32_t big = 0;
		int64_t from = 0; // k range
		int64_t to = 0;
	};
	LocalVector<uint32_t> big;
	LocalVector<uint32_t> small;
	LocalVector<BigRange> big_ranges;
	LocalVector<ScanChunk> scan_chunks;
	uint32_t scan_chunk_count = 0;
	LocalVector<SwapTask> swap_tasks;

	static constexpr int64_t SCAN_CHUNK_MIN = 1024;
	static constexpr int64_t SWAP_TASK_MIN = 1024;

	// The k-th left stop of a big range (ascending order) and its k-th right stop (descending order).
	_FORCE_INLINE_ int64_t _left_stop(const BigRange &p_big, int64_t p_k) const {
		uint32_t lo = p_big.chunk_first, hi = p_big.chunk_first + p_big.chunk_count - 1;
		while (lo < hi) { // last chunk whose left_before <= k
			const uint32_t mid = (lo + hi + 1) / 2;
			if (scan_chunks[mid].left_before <= p_k) {
				lo = mid;
			} else {
				hi = mid - 1;
			}
		}
		while (p_k - scan_chunks[lo].left_before >= int64_t(scan_chunks[lo].left.size())) {
			lo++; // chunks without left stops share left_before with the next one
		}
		return scan_chunks[lo].left[p_k - scan_chunks[lo].left_before];
	}
	_FORCE_INLINE_ int64_t _right_stop(const BigRange &p_big, int64_t p_k) const {
		uint32_t lo = p_big.chunk_first, hi = p_big.chunk_first + p_big.chunk_count - 1;
		while (lo < hi) { // first chunk whose right_after <= k
			const uint32_t mid = (lo + hi) / 2;
			if (scan_chunks[mid].right_after <= p_k) {
				hi = mid;
			} else {
				lo = mid + 1;
			}
		}
		while (p_k - scan_chunks[lo].right_after >= int64_t(scan_chunks[lo].right.size())) {
			lo--; // chunks without right stops share right_after with the previous one
		}
		return scan_chunks[lo].right[p_k - scan_chunks[lo].right_after];
	}

	void _partition_big(FellowParallelFor &p_for, uint32_t p_max_helpers) {
		// Scan chunks of the big ranges (and the small ranges' whole partition steps) in one loop.
		big_ranges.resize(big.size());
		scan_chunk_count = 0;
		const int64_t workers = int64_t(p_max_helpers) + 1;
		for (uint32_t b = 0; b < big.size(); b++) {
			const Range &r = split[big[b]];
			BigRange &br = big_ranges[b];
			br.split_index = big[b];
			Sorter sorter;
			br.pivot = r.array[sorter.median_of_3_index(r.array, r.first, r.first + (r.last - r.first) / 2, r.last - 1)];
			const int64_t size = r.last - r.first;
			const int64_t chunk = MAX(SCAN_CHUNK_MIN, (size + workers * 2 - 1) / (workers * 2));
			br.chunk_first = scan_chunk_count;
			br.chunk_count = uint32_t((size + chunk - 1) / chunk);
			scan_chunk_count += br.chunk_count;
			if (scan_chunks.size() < scan_chunk_count) {
				scan_chunks.resize(scan_chunk_count);
			}
			for (uint32_t c = 0; c < br.chunk_count; c++) {
				ScanChunk &sc = scan_chunks[br.chunk_first + c];
				sc.big = b;
				sc.from = r.first + c * chunk;
				sc.to = MIN(r.last, sc.from + chunk);
			}
		}
		p_for.run(scan_chunk_count + small.size(), &_scan_or_split, this, p_max_helpers);

		// Per big range: stop counts before/after each chunk, the number of swaps, the cut, the swap tasks.
		swap_tasks.clear();
		for (uint32_t b = 0; b < big.size(); b++) {
			BigRange &br = big_ranges[b];
			int64_t before = 0;
			for (uint32_t c = 0; c < br.chunk_count; c++) {
				ScanChunk &sc = scan_chunks[br.chunk_first + c];
				sc.left_before = before;
				before += sc.left.size();
			}
			br.left_count = before;
			int64_t after = 0;
			for (int64_t c = int64_t(br.chunk_count) - 1; c >= 0; c--) {
				ScanChunk &sc = scan_chunks[br.chunk_first + c];
				sc.right_after = after;
				after += sc.right.size();
			}
			br.right_count = after;
			// K: the first k with L[k] >= R[k] (or no k-th stop on a side).
			int64_t lo = 0, hi = MIN(br.left_count, br.right_count);
			while (lo < hi) {
				const int64_t mid = (lo + hi) / 2;
				if (_left_stop(br, mid) < _right_stop(br, mid)) {
					lo = mid + 1;
				} else {
					hi = mid;
				}
			}
			br.swaps = lo;
			int64_t cut = INT64_MAX;
			if (br.swaps < br.left_count) {
				cut = _left_stop(br, br.swaps);
			}
			if (br.swaps > 0) {
				cut = MIN(cut, _right_stop(br, br.swaps - 1));
			}
			br.cut = cut;
			for (int64_t k = 0; k < br.swaps; k += SWAP_TASK_MIN) {
				swap_tasks.push_back({ b, k, MIN(br.swaps, k + SWAP_TASK_MIN) });
			}
		}
		if (!swap_tasks.is_empty()) {
			p_for.run(swap_tasks.size(), &_swap_task, this, p_max_helpers);
		}
		for (uint32_t b = 0; b < big.size(); b++) {
			const BigRange &br = big_ranges[b];
			const Range &r = split[br.split_index];
			const int64_t depth = r.depth - 1;
			children[br.split_index * 2 + 0] = { r.array, r.user, br.cut, r.last, depth };
			children[br.split_index * 2 + 1] = { r.array, r.user, r.first, br.cut, depth };
		}
	}

	static void _scan_or_split(void *p_self, uint32_t p_index) {
		FellowParallelSort *self = static_cast<FellowParallelSort *>(p_self);
		if (p_index >= self->scan_chunk_count) {
			_split(p_self, self->small[p_index - self->scan_chunk_count]);
			return;
		}
		ScanChunk &sc = self->scan_chunks[p_index];
		const BigRange &br = self->big_ranges[sc.big];
		const T *a = self->split[br.split_index].array;
		const T &v = br.pivot;
		Comparator compare;
		sc.left.clear();
		sc.right.clear();
		for (int64_t i = sc.from; i < sc.to; i++) {
			if (!compare(a[i], v)) {
				sc.left.push_back(i);
			}
		}
		for (int64_t i = sc.to - 1; i >= sc.from; i--) {
			if (!compare(v, a[i])) {
				sc.right.push_back(i);
			}
		}
	}

	static void _swap_task(void *p_self, uint32_t p_index) {
		FellowParallelSort *self = static_cast<FellowParallelSort *>(p_self);
		const SwapTask &task = self->swap_tasks[p_index];
		const BigRange &br = self->big_ranges[task.big];
		T *a = self->split[br.split_index].array;
		for (int64_t k = task.from; k < task.to; k++) {
			SWAP(a[self->_left_stop(br, k)], a[self->_right_stop(br, k)]);
		}
	}

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
