/**************************************************************************/
/*  fellow_autotune.cpp                                                   */
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

#include "fellow_autotune.h"

#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/utilities.h"

#include <cmath>

namespace Fellow {

namespace {

// An evaluation: AUTOTUNE_PAIRS pairs of blocks of autotune/block_frames frames, the tunable on in one block of each
// pair and off in the other, in the order on, off, off, on, on, off... (a drift in the GPU's clock or the view cancels
// out over two pairs). The first frames of each block are not counted: the choice just changed (a pyramid turned back on
// is rebuilt whole, pipelines of the other specialization may still be compiling).
constexpr uint32_t AUTOTUNE_PAIRS = 6;
constexpr uint32_t AUTOTUNE_BLOCKS = AUTOTUNE_PAIRS * 2;
constexpr uint32_t AUTOTUNE_SKIP = 4;
constexpr uint32_t AUTOTUNE_RING = 64; // frames whose timestamps may still be on their way back (frames in flight)
constexpr uint32_t AUTOTUNE_LATENCY = 8; // frames after an evaluation's last frame before its results are complete
// autotune/interval_frames (600): frames between evaluations, doubled while the choice holds (up to 16 times); the first
// evaluation after 40 % of it (start-up compilations, caches).
constexpr uint32_t AUTOTUNE_INTERVAL_GROWTH = 16;
constexpr double AUTOTUNE_T = 2.5; // the pairs' differences must be this many standard errors from zero

struct TuneState {
	bool choice = true; // the decision outside evaluations
	bool evaluating = false;
	bool started = false;
	uint64_t next_eval = 0;
	uint32_t interval = 0;
	uint64_t eval_start = 0;
	uint32_t block_frames = 16;
	double block_sum[AUTOTUNE_BLOCKS] = {};
	uint32_t block_n[AUTOTUNE_BLOCKS] = {};
};

struct FrameRecord {
	uint64_t id = 0;
	int8_t tunable = -1; // the tunable under evaluation in this frame, if counted
	uint8_t block = 0;
	bool has_begin = false;
	uint64_t begin = 0;
};

TuneState states[TUNE_MAX];
bool off[TUNE_MAX] = {}; // the tunable does not run in this frame
FrameRecord ring[AUTOTUNE_RING];
uint64_t frame_id = 0;
bool capturing = false; // timestamps captured for this frame

bool block_on(uint32_t p_block) {
	const uint32_t r = p_block % 4;
	return r == 0 || r == 3;
}

bool tunable_applies(Tunable p_tunable) {
	switch (p_tunable) {
		case TUNE_DIRECTIONAL_PYRAMID:
			return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/directional_max_mip");
		case TUNE_POSITIONAL_PYRAMID:
			return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/positional_max_mip");
		default:
			return false;
	}
}

void decide(TuneState &s) {
	// Per pair: GPU time with the tunable off minus on (positive: it saves time).
	double diffs[AUTOTUNE_PAIRS];
	uint32_t m = 0;
	double on_sum = 0.0;
	uint32_t on_n = 0;
	const uint32_t min_n = s.block_frames > AUTOTUNE_SKIP + 2 ? s.block_frames - AUTOTUNE_SKIP - 2 : 1;
	for (uint32_t p = 0; p < AUTOTUNE_PAIRS; p++) {
		const uint32_t a = 2 * p, b = 2 * p + 1;
		if (s.block_n[a] < min_n || s.block_n[b] < min_n) {
			continue;
		}
		const double mean_a = s.block_sum[a] / s.block_n[a];
		const double mean_b = s.block_sum[b] / s.block_n[b];
		const double on = block_on(a) ? mean_a : mean_b;
		const double off = block_on(a) ? mean_b : mean_a;
		diffs[m++] = off - on;
		on_sum += on;
		on_n++;
	}
	bool new_choice = s.choice;
	if (m >= AUTOTUNE_PAIRS - 1 && on_n > 0) {
		double mean = 0.0;
		for (uint32_t i = 0; i < m; i++) {
			mean += diffs[i];
		}
		mean /= m;
		double var = 0.0;
		for (uint32_t i = 0; i < m; i++) {
			var += (diffs[i] - mean) * (diffs[i] - mean);
		}
		var /= (m - 1);
		const double se = std::sqrt(var / m);
		const double base = on_sum / on_n;
		const double min_gain = MAX(0.0, (double)GLOBAL_GET_CACHED(float, "rendering/fellow/autotune/min_gain_percent")) * 0.01 * base;
		if (std::fabs(mean) > min_gain && (se <= 0.0 || std::fabs(mean) >= AUTOTUNE_T * se)) {
			new_choice = mean > 0.0;
		}
	}
	print_verbose(vformat("FellowEngine autotune: decide frame %d start %d block %d pairs %d choice %d->%d n0 %d n1 %d", (int64_t)frame_id, (int64_t)s.eval_start, (int)s.block_frames, (int)m, (int)s.choice, (int)new_choice, (int)s.block_n[0], (int)s.block_n[1]));
	stats().autotune_evals.increment();
	const uint32_t base = (uint32_t)MAX(1, GLOBAL_GET_CACHED(int, "rendering/fellow/autotune/interval_frames"));
	if (new_choice != s.choice) {
		stats().autotune_flips.increment();
		s.interval = base;
	} else {
		s.interval = MIN(MAX(s.interval, base) * 2, base * AUTOTUNE_INTERVAL_GROWTH);
	}
	s.choice = new_choice;
	s.evaluating = false;
	s.next_eval = frame_id + s.interval;
}

} // namespace

bool autotune_allows(Tunable p_tunable) {
	return !off[p_tunable];
}

void autotune_frame_begin() {
	frame_id++;
	capturing = false;
	const bool active = enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/autotune/enabled") && RD::get_singleton() != nullptr;
	if (!active) {
		for (bool &o : off) {
			o = false;
		}
		for (TuneState &s : states) {
			s.evaluating = false;
		}
		return;
	}

	int evaluating = -1;
	for (int t = 0; t < TUNE_MAX; t++) {
		TuneState &s = states[t];
		if (!tunable_applies(Tunable(t))) {
			if (s.evaluating) {
				print_verbose(vformat("FellowEngine autotune: tunable %d cancelled, frame %d", t, (int64_t)frame_id));
			}
			s.evaluating = false;
			off[t] = false;
			continue;
		}
		if (!s.started) {
			s.started = true;
			s.interval = (uint32_t)MAX(1, GLOBAL_GET_CACHED(int, "rendering/fellow/autotune/interval_frames"));
			s.next_eval = frame_id + s.interval * 2 / 5;
		}
		if (s.evaluating && frame_id > s.eval_start + AUTOTUNE_BLOCKS * s.block_frames + AUTOTUNE_LATENCY) {
			decide(s);
		}
		if (s.evaluating) {
			evaluating = t;
		}
	}
	if (evaluating < 0) {
		// One tunable at a time.
		for (int t = 0; t < TUNE_MAX; t++) {
			TuneState &s = states[t];
			if (tunable_applies(Tunable(t)) && frame_id >= s.next_eval) {
				s.evaluating = true;
				s.eval_start = frame_id;
				s.block_frames = (uint32_t)CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/autotune/block_frames"), AUTOTUNE_SKIP + 4, 120);
				for (uint32_t b = 0; b < AUTOTUNE_BLOCKS; b++) {
					s.block_sum[b] = 0.0;
					s.block_n[b] = 0;
				}
				evaluating = t;
				print_verbose(vformat("FellowEngine autotune: tunable %d evaluated from frame %d, blocks of %d", t, (int64_t)frame_id, (int)s.block_frames));
				break;
			}
		}
	}

	FrameRecord &rec = ring[frame_id % AUTOTUNE_RING];
	rec.id = frame_id;
	rec.tunable = -1;
	rec.has_begin = false;
	for (int t = 0; t < TUNE_MAX; t++) {
		TuneState &s = states[t];
		if (!tunable_applies(Tunable(t))) {
			continue;
		}
		bool run = s.choice;
		if (t == evaluating) {
			const uint64_t offset = frame_id - s.eval_start;
			const uint32_t block = uint32_t(offset / s.block_frames);
			if (block < AUTOTUNE_BLOCKS) {
				run = block_on(block);
				if (offset % s.block_frames >= AUTOTUNE_SKIP) {
					rec.tunable = int8_t(t);
					rec.block = uint8_t(block);
				}
			}
		}
		off[t] = !run;
		if (!run) {
			stats().autotune_off_frames.increment();
		}
	}

	// Timestamps around the frame's viewports while a tunable is evaluated (names starting with '<' and '>' are skipped
	// by the profilers' region lists).
	if (rec.tunable >= 0) {
		RSG::utilities->capture_timestamp("<FellowTune:" + itos(frame_id));
		capturing = true;
	}
}

void autotune_frame_end() {
	if (capturing) {
		RSG::utilities->capture_timestamp(">FellowTune:" + itos(frame_id));
		capturing = false;
	}
}

void autotune_timestamp(const String &p_name, uint64_t p_gpu_time) {
	const bool begin = p_name.begins_with("<FellowTune:");
	if (!begin && !p_name.begins_with(">FellowTune:")) {
		return;
	}
	const uint64_t id = (uint64_t)p_name.substr(12).to_int();
	FrameRecord &rec = ring[id % AUTOTUNE_RING];
	if (rec.id != id) {
		return; // overwritten: too old
	}
	if (begin) {
		rec.begin = p_gpu_time;
		rec.has_begin = true;
		return;
	}
	if (!rec.has_begin || rec.tunable < 0 || p_gpu_time <= rec.begin) {
		return;
	}
	TuneState &s = states[rec.tunable];
	if (!s.evaluating || id < s.eval_start || rec.block >= AUTOTUNE_BLOCKS) {
		return;
	}
	s.block_sum[rec.block] += double(p_gpu_time - rec.begin) / 1e6;
	s.block_n[rec.block]++;
	rec.tunable = -1;
}

} // namespace Fellow
