/**************************************************************************/
/*  fellow_vulkan_sync_log.cpp                                            */
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

#include "fellow_vulkan_sync_log.h"

#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/string/print_string.h"
#include "servers/rendering/fellow_settings.h"

#include <type_traits>

template <typename T>
static uint64_t _fellow_handle(T p_handle) {
	if constexpr (std::is_pointer_v<T>) {
		return uint64_t(uintptr_t(p_handle));
	} else {
		return uint64_t(p_handle);
	}
}

static const char *_fellow_kind_name(FellowVulkanSyncLog::Kind p_kind) {
	switch (p_kind) {
		case FellowVulkanSyncLog::KIND_SUBMIT:
			return "submission";
		case FellowVulkanSyncLog::KIND_PRESENT:
			return "presentation";
		case FellowVulkanSyncLog::KIND_ACQUIRE:
			return "acquisition";
	}
	return "?";
}

static const char *_fellow_fence_status(VkDevice p_device, VkFence p_fence) {
	if (p_fence == VK_NULL_HANDLE) {
		return "";
	}
	switch (vkGetFenceStatus(p_device, p_fence)) {
		case VK_SUCCESS:
			return "signalled";
		case VK_NOT_READY:
			return "NOT signalled";
		case VK_ERROR_DEVICE_LOST:
			return "device lost";
		default:
			return "error";
	}
}

const FellowVulkanSyncLog::Op *FellowVulkanSyncLog::_op(uint64_t p_serial) const {
	if (p_serial == 0 || p_serial > serial || p_serial + OPS <= serial) {
		return nullptr; // none, or written over
	}
	const Op &op = ops[p_serial % OPS];
	return op.serial == p_serial ? &op : nullptr;
}

String FellowVulkanSyncLog::_describe(const Op &p_op, VkDevice p_device) const {
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	String s = vformat("#%d %s on %s, %.1f ms ago, thread %d", p_op.serial, _fellow_kind_name(p_op.kind), p_op.queue, double(now - p_op.usec) / 1000.0, p_op.thread);
	if (p_op.kind == KIND_SUBMIT) {
		s += vformat(", %d command buffers", p_op.command_buffers);
	}
	if (p_op.image >= 0) {
		s += vformat(", image %d", p_op.image);
	}
	if (p_op.acquire_fence != VK_NULL_HANDLE) {
		// Whether the presentation engine has released the image (its fence signalled with the semaphore); a fence
		// reused by a later acquisition was.
		const uint64_t *a = acquire_fences.getptr(p_op.acquire_fence);
		s += vformat(" (released: %s)", (a != nullptr && *a == p_op.serial) ? _fellow_fence_status(p_device, p_op.acquire_fence) : "signalled");
	}
	if (p_op.has_result && p_op.result != VK_SUCCESS) {
		s += vformat(", returned %d", p_op.result);
	}
	if (p_op.wait_count > 0) {
		s += ", waits";
		for (uint32_t i = 0; i < MIN(p_op.wait_count, MAX_SEMAPHORES); i++) {
			if (p_op.unsignalled_waits & (1u << i)) {
				s += vformat(" %x (NO SIGNAL)", _fellow_handle(p_op.waits[i]));
			} else {
				s += vformat(" %x<#%d", _fellow_handle(p_op.waits[i]), p_op.waits_signalled_by[i]);
			}
		}
	}
	if (p_op.signal_count > 0) {
		s += ", signals";
		for (uint32_t i = 0; i < MIN(p_op.signal_count, MAX_SEMAPHORES); i++) {
			s += vformat(" %x", _fellow_handle(p_op.signals[i]));
		}
	}
	if (p_op.fence != VK_NULL_HANDLE) {
		// The fence's state only for its latest submission not yet waited for (it is reset and submitted again).
		const uint64_t *f = fences.getptr(p_op.fence);
		s += vformat(", fence %x (%s)", _fellow_handle(p_op.fence), (f != nullptr && *f == p_op.serial) ? _fellow_fence_status(p_device, p_op.fence) : "waited for since");
	}
	return s;
}

void FellowVulkanSyncLog::_anomaly(const String &p_what) {
	anomalies++;
	Fellow::stats().sync_log_anomalies.increment();
	if (anomalies <= MAX_PRINTED) {
		print_error("FellowEngine sync log: " + p_what);
		if (anomalies == MAX_PRINTED) {
			print_error("FellowEngine sync log: no more such messages are printed (rd_stats: sync_log_anomalies).");
		}
	}
}

FellowVulkanSyncLog::Op &FellowVulkanSyncLog::record(Kind p_kind, const char *p_queue, const VkSemaphore *p_waits, uint32_t p_wait_count, const VkSemaphore *p_signals, uint32_t p_signal_count, VkFence p_fence, uint32_t p_command_buffers) {
	serial++;
	Op &op = ops[serial % OPS];
	op = Op();
	op.serial = serial;
	op.usec = OS::get_singleton()->get_ticks_usec();
	op.thread = Thread::get_caller_id();
	op.kind = p_kind;
	op.queue = p_queue;
	op.command_buffers = p_command_buffers;
	op.wait_count = p_wait_count;
	op.signal_count = p_signal_count;
	op.fence = p_fence;
	const String what = vformat("#%d (%s on %s, thread %d)", serial, _fellow_kind_name(p_kind), p_queue, op.thread);

	for (uint32_t i = 0; i < p_wait_count; i++) {
		SemaphoreState &s = semaphores[p_waits[i]];
		const uint64_t from = s.pending_signal;
		if (from == 0) {
			if (i < 32) {
				op.unsignalled_waits |= 1u << i;
			}
			_anomaly(vformat("%s waits on semaphore %x with no signal pending (last signalled by #%d, last waited on by #%d): the wait may never end.", what, _fellow_handle(p_waits[i]), s.last_signal, s.last_wait));
		}
		s.pending_signal = 0;
		s.last_wait = serial;
		if (i < MAX_SEMAPHORES) {
			op.waits[i] = p_waits[i];
			op.waits_signalled_by[i] = from;
		}
	}
	for (uint32_t i = 0; i < p_signal_count; i++) {
		SemaphoreState &s = semaphores[p_signals[i]];
		if (s.pending_signal != 0) {
			_anomaly(vformat("%s signals semaphore %x, already signalled by #%d and not waited on since.", what, _fellow_handle(p_signals[i]), s.pending_signal));
		}
		s.pending_signal = serial;
		s.last_signal = serial;
		if (i < MAX_SEMAPHORES) {
			op.signals[i] = p_signals[i];
		}
	}
	if (p_fence != VK_NULL_HANDLE) {
		uint64_t &f = fences[p_fence];
		if (f != 0) {
			_anomaly(vformat("%s signals fence %x, already signalled by #%d and not waited for since.", what, _fellow_handle(p_fence), f));
		}
		f = serial;
	}
	return op;
}

void FellowVulkanSyncLog::set_acquire(Op &r_op, int32_t p_image, VkFence p_fence) {
	r_op.image = p_image;
	r_op.acquire_fence = p_fence;
	if (p_fence != VK_NULL_HANDLE) {
		acquire_fences[p_fence] = r_op.serial;
	}
}

void FellowVulkanSyncLog::forget_acquire_fence(VkFence p_fence) {
	MutexLock lock(mutex);
	acquire_fences.erase(p_fence);
	for (Op &op : ops) {
		if (op.acquire_fence == p_fence) {
			op.acquire_fence = VK_NULL_HANDLE;
		}
	}
}

void FellowVulkanSyncLog::set_result(uint64_t p_serial, int32_t p_result) {
	MutexLock lock(mutex);
	Op &op = ops[p_serial % OPS];
	if (op.serial == p_serial) {
		op.has_result = true;
		op.result = p_result;
	}
}

void FellowVulkanSyncLog::forget_semaphore(VkSemaphore p_semaphore) {
	MutexLock lock(mutex);
	semaphores.erase(p_semaphore);
}

void FellowVulkanSyncLog::forget_fence(VkFence p_fence) {
	MutexLock lock(mutex);
	fences.erase(p_fence);
	for (Op &op : ops) {
		if (op.fence == p_fence) {
			op.fence = VK_NULL_HANDLE; // freed: its status is not asked for
		}
	}
}

void FellowVulkanSyncLog::fence_waiting(VkFence p_fence, bool p_signalled) {
	if (p_signalled) {
		return;
	}
	MutexLock lock(mutex);
	const uint64_t *f = fences.getptr(p_fence);
	if (f == nullptr || *f == 0) {
		_anomaly(vformat("a wait (thread %d) for fence %x, which no submission has signalled since it was last waited for: the wait may never end.", Thread::get_caller_id(), _fellow_handle(p_fence)));
	}
}

void FellowVulkanSyncLog::fence_waited(VkFence p_fence) {
	MutexLock lock(mutex);
	fences.erase(p_fence);
}

String FellowVulkanSyncLog::report(VkFence p_fence, VkDevice p_device) {
	MutexLock lock(mutex);
	String r = vformat("fence %x: %s.", _fellow_handle(p_fence), _fellow_fence_status(p_device, p_fence));
	if (!enabled) {
		return r + "\n(rendering/fellow/debug/sync_log off: no record of the queue operations.)";
	}
	const uint64_t *f = fences.getptr(p_fence);
	if (f == nullptr || *f == 0) {
		r += "\nNo submission has signalled it since it was last waited for.";
	} else {
		const Op *op = _op(*f);
		if (op == nullptr) {
			r += vformat("\nSignalled by #%d, out of the record (%d operations since).", *f, serial - *f);
		} else {
			r += "\nSignalled by " + _describe(*op, p_device) + ".";
			for (uint32_t i = 0; i < MIN(op->wait_count, MAX_SEMAPHORES); i++) {
				if (op->unsignalled_waits & (1u << i)) {
					r += vformat("\n  wait %d, semaphore %x: NO SIGNAL was pending when it was submitted.", i, _fellow_handle(op->waits[i]));
					continue;
				}
				const Op *from = _op(op->waits_signalled_by[i]);
				r += vformat("\n  wait %d, semaphore %x: signalled by ", i, _fellow_handle(op->waits[i]));
				r += from ? _describe(*from, p_device) + "." : vformat("#%d (out of the record).", op->waits_signalled_by[i]);
			}
		}
	}
	r += vformat("\nAnomalies so far: %d. The last operations (of %d):", anomalies, serial);
	const uint64_t first = serial > 32 ? serial - 31 : 1;
	for (uint64_t i = first; i <= serial; i++) {
		const Op *op = _op(i);
		if (op != nullptr) {
			r += "\n  " + _describe(*op, p_device);
		}
	}
	return r;
}
