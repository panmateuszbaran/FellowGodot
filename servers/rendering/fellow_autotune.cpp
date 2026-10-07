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

#include "core/os/os.h"
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
constexpr uint32_t AUTOTUNE_EARLY_PAIRS = 3; // an evaluation may end after this many pairs if the difference is clear:
constexpr double AUTOTUNE_EARLY_GAIN = 0.03; // at least 3 % of the GPU time
constexpr double AUTOTUNE_EARLY_T = 4.0; // and 4 standard errors

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
uint64_t last_begin_usec = 0;

bool block_on(uint32_t p_block) {
	const uint32_t r = p_block % 4;
	return r == 0 || r == 3;
}

bool uses_frame_time(int p_tunable) {
	return p_tunable == TUNE_ASYNC_COMPUTE || p_tunable == TUNE_FRAME_PLAN;
}

bool tunable_applies(Tunable p_tunable) {
	switch (p_tunable) {
		case TUNE_DIRECTIONAL_PYRAMID:
			return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/directional_max_mip");
		case TUNE_POSITIONAL_PYRAMID:
			return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/positional_max_mip");
		case TUNE_ASYNC_COMPUTE:
			return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/async_compute");
		case TUNE_FRAME_PLAN:
			return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/replay_frame_plan") && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/parallel_replay");
		default:
			return false;
	}
}

struct Analysis {
	uint32_t pairs = 0;
	double mean = 0.0; // GPU ms, off minus on (positive: the optimization saves time)
	double se = 0.0;
	double base = 0.0; // GPU ms with the optimization on
};

Analysis analyze(const TuneState &s, uint32_t p_pairs) {
	// Per pair: GPU time with the tunable off minus on.
	double diffs[AUTOTUNE_PAIRS];
	Analysis r;
	double on_sum = 0.0;
	const uint32_t min_n = s.block_frames > AUTOTUNE_SKIP + 2 ? s.block_frames - AUTOTUNE_SKIP - 2 : 1;
	for (uint32_t p = 0; p < p_pairs; p++) {
		const uint32_t a = 2 * p, b = 2 * p + 1;
		if (s.block_n[a] < min_n || s.block_n[b] < min_n) {
			continue;
		}
		const double mean_a = s.block_sum[a] / s.block_n[a];
		const double mean_b = s.block_sum[b] / s.block_n[b];
		const double on = block_on(a) ? mean_a : mean_b;
		const double off = block_on(a) ? mean_b : mean_a;
		diffs[r.pairs++] = off - on;
		on_sum += on;
	}
	if (r.pairs == 0) {
		return r;
	}
	for (uint32_t i = 0; i < r.pairs; i++) {
		r.mean += diffs[i];
	}
	r.mean /= r.pairs;
	if (r.pairs > 1) {
		double var = 0.0;
		for (uint32_t i = 0; i < r.pairs; i++) {
			var += (diffs[i] - r.mean) * (diffs[i] - r.mean);
		}
		r.se = std::sqrt(var / (r.pairs - 1) / r.pairs);
	}
	r.base = on_sum / r.pairs;
	return r;
}

void finish(TuneState &s, bool p_new_choice, const Analysis &p_analysis, bool p_early) {
	print_verbose(vformat("FellowEngine autotune: evaluation from frame %d ends at frame %d%s: %d pairs, on %.3f ms, off - on %.4f ms (se %.4f), choice %s -> %s", (int64_t)s.eval_start, (int64_t)frame_id, p_early ? " (early)" : "", (int)p_analysis.pairs, p_analysis.base, p_analysis.mean, p_analysis.se, s.choice ? "on" : "off", p_new_choice ? "on" : "off"));
	stats().autotune_evals.increment();
	if (p_early) {
		stats().autotune_early.increment();
	}
	const uint32_t base = (uint32_t)MAX(1, GLOBAL_GET_CACHED(int, "rendering/fellow/autotune/interval_frames"));
	if (p_new_choice != s.choice) {
		stats().autotune_flips.increment();
		s.interval = base;
	} else {
		s.interval = MIN(MAX(s.interval, base) * 2, base * AUTOTUNE_INTERVAL_GROWTH);
	}
	s.choice = p_new_choice;
	s.evaluating = false;
	s.next_eval = frame_id + s.interval;
}

void decide(TuneState &s) {
	const Analysis r = analyze(s, AUTOTUNE_PAIRS);
	bool new_choice = s.choice;
	if (r.pairs >= AUTOTUNE_PAIRS - 1) {
		const double min_gain = MAX(0.0, (double)GLOBAL_GET_CACHED(float, "rendering/fellow/autotune/min_gain_percent")) * 0.01 * r.base;
		if (std::fabs(r.mean) > min_gain && (r.se <= 0.0 || std::fabs(r.mean) >= AUTOTUNE_T * r.se)) {
			new_choice = r.mean > 0.0;
		}
	}
	finish(s, new_choice, r, false);
}

bool try_decide_early(TuneState &s) {
	// A clear difference ends the evaluation after AUTOTUNE_EARLY_PAIRS pairs (fewer frames run the slower way when the
	// optimization matters most).
	uint32_t ready = 0;
	while (ready < AUTOTUNE_PAIRS && frame_id > s.eval_start + (2 * ready + 2) * s.block_frames + AUTOTUNE_LATENCY) {
		ready++;
	}
	if (ready < AUTOTUNE_EARLY_PAIRS || ready >= AUTOTUNE_PAIRS) {
		return false;
	}
	const Analysis r = analyze(s, ready);
	if (r.pairs < AUTOTUNE_EARLY_PAIRS || r.base <= 0.0) {
		return false;
	}
	if (std::fabs(r.mean) >= AUTOTUNE_EARLY_GAIN * r.base && r.se > 0.0 && std::fabs(r.mean) >= AUTOTUNE_EARLY_T * r.se) {
		finish(s, r.mean > 0.0, r, true);
		return true;
	}
	return false;
}

} // namespace

bool autotune_allows(Tunable p_tunable) {
	return !off[p_tunable];
}

static void add_sample(int p_tunable, uint64_t p_id, uint8_t p_block, double p_ms) {
	TuneState &s = states[p_tunable];
	if (!s.evaluating || p_id < s.eval_start || p_block >= AUTOTUNE_BLOCKS) {
		return;
	}
	s.block_sum[p_block] += p_ms;
	s.block_n[p_block]++;
}

void autotune_frame_begin() {
	// The previous frame's wall time (render thread, from this call to this call), for the tunables measured by it.
	const uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
	if (frame_id > 0 && last_begin_usec > 0) {
		FrameRecord &prev = ring[frame_id % AUTOTUNE_RING];
		if (prev.id == frame_id && prev.tunable >= 0 && uses_frame_time(prev.tunable)) {
			add_sample(prev.tunable, prev.id, prev.block, double(now_usec - last_begin_usec) / 1000.0);
			prev.tunable = -1;
		}
	}
	last_begin_usec = now_usec;
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
		} else if (s.evaluating) {
			try_decide_early(s);
		}
		if (s.evaluating) {
			evaluating = t;
		}
	}
	if (evaluating < 0) {
		// One tunable at a time: the most overdue.
		int due = -1;
		for (int t = 0; t < TUNE_MAX; t++) {
			if (tunable_applies(Tunable(t)) && frame_id >= states[t].next_eval && (due < 0 || states[t].next_eval < states[due].next_eval)) {
				due = t;
			}
		}
		if (due >= 0) {
			TuneState &s = states[due];
			s.evaluating = true;
			s.eval_start = frame_id;
			s.block_frames = (uint32_t)CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/autotune/block_frames"), AUTOTUNE_SKIP + 4, 120);
			for (uint32_t b = 0; b < AUTOTUNE_BLOCKS; b++) {
				s.block_sum[b] = 0.0;
				s.block_n[b] = 0;
			}
			evaluating = due;
			print_verbose(vformat("FellowEngine autotune: tunable %d evaluated from frame %d, blocks of %d", due, (int64_t)frame_id, (int)s.block_frames));
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
	if (rec.tunable >= 0 && !uses_frame_time(rec.tunable)) {
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
	if (!rec.has_begin || rec.tunable < 0 || uses_frame_time(rec.tunable) || p_gpu_time <= rec.begin) {
		return;
	}
	add_sample(rec.tunable, id, rec.block, double(p_gpu_time - rec.begin) / 1e6);
	rec.tunable = -1;
}

} // namespace Fellow
