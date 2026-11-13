/**************************************************************************/
/*  register_types.h                                                      */
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

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/os/thread.h"
#include "core/variant/dictionary.h"

#include <atomic>

// Milestone 97 (decision 0205): crash reports for shipped games. With debug/fellow/crash_reports/enabled, a fatal
// signal or exception (a release template included, where Godot's own handler is compiled out) writes a JSON report
// into the report directory before the process ends: the reason, the faulting thread's frames (module and offset, to
// be symbolized against the build's symbols), the last lines of the log, the game's breadcrumbs and values, the
// machine, the GPU and the scene; on Windows a minidump beside it. The next run finds it (get_pending_report()).
class FellowCrashReporter : public Object {
	GDCLASS(FellowCrashReporter, Object);

	static FellowCrashReporter *singleton;

	bool enabled = false;
	String directory; // absolute
	int max_reports = 10;
	uint64_t setup_usec = 0;
	PackedStringArray written; // this session's reports (not pending)

	// Milestone 102 (decision 0210): the reports sent to a collector, on a thread of their own.
	Thread *upload_thread = nullptr;
	std::atomic<bool> uploading{ false };
	std::atomic<bool> upload_cancel{ false };
	PackedStringArray upload_queue;
	String upload_target;
	bool upload_minidumps = true;
	bool upload_automatically = false;
	bool auto_upload_done = false;
	static void _upload_thread(void *p_self);
	void _upload_done(int p_sent, int p_failed);
	void _emit_uploaded(const String &p_path, bool p_ok, int p_code);

	void _prune_reports(const String &p_dir);

protected:
	static void _bind_methods();

public:
	static FellowCrashReporter *get_singleton() { return singleton; }
	static void create();
	static void destroy();

	bool is_enabled() const { return enabled; }
	String get_report_directory() const { return directory; }

	void add_breadcrumb(const String &p_message, const String &p_category = String());
	void set_custom_value(const String &p_key, const Variant &p_value);
	void remove_custom_value(const String &p_key);
	Dictionary get_custom_values() const;

	String write_report(const String &p_reason);
	PackedStringArray get_reports() const;
	Dictionary get_pending_report() const;
	void acknowledge_reports();
	static Dictionary read_report(const String &p_path);

	Error upload_reports(const String &p_url = String());
	bool is_uploading() const { return uploading.load(); }
	void auto_upload(); // the first frame's, with upload_automatically

	void debug_crash(const String &p_kind);

	Dictionary get_stats() const;

	FellowCrashReporter();
	~FellowCrashReporter();
};
