/**************************************************************************/
/*  fellow_motion_service.h                                               */
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

#include "fellow_motion_model.h"
#include "fellow_motion_rig.h"

#include "core/os/semaphore.h"
#include "core/os/thread.h"

// One model shared by every character that names its file: the requests of a frame go to a worker thread, which runs
// them as one batch on the model's device (each character's motion is an independent request of the batch) and hands
// the predictions back. A character polls for its result on the following frames.
class FellowMotionService : public RefCounted {
	GDCLASS(FellowMotionService, RefCounted);

public:
	using Xf = FellowMotionRig::Xf;
	struct Request {
		float grv[8 * 5];
		int32_t hg[8];
		float lrv[8 * 4];
		int32_t hl[8];
		float lp[8 * 267];
		int32_t hp[8];
		int32_t allowed[11];
		// Or the poses, which the service encodes into the arrays above, and after the batch decodes into poses and
		// endpoint-corrects (in parallel, on the worker thread when asynchronous): four context poses and four
		// target poses (local transforms, [4][joints] each), whether the targets are whole poses, the allowed token
		// counts (bit c - 6 for c tokens), and whether the end is corrected too (to the whole first target pose).
		LocalVector<Xf> context;
		LocalVector<Xf> target;
		bool target_has_pose = false;
		uint32_t allowed_bits = 0;
		bool correct_end = false;
		Xf align; // set by the encoding
	};
	struct Result {
		LocalVector<float> motion; // [64, 366], when the request asked for no poses
		LocalVector<Xf> frames; // [count][joints], when it did
		LocalVector<float> contacts; // [count][4]
		int count = 0;
		int tokens = 0;
		double gpu_msec = 0.0; // the batch's
		double decode_msec = 0.0; // the batch's decoding (parallel)
		int batch = 0;
	};

private:
	static Mutex registry_mutex;
	static HashMap<String, FellowMotionService *> registry;

	String key;
	Ref<FellowMotionModel> model;
	FellowMotionRig rig;
	bool rig_ok = false;
	String rig_error;

	Mutex mutex;
	Semaphore semaphore;
	Thread thread;
	SafeFlag exit;
	struct Pending {
		uint64_t id;
		Request req;
	};
	LocalVector<Pending> pending;
	HashMap<uint64_t, Result> results;
	uint64_t next_id = 1;

	// Statistics.
	uint64_t stat_requests = 0;
	uint64_t stat_batches = 0;
	double stat_msec = 0.0;
	double stat_last_msec = 0.0;
	int stat_last_batch = 0;
	int stat_max_batch_seen = 0;

	static void _thread_func(void *p_self);
	void _run(LocalVector<Pending> &p_work);
	struct DecodeWork {
		LocalVector<Pending> *work = nullptr;
		LocalVector<Result> *results = nullptr;
		const FellowMotionModel::Batch *batch = nullptr;
	};
	void _encode_one(uint32_t p_index, LocalVector<Pending> *p_work);
	void _decode_one(uint32_t p_index, DecodeWork *p_work);
	double stat_decode_msec = 0.0;

protected:
	static void _bind_methods();

public:
	static Ref<FellowMotionService> acquire(const String &p_path, int p_max_batch, String &r_error);

	const FellowMotionRig &get_rig() const { return rig; }
	bool is_ready() const { return model.is_valid() && model->is_loaded() && rig_ok; }
	Ref<FellowMotionModel> get_model() const { return model; }

	uint64_t submit(Request &&p_request, bool p_wake = true);
	// Runs every request waiting (submitted with p_wake false, or not yet taken by the worker) on the calling thread.
	void flush();
	bool fetch(uint64_t p_id, Result &r_result);
	void cancel(uint64_t p_id);
	// Runs one request on the calling thread and waits (deterministic, for tests and single characters).
	bool run_now(Request &&p_request, Result &r_result);

	Dictionary get_stats() const;
	static Dictionary get_stats_for(const String &p_path);

	~FellowMotionService();
};
