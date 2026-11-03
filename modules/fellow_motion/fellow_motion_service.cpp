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
				work.push_back(self->pending[i]);
			}
			LocalVector<Pending> rest;
			for (uint32_t i = n; i < self->pending.size(); i++) {
				rest.push_back(self->pending[i]);
			}
			self->pending = rest;
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
	double ms = (OS::get_singleton()->get_ticks_usec() - t0) / 1000.0;
	MutexLock lock(mutex);
	stat_batches++;
	stat_requests += n;
	stat_msec += ms;
	stat_last_msec = ms;
	stat_last_batch = n;
	stat_max_batch_seen = MAX(stat_max_batch_seen, n);
	for (int i = 0; i < n; i++) {
		Result res;
		res.batch = n;
		res.gpu_msec = ms;
		if (err == OK) {
			res.tokens = b.pred_num_tokens[i];
			res.motion.resize(64 * 366);
			memcpy(res.motion.ptr(), b.pred_global_motions.ptr() + i * 64 * 366, 64 * 366 * sizeof(float));
		}
		results[p_work[i].id] = res;
	}
}

uint64_t FellowMotionService::submit(const Request &p_request, bool p_wake) {
	MutexLock lock(mutex);
	uint64_t id = next_id++;
	pending.push_back({ id, p_request });
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
				work.push_back(pending[i]);
			}
			LocalVector<Pending> rest;
			for (uint32_t i = n; i < pending.size(); i++) {
				rest.push_back(pending[i]);
			}
			pending = rest;
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
	r_result = *r;
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

bool FellowMotionService::run_now(const Request &p_request, Result &r_result) {
	LocalVector<Pending> one;
	one.push_back({ 0, p_request });
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
