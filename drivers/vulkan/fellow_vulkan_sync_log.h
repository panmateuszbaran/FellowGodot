/**************************************************************************/
/*  fellow_vulkan_sync_log.h                                              */
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

#include "core/os/mutex.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

#include <drivers/vulkan/godot_vulkan.h>

// FellowEngine (milestone 146, decision 0270): the Vulkan driver's queue operations recorded in the order they are made
// (rendering/fellow/debug/sync_log, off by default): every submission, presentation and swap chain acquisition with the
// binary semaphores it waits on and signals and the fence it signals. Each wait is matched with the signal it consumes
// as it is submitted, and each fence wait with the submission that signals the fence, so that what leaves a queue
// waiting for ever is reported when it happens — a wait on a semaphore with no signal pending, a semaphore signalled
// twice without a wait between, a fence waited for that no submission signals, a fence submitted twice — and a late
// fence can be traced to the submission that signals it, what that submission waits on and where each signal came
// from. The operations are recorded under one lock taken around vkQueueSubmit() (all queues), so their order is the
// order the driver saw them in.
class FellowVulkanSyncLog {
public:
	enum Kind : uint8_t {
		KIND_SUBMIT,
		KIND_PRESENT,
		KIND_ACQUIRE,
	};

	static constexpr uint32_t MAX_SEMAPHORES = 8;

	struct Op {
		uint64_t serial = 0; // 0: an empty slot
		uint64_t usec = 0;
		uint64_t thread = 0;
		Kind kind = KIND_SUBMIT;
		const char *queue = "";
		uint32_t command_buffers = 0;
		uint32_t wait_count = 0; // the first MAX_SEMAPHORES are kept
		uint32_t signal_count = 0;
		uint32_t unsignalled_waits = 0; // bit i: wait i had no signal pending when it was submitted
		VkSemaphore waits[MAX_SEMAPHORES] = {};
		uint64_t waits_signalled_by[MAX_SEMAPHORES] = {}; // the serial of the signal each wait consumed
		VkSemaphore signals[MAX_SEMAPHORES] = {};
		VkFence fence = VK_NULL_HANDLE;
	};

private:
	struct SemaphoreState {
		uint64_t pending_signal = 0; // the serial of the signal not waited on yet (0: none)
		uint64_t last_signal = 0;
		uint64_t last_wait = 0;
	};

	static constexpr uint32_t OPS = 256;
	static constexpr uint32_t MAX_PRINTED = 32;

	bool enabled = false;
	BinaryMutex mutex;
	uint64_t serial = 0;
	Op ops[OPS];
	HashMap<VkSemaphore, SemaphoreState> semaphores;
	HashMap<VkFence, uint64_t> fences; // the serial of the submission that signals it, until it is waited for
	uint32_t anomalies = 0;

	const Op *_op(uint64_t p_serial) const;
	String _describe(const Op &p_op, VkDevice p_device) const;
	void _anomaly(const String &p_what);

public:
	bool is_enabled() const { return enabled; }
	void set_enabled(bool p_enabled) { enabled = p_enabled; }
	uint32_t get_anomalies() const { return anomalies; }

	// Taken around the queue operation and its record(), so that the record's order is the queue operations' order.
	BinaryMutex &get_mutex() { return mutex; }
	// With the mutex held.
	void record(Kind p_kind, const char *p_queue, const VkSemaphore *p_waits, uint32_t p_wait_count, const VkSemaphore *p_signals, uint32_t p_signal_count, VkFence p_fence, uint32_t p_command_buffers);

	// These take the mutex themselves.
	void forget_semaphore(VkSemaphore p_semaphore); // destroyed (its handle may come back as another)
	void forget_fence(VkFence p_fence);
	void fence_waiting(VkFence p_fence, bool p_signalled); // a wait for the fence begins (p_signalled: it already is)
	void fence_waited(VkFence p_fence); // the fence was waited for and reset

	// A late fence: the submission that signals it, its waits and their signals, the operations recorded last. One line
	// each.
	String report(VkFence p_fence, VkDevice p_device);
};
