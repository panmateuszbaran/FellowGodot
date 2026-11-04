/**************************************************************************/
/*  fellow_motion_service.cpp                                             */
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

#include "fellow_motion_service.h"

#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"

Mutex FellowMotionService::registry_mutex;
HashMap<String, FellowMotionService *> FellowMotionService::registry;

Ref<FellowMotionService> FellowMotionService::acquire(const String &p_path, int p_max_batch, String &r_error) {
	MutexLock lock(registry_mutex);
	if (FellowMotionService **s = registry.getptr(p_path)) {
		return Ref<FellowMotionService>(*s);
	}
	Ref<FellowMotionService> s;
	s.instantiate();
	s->key = p_path;
	s->model.instantiate();
	Error err = s->model->load(p_path, p_max_batch);
	if (err != OK) {
		r_error = "cannot load the model " + p_path + ".";
		return Ref<FellowMotionService>();
	}
	s->rig_ok = s->rig.setup(s->model->get_skeleton(), s->model->get_capture_offsets(), s->model->get_standard_offsets(), s->rig_error);
	if (!s->rig_ok) {
		r_error = s->rig_error;
		return Ref<FellowMotionService>();
	}
	s->thread.start(_thread_func, s.ptr());
	registry[p_path] = s.ptr();
	return s;
}

void FellowMotionService::_thread_func(void *p_self) {
	FellowMotionService *self = (FellowMotionService *)p_self;
	while (true) {
		self->semaphore.wait();
		if (self->exit.is_set()) {
			break;
		}
		LocalVector<Pending> work;
		{
			MutexLock lock(self->mutex);
			int n = MIN(int(self->pending.size()), self->model->get_max_batch());
			for (int i = 0; i < n; i++) {
				work.push_back(std::move(self->pending[i]));
			}
			LocalVector<Pending> rest;
			for (uint32_t i = n; i < self->pending.size(); i++) {
				rest.push_back(std::move(self->pending[i]));
			}
			self->pending = std::move(rest);
			if (!self->pending.is_empty()) {
				self->semaphore.post();
			}
		}
		if (!work.is_empty()) {
			self->_run(work);
		}
	}
}

void FellowMotionService::_run(LocalVector<Pending> &p_work) {
	const int n = p_work.size();
	// The requests given as poses encoded, spread over the worker threads.
	if (n > 1) {
		WorkerThreadPool::GroupID g = WorkerThreadPool::get_singleton()->add_template_group_task(this, &FellowMotionService::_encode_one, &p_work, n, -1, true, "FellowMotion encode");
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(g);
	} else {
		_encode_one(0, &p_work);
	}
	FellowMotionModel::Batch b;
	b.resize(n);
	for (int i = 0; i < n; i++) {
		const Request &r = p_work[i].req;
		memcpy(b.global_root_values.ptr() + i * 40, r.grv, sizeof(r.grv));
		memcpy(b.has_global_root_values.ptr() + i * 8, r.hg, sizeof(r.hg));
		memcpy(b.local_root_values.ptr() + i * 32, r.lrv, sizeof(r.lrv));
		memcpy(b.has_local_root_values.ptr() + i * 8, r.hl, sizeof(r.hl));
		memcpy(b.local_poses.ptr() + i * 8 * 267, r.lp, sizeof(r.lp));
		memcpy(b.has_local_poses.ptr() + i * 8, r.hp, sizeof(r.hp));
		memcpy(b.allowed_pred_num_tokens.ptr() + i * 11, r.allowed, sizeof(r.allowed));
	}
	uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	Error err = model->run_batch(b);
	uint64_t t1 = OS::get_singleton()->get_ticks_usec();
	double ms = (t1 - t0) / 1000.0;
	LocalVector<Result> out;
	out.resize(n);
	for (int i = 0; i < n; i++) {
		out[i].batch = n;
		out[i].gpu_msec = ms;
		out[i].tokens = err == OK ? b.pred_num_tokens[i] : 0;
	}
	if (err == OK) {
		// Each request's poses (or raw prediction), the requests spread over the worker threads.
		DecodeWork dw;
		dw.work = &p_work;
		dw.results = &out;
		dw.batch = &b;
		if (n > 1) {
			WorkerThreadPool::GroupID g = WorkerThreadPool::get_singleton()->add_template_group_task(this, &FellowMotionService::_decode_one, &dw, n, -1, true, "FellowMotion decode");
			WorkerThreadPool::get_singleton()->wait_for_group_task_completion(g);
		} else {
			_decode_one(0, &dw);
		}
	}
	double dms = (OS::get_singleton()->get_ticks_usec() - t1) / 1000.0;
	MutexLock lock(mutex);
	stat_batches++;
	stat_requests += n;
	stat_msec += ms;
	stat_decode_msec += dms;
	stat_last_msec = ms;
	stat_last_batch = n;
	stat_max_batch_seen = MAX(stat_max_batch_seen, n);
	for (int i = 0; i < n; i++) {
		out[i].decode_msec = dms;
		results[p_work[i].id] = std::move(out[i]);
	}
}

void FellowMotionService::_encode_one(uint32_t p_index, LocalVector<Pending> *p_work) {
	Request &r = (*p_work)[p_index].req;
	const uint32_t J = rig.joints;
	if (r.context.size() != 4 * J || r.target.size() != 4 * J) {
		return;
	}
	rig.encode(r.context.ptr(), r.target.ptr(), 4, r.target_has_pose, r.allowed_bits, r.grv, r.hg, r.lrv, r.hl, r.lp, r.hp, r.allowed, r.align);
}

void FellowMotionService::_decode_one(uint32_t p_index, DecodeWork *p_work) {
	const Request &r = (*p_work->work)[p_index].req;
	Result &res = (*p_work->results)[p_index];
	const float *m = p_work->batch->pred_global_motions.ptr() + p_index * 64 * 366;
	if (res.tokens <= 0) {
		return;
	}
	const uint32_t J = rig.joints;
	if (r.context.size() != 4 * J || r.target.size() != 4 * J) {
		res.motion.resize(64 * 366);
		memcpy(res.motion.ptr(), m, 64 * 366 * sizeof(float));
		return;
	}
	res.count = rig.decode(m, res.tokens, r.align, res.frames, res.contacts);
	if (res.count >= 2) {
		// From the last context pose to the first target pose.
		rig.correct_endpoints(res.frames, res.count, r.context.ptr() + 3 * J, r.target.ptr(), r.correct_end, r.correct_end);
	}
}

uint64_t FellowMotionService::submit(Request &&p_request, bool p_wake) {
	MutexLock lock(mutex);
	uint64_t id = next_id++;
	pending.push_back({ id, std::move(p_request) });
	if (p_wake) {
		semaphore.post();
	}
	return id;
}

void FellowMotionService::flush() {
	while (true) {
		LocalVector<Pending> work;
		{
			MutexLock lock(mutex);
			int n = MIN(int(pending.size()), model->get_max_batch());
			if (n == 0) {
				return;
			}
			for (int i = 0; i < n; i++) {
				work.push_back(std::move(pending[i]));
			}
			LocalVector<Pending> rest;
			for (uint32_t i = n; i < pending.size(); i++) {
				rest.push_back(std::move(pending[i]));
			}
			pending = std::move(rest);
		}
		_run(work);
	}
}

bool FellowMotionService::fetch(uint64_t p_id, Result &r_result) {
	MutexLock lock(mutex);
	Result *r = results.getptr(p_id);
	if (!r) {
		return false;
	}
	r_result = std::move(*r);
	results.erase(p_id);
	return true;
}

void FellowMotionService::cancel(uint64_t p_id) {
	MutexLock lock(mutex);
	results.erase(p_id);
	for (uint32_t i = 0; i < pending.size(); i++) {
		if (pending[i].id == p_id) {
			pending.remove_at(i);
			break;
		}
	}
}

bool FellowMotionService::run_now(Request &&p_request, Result &r_result) {
	LocalVector<Pending> one;
	one.push_back({ 0, std::move(p_request) });
	_run(one);
	return fetch(0, r_result) && r_result.tokens > 0;
}

Dictionary FellowMotionService::get_stats() const {
	MutexLock lock(const_cast<Mutex &>(mutex));
	Dictionary d;
	d["requests"] = int64_t(stat_requests);
	d["batches"] = int64_t(stat_batches);
	d["avg_batch"] = stat_batches ? double(stat_requests) / double(stat_batches) : 0.0;
	d["avg_batch_msec"] = stat_batches ? stat_msec / double(stat_batches) : 0.0;
	d["avg_decode_msec"] = stat_batches ? stat_decode_msec / double(stat_batches) : 0.0;
	d["last_batch"] = stat_last_batch;
	d["last_batch_msec"] = stat_last_msec;
	d["max_batch_seen"] = stat_max_batch_seen;
	d["pending"] = int(pending.size());
	d["model"] = model.is_valid() ? model->get_info() : Dictionary();
	return d;
}

Dictionary FellowMotionService::get_stats_for(const String &p_path) {
	MutexLock lock(registry_mutex);
	if (FellowMotionService **s = registry.getptr(p_path)) {
		return (*s)->get_stats();
	}
	return Dictionary();
}

void FellowMotionService::_bind_methods() {
}

FellowMotionService::~FellowMotionService() {
	{
		MutexLock lock(registry_mutex);
		if (registry.has(key) && registry[key] == this) {
			registry.erase(key);
		}
	}
	if (thread.is_started()) {
		exit.set();
		semaphore.post();
		thread.wait_to_finish();
	}
}
