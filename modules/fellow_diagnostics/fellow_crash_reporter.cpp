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

// The fatal paths below run in a signal handler or an unhandled-exception filter: the report's core is written with
// the state kept here in fixed buffers (no allocation, no lock, plain file writes), the frames described by the
// loader (dladdr, GetModuleHandleEx); what needs the heap (the scripts' backtraces) is appended afterwards, so a
// heap too broken for it still leaves a complete report.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE // REG_RIP in <ucontext.h>
#endif

#include "fellow_crash_reporter.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/crypto/crypto.h"
#include "core/debugger/engine_debugger.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/io/logger.h"
#include "core/object/callable_mp.h"
#include "core/object/script_backtrace.h"
#include "core/object/script_language.h"
#include "core/os/memory.h"
#include "core/os/os.h"
#include "core/os/spin_lock.h"
#include "core/os/thread.h"
#include "core/version.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if (defined(__linux__) && defined(__GLIBC__)) || defined(__APPLE__)
#define FELLOW_CRASH_POSIX 1
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
#elif defined(WINDOWS_ENABLED)
#define FELLOW_CRASH_WINDOWS 1
#include <windows.h>
// After windows.h.
#include <dbghelp.h>
#include <signal.h>
#else
#include <unistd.h>
#endif

#if !defined(FELLOW_CRASH_WINDOWS)
#include <fcntl.h>
#include <unistd.h>
#endif

FellowCrashReporter *FellowCrashReporter::singleton = nullptr;

namespace {

constexpr int LOG_LINE = 320;
constexpr int LOG_MAX = 1024;
constexpr int CRUMB_MAX = 64;
constexpr int CRUMB_TEXT = 200;
constexpr int CUSTOM_MAX = 32;
constexpr int CUSTOM_KEY = 48;
constexpr int CUSTOM_VALUE = 208;
constexpr int CONTEXT_MAX = 8192;
constexpr int PATH_BYTES = 1024;
constexpr int FRAMES_MAX = 96;

struct Crumb {
	uint64_t msec;
	char category[24];
	char text[CRUMB_TEXT];
};

struct Custom {
	char key[CUSTOM_KEY];
	char value[CUSTOM_VALUE];
};

// Everything a fatal handler reads. In static storage: it outlives the reporter, the loggers and the servers, so a
// crash during the engine's shutdown is still reported.
struct State {
	std::atomic<bool> installed{ false };
	std::atomic<bool> scripts_ok{ false }; // the script server lives: its backtraces can be captured
	char dir[PATH_BYTES] = {}; // UTF-8, ending with a separator
	std::atomic<int> context_index{ 0 };
	char context[2][CONTEXT_MAX] = {};
	char scene[512] = {};
	std::atomic<uint64_t> frame{ 0 };
	std::atomic<uint64_t> video_bytes{ 0 };
	uint64_t start_msec = 0;
	uint64_t page_size = 4096;
	std::atomic<uint32_t> log_capacity{ 0 };
	std::atomic<uint32_t> log_head{ 0 };
	char log[LOG_MAX][LOG_LINE] = {};
	std::atomic<uint32_t> crumb_head{ 0 };
	Crumb crumbs[CRUMB_MAX] = {};
	SpinLock custom_lock;
	Custom custom[CUSTOM_MAX] = {};
	std::atomic<int> reporting{ 0 }; // 0 none, 1 a fatal report being written, 2 written
	std::atomic<uint64_t> reporting_thread{ 0 };
	std::atomic<uint32_t> sequence{ 0 };
	std::atomic<uint32_t> reports_written{ 0 };
	char last_report[PATH_BYTES] = {}; // the fatal report's path, for what is appended to it
	bool minidumps = true;
	bool idle_added = false;
	bool logger_added = false;
	bool context_has_gpu = false;
	ObjectID last_scene;
};

State g;

uint64_t _now_msec() {
#ifdef FELLOW_CRASH_WINDOWS
	return GetTickCount64();
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
#endif
}

struct UtcTime {
	int year, month, day, hour, minute, second;
};

UtcTime _utc_now() {
	UtcTime t;
#ifdef FELLOW_CRASH_WINDOWS
	SYSTEMTIME st;
	GetSystemTime(&st);
	t = { st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond };
#else
	const time_t now = time(nullptr);
	struct tm tm;
	gmtime_r(&now, &tm);
	t = { tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec };
#endif
	return t;
}

uint64_t _process_id() {
#ifdef FELLOW_CRASH_WINDOWS
	return GetCurrentProcessId();
#else
	return uint64_t(getpid());
#endif
}

// Copies at most p_len bytes of p_src into p_dst (p_cap bytes, the last one never written: always terminated):
// without terminal escape sequences or a trailing newline, cut at a UTF-8 character's boundary.
void _copy_text(char *p_dst, size_t p_cap, const char *p_src, size_t p_len) {
	size_t o = 0;
	for (size_t i = 0; i < p_len && p_src[i]; i++) {
		const unsigned char c = (unsigned char)p_src[i];
		if (c == 0x1b) { // ESC [ parameters final-byte
			i++;
			if (i < p_len && p_src[i] == '[') {
				i++;
				while (i < p_len && p_src[i] && !(p_src[i] >= 0x40 && p_src[i] <= 0x7e)) {
					i++;
				}
			}
			continue;
		}
		if (o + 2 >= p_cap) {
			break;
		}
		p_dst[o++] = char(c);
	}
	if (o > 0 && ((unsigned char)p_dst[o - 1] & 0x80)) {
		size_t k = o;
		while (k > 0 && ((unsigned char)p_dst[k - 1] & 0xc0) == 0x80) {
			k--;
		}
		if (k > 0) {
			const unsigned char lead = (unsigned char)p_dst[k - 1];
			const size_t need = lead >= 0xf0 ? 4 : (lead >= 0xe0 ? 3 : (lead >= 0xc0 ? 2 : 1));
			if (o - (k - 1) < need) {
				o = k - 1;
			}
		}
	}
	while (o > 0 && (p_dst[o - 1] == '\n' || p_dst[o - 1] == '\r')) {
		o--;
	}
	p_dst[o] = 0;
}

void _copy_string(char *p_dst, size_t p_cap, const String &p_text) {
	const CharString cs = p_text.utf8();
	_copy_text(p_dst, p_cap, cs.get_data(), size_t(cs.length()));
}

size_t _bounded_length(const char *p_text, size_t p_cap) {
	size_t n = 0;
	while (n < p_cap && p_text[n]) {
		n++;
	}
	return n;
}

// A file written without the heap: a buffer on the stack, the system's own calls.
struct ReportFile {
#ifdef FELLOW_CRASH_WINDOWS
	HANDLE h = INVALID_HANDLE_VALUE;
#else
	int fd = -1;
#endif
	char buf[4096];
	size_t n = 0;
	uint64_t total = 0;

	bool open(const char *p_path, bool p_existing) {
#ifdef FELLOW_CRASH_WINDOWS
		wchar_t w[PATH_BYTES];
		if (!MultiByteToWideChar(CP_UTF8, 0, p_path, -1, w, PATH_BYTES)) {
			return false;
		}
		h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ, nullptr, p_existing ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		return h != INVALID_HANDLE_VALUE;
#else
		fd = ::open(p_path, p_existing ? O_WRONLY : (O_WRONLY | O_CREAT | O_TRUNC), 0644);
		return fd >= 0;
#endif
	}
	void flush() {
		size_t done = 0;
		while (done < n) {
#ifdef FELLOW_CRASH_WINDOWS
			DWORD w = 0;
			if (!WriteFile(h, buf + done, DWORD(n - done), &w, nullptr) || w == 0) {
				break;
			}
#else
			const ssize_t w = ::write(fd, buf + done, n - done);
			if (w <= 0) {
				break;
			}
#endif
			done += size_t(w);
		}
		total += done;
		n = 0;
	}
	// Moves back over the last p_bytes bytes written (the closing "}\n"), to write more fields before them.
	void back(int p_bytes) {
		flush();
#ifdef FELLOW_CRASH_WINDOWS
		LARGE_INTEGER d;
		d.QuadPart = -p_bytes;
		SetFilePointerEx(h, d, nullptr, FILE_END);
#else
		lseek(fd, -p_bytes, SEEK_END);
#endif
	}
	void close() {
		flush();
#ifdef FELLOW_CRASH_WINDOWS
		if (h != INVALID_HANDLE_VALUE) {
			CloseHandle(h);
			h = INVALID_HANDLE_VALUE;
		}
#else
		if (fd >= 0) {
			::close(fd);
			fd = -1;
		}
#endif
	}
	void raw(const char *p_s, size_t p_len) {
		while (p_len) {
			const size_t k = MIN(p_len, sizeof(buf) - n);
			memcpy(buf + n, p_s, k);
			n += k;
			p_s += k;
			p_len -= k;
			if (n == sizeof(buf)) {
				flush();
			}
		}
	}
	void str(const char *p_s) { raw(p_s, strlen(p_s)); }
	void ch(char p_c) { raw(&p_c, 1); }
	void json(const char *p_s, size_t p_max = size_t(-1)) {
		static const char *nibbles = "0123456789abcdef";
		ch('"');
		for (size_t i = 0; i < p_max && p_s && p_s[i]; i++) {
			const unsigned char c = (unsigned char)p_s[i];
			if (c == '"' || c == '\\') {
				ch('\\');
				ch(char(c));
			} else if (c == '\n') {
				str("\\n");
			} else if (c == '\t') {
				str("\\t");
			} else if (c < 0x20 || c == 0x7f) {
				char e[6] = { '\\', 'u', '0', '0', nibbles[c >> 4], nibbles[c & 15] };
				raw(e, 6);
			} else {
				ch(char(c));
			}
		}
		ch('"');
	}
	void u64(uint64_t p_v) {
		char d[24];
		int k = 0;
		do {
			d[k++] = char('0' + p_v % 10);
			p_v /= 10;
		} while (p_v);
		while (k) {
			ch(d[--k]);
		}
	}
	void i64(int64_t p_v) {
		if (p_v < 0) {
			ch('-');
			u64(uint64_t(-(p_v + 1)) + 1);
		} else {
			u64(uint64_t(p_v));
		}
	}
	void hex(uint64_t p_v) {
		static const char *digits = "0123456789abcdef";
		char d[20];
		int k = 0;
		do {
			d[k++] = digits[p_v & 15];
			p_v >>= 4;
		} while (p_v);
		str("\"0x");
		while (k) {
			ch(d[--k]);
		}
		ch('"');
	}
	void two(int p_v) {
		ch(char('0' + (p_v / 10) % 10));
		ch(char('0' + p_v % 10));
	}
};

// A frame: its module and the offset in it (what a symbol file is searched with), and the loader's symbol if it
// exports one.
struct FrameInfo {
	char module[160];
	uint64_t base;
	char symbol[160];
	uint64_t symbol_address;
};

void _describe_frame(void *p_address, FrameInfo &r_info) {
	r_info.module[0] = 0;
	r_info.symbol[0] = 0;
	r_info.base = 0;
	r_info.symbol_address = 0;
#if defined(FELLOW_CRASH_POSIX)
	Dl_info info;
	if (dladdr(p_address, &info)) {
		if (info.dli_fname) {
			const char *name = info.dli_fname;
			for (const char *s = info.dli_fname; *s; s++) {
				if (*s == '/') {
					name = s + 1;
				}
			}
			_copy_text(r_info.module, sizeof(r_info.module), name, strlen(name));
		}
		r_info.base = uint64_t(info.dli_fbase);
		if (info.dli_sname) {
			_copy_text(r_info.symbol, sizeof(r_info.symbol), info.dli_sname, strlen(info.dli_sname));
			r_info.symbol_address = uint64_t(info.dli_saddr);
		}
	}
#elif defined(FELLOW_CRASH_WINDOWS)
	HMODULE module = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p_address, &module) && module) {
		r_info.base = uint64_t(module);
		wchar_t w[PATH_BYTES];
		const DWORD len = GetModuleFileNameW(module, w, PATH_BYTES);
		if (len > 0 && len < PATH_BYTES) {
			const wchar_t *name = w;
			for (const wchar_t *s = w; *s; s++) {
				if (*s == L'\\' || *s == L'/') {
					name = s + 1;
				}
			}
			char utf8[PATH_BYTES];
			const int k = WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, PATH_BYTES, nullptr, nullptr);
			if (k > 0) {
				_copy_text(r_info.module, sizeof(r_info.module), utf8, size_t(k));
			}
		}
	}
#else
	(void)p_address;
#endif
}

struct ReportInfo {
	const char *kind = "crash";
	const char *reason = "";
	int64_t signal = -1;
	int64_t code = 0;
	bool has_code = false;
	bool code_hex = false;
	uint64_t fault_address = 0;
	bool has_fault_address = false;
	const char *access = nullptr;
	void *const *frames = nullptr;
	int frame_count = 0;
	bool main_thread = false;
	uint64_t thread_id = 0;
	const char *minidump = nullptr; // a file name in the report directory
	const struct ScriptFrame *script_frames = nullptr; // milestone 98: a hang's script frames, taken without the heap
	int script_frame_count = 0;
	uint64_t stalled_ms = 0; // milestone 98: how long the main thread had made no progress
};

// Milestone 98 (decision 0206): a script's frame copied without the heap (in a signal handler on the main thread).
struct ScriptFrame {
	char function[96];
	char source[192];
	int line;
};

uint64_t _resident_bytes() {
#if defined(__linux__)
	const int fd = ::open("/proc/self/statm", O_RDONLY);
	if (fd < 0) {
		return 0;
	}
	char b[128];
	const ssize_t n = ::read(fd, b, sizeof(b) - 1);
	::close(fd);
	if (n <= 0) {
		return 0;
	}
	b[n] = 0;
	// "size resident shared ...", in pages.
	const char *s = b;
	while (*s && *s != ' ') {
		s++;
	}
	uint64_t pages = 0;
	while (*s == ' ') {
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		pages = pages * 10 + uint64_t(*s - '0');
		s++;
	}
	return pages * g.page_size;
#elif defined(FELLOW_CRASH_WINDOWS)
	typedef BOOL(WINAPI * GetMemoryInfo)(HANDLE, void *, DWORD);
	static GetMemoryInfo fn = nullptr;
	if (!fn) {
		HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
		fn = k32 ? (GetMemoryInfo)(void *)GetProcAddress(k32, "K32GetProcessMemoryInfo") : nullptr;
	}
	struct {
		DWORD cb;
		DWORD PageFaultCount;
		SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage;
	} pmc = {};
	pmc.cb = sizeof(pmc);
	if (fn && fn(GetCurrentProcess(), &pmc, sizeof(pmc))) {
		return uint64_t(pmc.WorkingSetSize);
	}
	return 0;
#else
	return 0;
#endif
}

// The report's name: <kind>_<UTC date>_<UTC time>_<process>_<sequence>.json (the names sort by time).
void _report_name(const char *p_kind, const char *p_extension, uint32_t p_sequence, char *r_name, size_t p_cap) {
	const UtcTime t = _utc_now();
	snprintf(r_name, p_cap, "%s_%04d%02d%02d_%02d%02d%02d_%llu_%u.%s", p_kind, t.year, t.month, t.day, t.hour, t.minute, t.second, (unsigned long long)_process_id(), p_sequence, p_extension);
}

// p_dir + p_name into r_path (p_cap bytes), cut if too long.
void _join_path(char *r_path, size_t p_cap, const char *p_dir, const char *p_name) {
	size_t o = 0;
	for (const char *s = p_dir; *s && o + 1 < p_cap; s++) {
		r_path[o++] = *s;
	}
	for (const char *s = p_name; *s && o + 1 < p_cap; s++) {
		r_path[o++] = *s;
	}
	r_path[o] = 0;
}

// The report's core: everything but what needs the heap. Leaves the file open (ending with "}\n") for more fields.
bool _write_core(const ReportInfo &p, const char *p_path, ReportFile &f) {
	if (!f.open(p_path, false)) {
		return false;
	}
	f.str("{\"fellow_crash_report\":1,\"kind\":");
	f.json(p.kind);
	f.str(",\"reason\":");
	f.json(p.reason);
	if (p.signal >= 0) {
		f.str(",\"signal\":");
		f.i64(p.signal);
	}
	if (p.has_code) {
		f.str(",\"code\":");
		if (p.code_hex) {
			f.hex(uint64_t(uint32_t(p.code)));
		} else {
			f.i64(p.code);
		}
	}
	if (p.has_fault_address) {
		f.str(",\"fault_address\":");
		f.hex(p.fault_address);
	}
	if (p.access) {
		f.str(",\"access\":");
		f.json(p.access);
	}
	const UtcTime t = _utc_now();
	f.str(",\"time_utc\":\"");
	f.u64(uint64_t(t.year));
	f.ch('-');
	f.two(t.month);
	f.ch('-');
	f.two(t.day);
	f.ch('T');
	f.two(t.hour);
	f.ch(':');
	f.two(t.minute);
	f.ch(':');
	f.two(t.second);
	f.str("Z\",\"uptime_ms\":");
	f.u64(_now_msec() - g.start_msec);
	f.str(",\"frame\":");
	f.u64(g.frame.load(std::memory_order_relaxed));
	f.str(",\"thread\":{\"main\":");
	f.str(p.main_thread ? "true" : "false");
	f.str(",\"id\":");
	f.u64(p.thread_id);
	f.ch('}');
	if (p.stalled_ms) {
		f.str(",\"stalled_ms\":");
		f.u64(p.stalled_ms);
	}
	f.str(",\"scene\":");
	f.json(g.scene, sizeof(g.scene));
	f.str(",\"memory\":{\"resident_mb\":");
	f.u64(_resident_bytes() >> 20);
	f.str(",\"engine_static_mb\":");
	f.u64(Memory::get_mem_usage() >> 20);
	f.str(",\"video_mb\":");
	f.u64(g.video_bytes.load(std::memory_order_relaxed) >> 20);
	f.str("},\"context\":");
	const char *context = g.context[g.context_index.load(std::memory_order_acquire) & 1];
	const size_t context_len = _bounded_length(context, CONTEXT_MAX);
	if (context_len) {
		f.raw(context, context_len);
	} else {
		f.str("{}");
	}
	f.str(",\"frames\":[");
	for (int i = 0; i < p.frame_count; i++) {
		FrameInfo info;
		_describe_frame(p.frames[i], info);
		f.str(i ? ",{\"address\":" : "{\"address\":");
		f.hex(uint64_t(p.frames[i]));
		f.str(",\"module\":");
		f.json(info.module, sizeof(info.module));
		f.str(",\"offset\":");
		f.hex(uint64_t(p.frames[i]) - info.base);
		if (info.symbol[0]) {
			f.str(",\"symbol\":");
			f.json(info.symbol, sizeof(info.symbol));
			f.str(",\"symbol_offset\":");
			f.hex(uint64_t(p.frames[i]) - info.symbol_address);
		}
		f.ch('}');
	}
	f.ch(']');
	if (p.script_frame_count > 0) {
		f.str(",\"script_frames\":[");
		for (int i = 0; i < p.script_frame_count; i++) {
			const ScriptFrame &sf = p.script_frames[i];
			f.str(i ? ",{\"function\":" : "{\"function\":");
			f.json(sf.function, sizeof(sf.function));
			f.str(",\"source\":");
			f.json(sf.source, sizeof(sf.source));
			f.str(",\"line\":");
			f.i64(sf.line);
			f.ch('}');
		}
		f.ch(']');
	}
	f.str(",\"breadcrumbs\":[");
	{
		const uint32_t head = g.crumb_head.load(std::memory_order_acquire);
		const uint32_t count = MIN(head, uint32_t(CRUMB_MAX));
		for (uint32_t i = head - count; i != head; i++) {
			const Crumb &c = g.crumbs[i % CRUMB_MAX];
			f.str(i != head - count ? ",{\"ms\":" : "{\"ms\":");
			f.u64(c.msec);
			f.str(",\"category\":");
			f.json(c.category, sizeof(c.category));
			f.str(",\"text\":");
			f.json(c.text, sizeof(c.text));
			f.ch('}');
		}
	}
	f.str("],\"custom\":{");
	{
		bool first = true;
		for (int i = 0; i < CUSTOM_MAX; i++) {
			const Custom &c = g.custom[i];
			if (!c.key[0]) {
				continue;
			}
			if (!first) {
				f.ch(',');
			}
			first = false;
			f.json(c.key, sizeof(c.key));
			f.ch(':');
			f.json(c.value, sizeof(c.value));
		}
	}
	f.str("},\"log\":[");
	{
		const uint32_t cap = g.log_capacity.load(std::memory_order_relaxed);
		const uint32_t head = g.log_head.load(std::memory_order_acquire);
		const uint32_t count = cap ? MIN(head, cap) : 0;
		for (uint32_t i = head - count; i != head; i++) {
			if (i != head - count) {
				f.ch(',');
			}
			f.json(g.log[i % cap], LOG_LINE);
		}
	}
	f.ch(']');
	if (p.minidump) {
		f.str(",\"minidump\":");
		f.json(p.minidump);
	}
	f.str("}\n");
	f.flush();
	return true;
}

// More fields before the closing brace: p_fields is ",\"name\":value...".
void _append(ReportFile &f, const char *p_fields, size_t p_len) {
	f.back(2);
	f.raw(p_fields, p_len);
	f.str("}\n");
	f.flush();
}

// The scripts' backtraces (GDScript's call stack of this thread; in a release build only with
// debug/settings/gdscript/always_track_call_stacks): needs the heap, so written last.
void _append_script_backtraces(ReportFile &f) {
	if (!g.scripts_ok.load()) {
		return;
	}
	Array texts;
	for (const Ref<ScriptBacktrace> &bt : ScriptServer::capture_script_backtraces(false)) {
		if (bt.is_valid() && !bt->is_empty()) {
			texts.push_back(bt->format());
		}
	}
	if (texts.is_empty()) {
		return;
	}
	const CharString cs = (",\"script_backtraces\":" + JSON::stringify(texts, "", false)).utf8();
	_append(f, cs.get_data(), size_t(cs.length()));
}

// The logger: every line the engine prints, the last log_lines kept in a ring.
class FellowCrashLogger : public Logger {
public:
	virtual void logv(const char *p_format, va_list p_list, bool p_err) override {
		const uint32_t cap = g.log_capacity.load(std::memory_order_relaxed);
		if (cap == 0) {
			return;
		}
		char tmp[LOG_LINE * 2];
		const int n = vsnprintf(tmp, sizeof(tmp), p_format, p_list);
		if (n <= 0) {
			return;
		}
		const char *s = tmp;
		const char *end = tmp + MIN(size_t(n), sizeof(tmp) - 1);
		while (s < end) {
			const char *nl = (const char *)memchr(s, '\n', size_t(end - s));
			const size_t len = nl ? size_t(nl - s) : size_t(end - s);
			if (len > 0) {
				const uint32_t slot = g.log_head.fetch_add(1, std::memory_order_acq_rel) % cap;
				_copy_text(g.log[slot], LOG_LINE, s, len);
			}
			s += len + 1;
		}
	}
};

void _add_crumb(const String &p_category, const String &p_text) {
	const uint32_t i = g.crumb_head.fetch_add(1, std::memory_order_acq_rel);
	Crumb &c = g.crumbs[i % CRUMB_MAX];
	c.msec = _now_msec() - g.start_msec;
	_copy_string(c.category, sizeof(c.category), p_category);
	_copy_string(c.text, sizeof(c.text), p_text);
}

// The machine, the build, the project: JSON written once (again when the GPU is known), read by the handlers.
void _build_context() {
	Dictionary d;
	OS *os = OS::get_singleton();
	d["engine"] = GODOT_VERSION_FULL_NAME;
	d["commit"] = GODOT_VERSION_HASH;
#if defined(TOOLS_ENABLED)
	d["build"] = "editor";
#elif defined(DEBUG_ENABLED)
	d["build"] = "debug";
#else
	d["build"] = "release";
#endif
	d["os"] = os->get_name();
	d["os_version"] = os->get_version();
	d["distribution"] = os->get_distribution_name();
	d["cpu"] = os->get_processor_name();
	d["cpu_count"] = os->get_processor_count();
	const Dictionary mem = os->get_memory_info();
	d["ram_mb"] = int64_t(mem.get("physical", -1)) > 0 ? int64_t(mem["physical"]) >> 20 : -1;
	d["locale"] = os->get_locale();
	d["pid"] = os->get_process_id();
	d["executable"] = os->get_executable_path().get_file();
	d["project"] = GLOBAL_GET("application/config/name");
	d["project_version"] = GLOBAL_GET("application/config/version");
	d["physics_3d"] = GLOBAL_GET("physics/3d/physics_engine");
	d["rendering_driver"] = os->get_current_rendering_driver_name();
	d["rendering_method"] = os->get_current_rendering_method();
	if (DisplayServer::get_singleton()) {
		d["display"] = DisplayServer::get_singleton()->get_name();
	}
	if (RenderingServer *rs = RenderingServer::get_singleton()) {
		d["gpu"] = rs->get_video_adapter_name();
		d["gpu_vendor"] = rs->get_video_adapter_vendor();
		d["gpu_api"] = rs->get_video_adapter_api_version();
		const Vector<String> driver = os->get_video_adapter_driver_info();
		d["gpu_driver"] = String(" ").join(driver);
		g.context_has_gpu = !String(d["gpu"]).is_empty() || DisplayServer::get_singleton() == nullptr || DisplayServer::get_singleton()->get_name() == "headless";
	}
	const UtcTime t = _utc_now();
	d["session_start_utc"] = vformat("%04d-%02d-%02dT%02d:%02d:%02dZ", t.year, t.month, t.day, t.hour, t.minute, t.second);
	const CharString cs = JSON::stringify(d, "", true).utf8();
	const int next = (g.context_index.load() + 1) & 1;
	const size_t len = MIN(size_t(cs.length()), size_t(CONTEXT_MAX - 1));
	memcpy(g.context[next], cs.get_data(), len);
	g.context[next][len] = 0;
	g.context_index.store(next, std::memory_order_release);
}

// Once a frame (the scene tree's idle callbacks): the frame, the scene, the GPU's memory now and then.
void _idle() {
	if (!g.installed.load(std::memory_order_relaxed)) {
		return;
	}
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	g.frame.store(frame, std::memory_order_relaxed);
	if (!g.context_has_gpu && frame > 1) {
		_build_context();
		g.context_has_gpu = true; // once, whatever it found
	}
	if (frame == 2 && FellowCrashReporter::get_singleton()) {
		FellowCrashReporter::get_singleton()->auto_upload(); // milestone 102
	}
	SceneTree *st = SceneTree::get_singleton();
	Node *scene = st ? st->get_current_scene() : nullptr;
	const ObjectID id = scene ? scene->get_instance_id() : ObjectID();
	if (id != g.last_scene) {
		g.last_scene = id;
		String path;
		if (scene) {
			path = scene->get_scene_file_path();
			if (path.is_empty()) {
				path = String(scene->get_name());
			}
		}
		_copy_string(g.scene, sizeof(g.scene), path);
		_add_crumb("scene", path.is_empty() ? String("(none)") : path);
	}
	if (frame % 60 == 0 || frame == 2) {
		if (RenderingServer *rs = RenderingServer::get_singleton()) {
			g.video_bytes.store(rs->get_rendering_info(RSE::RENDERING_INFO_VIDEO_MEM_USED), std::memory_order_relaxed);
		}
	}
}

// UTF-32 into UTF-8 without the heap (cut at a character).
void _copy_utf32(char *p_dst, size_t p_cap, const char32_t *p_src) {
	size_t o = 0;
	for (const char32_t *s = p_src; s && *s; s++) {
		const uint32_t c = uint32_t(*s);
		char b[4];
		size_t n;
		if (c < 0x80) {
			b[0] = char(c);
			n = 1;
		} else if (c < 0x800) {
			b[0] = char(0xc0 | (c >> 6));
			b[1] = char(0x80 | (c & 0x3f));
			n = 2;
		} else if (c < 0x10000) {
			b[0] = char(0xe0 | (c >> 12));
			b[1] = char(0x80 | ((c >> 6) & 0x3f));
			b[2] = char(0x80 | (c & 0x3f));
			n = 3;
		} else {
			b[0] = char(0xf0 | (c >> 18));
			b[1] = char(0x80 | ((c >> 12) & 0x3f));
			b[2] = char(0x80 | ((c >> 6) & 0x3f));
			b[3] = char(0x80 | (c & 0x3f));
			n = 4;
		}
		if (o + n + 1 >= p_cap) {
			break;
		}
		memcpy(p_dst + o, b, n);
		o += n;
	}
	p_dst[o] = 0;
}

// Milestone 98: GDScript, found at start-up (ScriptServer's lookup takes a lock).
ScriptLanguage *g_gdscript = nullptr;

// The calling thread's GDScript frames (GDScript keeps them in release builds only with
// debug/settings/gdscript/always_track_call_stacks): the strings are referenced, not copied on the heap.
int _capture_script_frames(ScriptFrame *r_frames, int p_max) {
	ScriptLanguage *sl = g_gdscript;
	if (!sl || !g.scripts_ok.load()) {
		return 0;
	}
	const int count = sl->debug_get_stack_level_count();
	int k = 0;
	for (int i = 0; i < count && k < p_max; i++) {
		const String function = sl->debug_get_stack_level_function(i);
		const String source = sl->debug_get_stack_level_source(i);
		ScriptFrame &f = r_frames[k++];
		_copy_utf32(f.function, sizeof(f.function), function.ptr());
		_copy_utf32(f.source, sizeof(f.source), source.ptr());
		f.line = sl->debug_get_stack_level_line(i);
	}
	return k;
}

// ---------------------------------------------------------------------------------------------- the fatal paths

#if defined(FELLOW_CRASH_POSIX)

const int FATAL_SIGNALS[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
constexpr int FATAL_COUNT = sizeof(FATAL_SIGNALS) / sizeof(FATAL_SIGNALS[0]);
struct sigaction g_previous[FATAL_COUNT];
alignas(16) char g_alt_stack[256 * 1024];

const char *_signal_name(int p_sig) {
	switch (p_sig) {
		case SIGSEGV:
			return "SIGSEGV";
		case SIGBUS:
			return "SIGBUS";
		case SIGILL:
			return "SIGILL";
		case SIGFPE:
			return "SIGFPE";
		case SIGABRT:
			return "SIGABRT";
		case SIGTRAP:
			return "SIGTRAP";
	}
	return "signal";
}

uint64_t _pc_from(void *p_context) {
	if (!p_context) {
		return 0;
	}
#if defined(__linux__)
	const ucontext_t *uc = (const ucontext_t *)p_context;
#if defined(__x86_64__) && defined(REG_RIP)
	return uint64_t(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__) && defined(REG_EIP)
	return uint64_t(uint32_t(uc->uc_mcontext.gregs[REG_EIP]));
#elif defined(__aarch64__)
	return uint64_t(uc->uc_mcontext.pc);
#else
	return 0;
#endif
#elif defined(__APPLE__) && defined(__x86_64__)
	const ucontext_t *uc = (const ucontext_t *)p_context;
	return uint64_t(uc->uc_mcontext->__ss.__rip);
#else
	return 0;
#endif
}

void _posix_chain(int p_sig, siginfo_t *p_info, void *p_context) {
	int index = 0;
	while (index < FATAL_COUNT && FATAL_SIGNALS[index] != p_sig) {
		index++;
	}
	if (index < FATAL_COUNT) {
		const struct sigaction &old = g_previous[index];
		if ((old.sa_flags & SA_SIGINFO) && old.sa_sigaction) {
			old.sa_sigaction(p_sig, p_info, p_context); // Godot's own handler (debug builds) prints and aborts
		} else if (old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN && old.sa_handler != nullptr) {
			old.sa_handler(p_sig);
		}
	}
	// The default action: a fault happens again when the handler returns; a signal sent by kill() or raise() is sent
	// again (blocked until the handler returns).
	struct sigaction dfl;
	memset(&dfl, 0, sizeof(dfl));
	dfl.sa_handler = SIG_DFL;
	sigemptyset(&dfl.sa_mask);
	sigaction(p_sig, &dfl, nullptr);
	if (!p_info || p_info->si_code <= 0) {
		raise(p_sig);
	}
}

void _posix_handler(int p_sig, siginfo_t *p_info, void *p_context) {
	int expected = 0;
	if (g.reporting.compare_exchange_strong(expected, 1)) {
		g.reporting_thread.store(Thread::get_caller_id());
		void *frames[FRAMES_MAX];
		int count = backtrace(frames, FRAMES_MAX);
		// From the faulting instruction on (before it: this handler and the kernel's trampoline).
		const uint64_t pc = _pc_from(p_context);
		int first = 0;
		for (int i = 0; i < count; i++) {
			if (uint64_t(frames[i]) == pc) {
				first = i;
				break;
			}
		}
		ReportInfo ri;
		ri.kind = "crash";
		ri.reason = _signal_name(p_sig);
		ri.signal = p_sig;
		if (p_info) {
			ri.code = p_info->si_code;
			ri.has_code = true;
			if (p_sig == SIGSEGV || p_sig == SIGBUS || p_sig == SIGILL || p_sig == SIGFPE) {
				ri.fault_address = uint64_t(p_info->si_addr);
				ri.has_fault_address = true;
			}
		}
		ri.frames = frames + first;
		ri.frame_count = count - first;
		ri.main_thread = Thread::is_main_thread();
		ri.thread_id = uint64_t(Thread::get_caller_id());
		char name[160];
		_report_name("crash", "json", g.sequence.fetch_add(1), name, sizeof(name));
		_join_path(g.last_report, sizeof(g.last_report), g.dir, name);
		ReportFile f;
		if (_write_core(ri, g.last_report, f)) {
			g.reports_written.fetch_add(1);
			// A stack overflow leaves too little for the scripts' backtraces even on the alternate stack.
			_append_script_backtraces(f);
			f.close();
		}
		g.reporting.store(2);
	} else if (expected == 1 && g.reporting_thread.load() != Thread::get_caller_id()) {
		// Another thread is writing its report: let it finish before this one ends the process.
		for (int i = 0; i < 500 && g.reporting.load() == 1; i++) {
			usleep(10000);
		}
	}
	_posix_chain(p_sig, p_info, p_context);
}

void _install_handlers() {
	g.page_size = uint64_t(sysconf(_SC_PAGESIZE));
	void *warm[2];
	backtrace(warm, 2); // loads the unwinder now, not in the handler
	stack_t ss;
	memset(&ss, 0, sizeof(ss));
	ss.ss_sp = g_alt_stack;
	ss.ss_size = sizeof(g_alt_stack);
	ss.ss_flags = 0;
	sigaltstack(&ss, nullptr); // the main thread's stack overflowing still runs the handler
	for (int i = 0; i < FATAL_COUNT; i++) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = _posix_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
		sigaction(FATAL_SIGNALS[i], &sa, &g_previous[i]);
	}
}

void _capture_current(void **r_frames, int &r_count) {
	r_count = backtrace(r_frames, FRAMES_MAX);
}

// Milestone 98: the stalled main thread's frames, taken by the main thread itself in a signal the watchdog sends it
// (as a sampling profiler's): its C++ frames and its script's, into static buffers.
struct HangCapture {
	std::atomic<int> state{ 0 }; // 1 asked, 2 taken
	void *frames[FRAMES_MAX];
	int count = 0;
	int first = 0;
	ScriptFrame script[24];
	int script_count = 0;
};
HangCapture g_hang;
int g_hang_signal = 0;
pthread_t g_main_pthread;

void _hang_handler(int, siginfo_t *, void *p_context) {
	if (!Thread::is_main_thread() || g_hang.state.load(std::memory_order_acquire) != 1) {
		return;
	}
	g_hang.count = backtrace(g_hang.frames, FRAMES_MAX);
	const uint64_t pc = _pc_from(p_context);
	g_hang.first = 0;
	for (int i = 0; i < g_hang.count; i++) {
		if (uint64_t(g_hang.frames[i]) == pc) {
			g_hang.first = i;
			break;
		}
	}
	g_hang.script_count = _capture_script_frames(g_hang.script, 24);
	g_hang.state.store(2, std::memory_order_release);
}

// A real-time signal nothing else handles (glibc's and .NET's are below SIGRTMIN + 4); SIGUSR2 where there are none.
void _install_hang_signal() {
	g_main_pthread = pthread_self();
#if defined(SIGRTMIN)
	const int first = SIGRTMIN + 4;
	const int last = SIGRTMAX - 1;
#else
	const int first = SIGUSR2;
	const int last = SIGUSR2;
#endif
	for (int sig = first; sig <= last; sig++) {
		struct sigaction old;
		memset(&old, 0, sizeof(old));
		sigaction(sig, nullptr, &old);
		if ((old.sa_flags & SA_SIGINFO) || old.sa_handler != SIG_DFL) {
			continue;
		}
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_sigaction = _hang_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_SIGINFO | SA_RESTART; // a system call the signal interrupts goes on
		if (sigaction(sig, &sa, nullptr) == 0) {
			g_hang_signal = sig;
		}
		return;
	}
}

int _hang_capture(void **&r_frames, ScriptFrame *&r_script, int &r_script_count) {
	r_script_count = 0;
	if (!g_hang_signal) {
		return 0;
	}
	g_hang.state.store(1, std::memory_order_release);
	if (pthread_kill(g_main_pthread, g_hang_signal) != 0) {
		g_hang.state.store(0);
		return 0;
	}
	for (int i = 0; i < 500 && g_hang.state.load(std::memory_order_acquire) != 2; i++) {
		usleep(1000);
	}
	if (g_hang.state.load(std::memory_order_acquire) != 2) {
		g_hang.state.store(0); // not taken in half a second (a thread blocked in the kernel): the report without them
		return 0;
	}
	r_frames = g_hang.frames + g_hang.first;
	r_script = g_hang.script;
	r_script_count = g_hang.script_count;
	return g_hang.count - g_hang.first;
}

void _hang_release() {
	g_hang.state.store(0, std::memory_order_release);
}

bool _debugger_attached() {
#if defined(__linux__)
	const int fd = ::open("/proc/self/status", O_RDONLY);
	if (fd < 0) {
		return false;
	}
	char b[2048];
	const ssize_t n = ::read(fd, b, sizeof(b) - 1);
	::close(fd);
	if (n <= 0) {
		return false;
	}
	b[n] = 0;
	const char *t = strstr(b, "TracerPid:");
	if (!t) {
		return false;
	}
	t += 10;
	while (*t == ' ' || *t == '\t') {
		t++;
	}
	return *t >= '1' && *t <= '9';
#else
	return false;
#endif
}

#elif defined(FELLOW_CRASH_WINDOWS)

typedef BOOL(WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpWriteDumpFn g_minidump = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;
void(__cdecl *g_previous_abort)(int) = nullptr;

// What the filter hands the thread that writes the report.
struct WindowsCrash {
	EXCEPTION_POINTERS *pointers = nullptr;
	DWORD thread = 0;
	bool main_thread = false;
	uint64_t thread_id = 0;
	const char *reason = "";
	void *frames[FRAMES_MAX];
	int frame_count = 0;
};
WindowsCrash g_crash;

const char *_exception_name(DWORD p_code) {
	switch (p_code) {
		case EXCEPTION_ACCESS_VIOLATION:
			return "EXCEPTION_ACCESS_VIOLATION";
		case EXCEPTION_STACK_OVERFLOW:
			return "EXCEPTION_STACK_OVERFLOW";
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			return "EXCEPTION_ILLEGAL_INSTRUCTION";
		case EXCEPTION_PRIV_INSTRUCTION:
			return "EXCEPTION_PRIV_INSTRUCTION";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			return "EXCEPTION_INT_DIVIDE_BY_ZERO";
		case EXCEPTION_INT_OVERFLOW:
			return "EXCEPTION_INT_OVERFLOW";
		case EXCEPTION_BREAKPOINT:
			return "EXCEPTION_BREAKPOINT";
		case EXCEPTION_IN_PAGE_ERROR:
			return "EXCEPTION_IN_PAGE_ERROR";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			return "EXCEPTION_DATATYPE_MISALIGNMENT";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
		case EXCEPTION_FLT_INVALID_OPERATION:
			return "EXCEPTION_FLT_INVALID_OPERATION";
		case 0xC0000374:
			return "STATUS_HEAP_CORRUPTION";
		case 0xE06D7363:
			return "C++ exception";
	}
	return "exception";
}

bool _readable(const void *p_address, size_t p_bytes) {
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(p_address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
		return false;
	}
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) {
		return false;
	}
	return uintptr_t(p_address) + p_bytes <= uintptr_t(mbi.BaseAddress) + mbi.RegionSize;
}

// The faulting thread's frames from its context, unwound by the modules' unwind tables (no symbols needed).
int _unwind(const CONTEXT *p_context, void **r_frames, int p_max) {
#if defined(_M_X64) || defined(__x86_64__)
	CONTEXT c = *p_context;
	int n = 0;
	while (n < p_max && c.Rip) {
		r_frames[n++] = (void *)c.Rip;
		if (!_readable((void *)c.Rsp, 8)) { // (a new thread's stack ends a few hundred bytes above it: no more)
			break;
		}
		DWORD64 image_base = 0;
		PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image_base, nullptr);
		if (!fn) {
			c.Rip = *(DWORD64 *)c.Rsp; // a leaf function: the return address on top of the stack
			c.Rsp += 8;
		} else {
			PVOID handler_data = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, c.Rip, fn, &c, &handler_data, &establisher, nullptr);
		}
	}
	return n;
#else
	(void)p_context;
	(void)r_frames;
	(void)p_max;
	return 0;
#endif
}

// A minidump of the process (with p_pointers, the exception's thread and context) beside the report, its size appended.
void _append_minidump(ReportFile &f, const char *p_name, EXCEPTION_POINTERS *p_pointers, DWORD p_thread) {
	char dump_path[PATH_BYTES];
	_join_path(dump_path, sizeof(dump_path), g.dir, p_name);
	wchar_t w[PATH_BYTES];
	if (!MultiByteToWideChar(CP_UTF8, 0, dump_path, -1, w, PATH_BYTES)) {
		return;
	}
	HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return;
	}
	MINIDUMP_EXCEPTION_INFORMATION mei;
	mei.ThreadId = p_thread;
	mei.ExceptionPointers = p_pointers;
	mei.ClientPointers = FALSE;
	const MINIDUMP_TYPE type = MINIDUMP_TYPE(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory);
	const BOOL ok = g_minidump(GetCurrentProcess(), GetCurrentProcessId(), h, type, p_pointers ? &mei : nullptr, nullptr, nullptr);
	LARGE_INTEGER size;
	size.QuadPart = 0;
	GetFileSizeEx(h, &size);
	CloseHandle(h);
	char fields[96];
	const int k = snprintf(fields, sizeof(fields), ",\"minidump_bytes\":%lld", ok ? (long long)size.QuadPart : -1LL);
	_append(f, fields, size_t(k));
}

DWORD WINAPI _windows_report_thread(LPVOID) {
	ReportInfo ri;
	ri.kind = "crash";
	ri.main_thread = g_crash.main_thread;
	ri.thread_id = g_crash.thread_id;
	ri.reason = g_crash.reason;
	if (g_crash.pointers && g_crash.pointers->ExceptionRecord) {
		const EXCEPTION_RECORD *er = g_crash.pointers->ExceptionRecord;
		ri.reason = _exception_name(er->ExceptionCode);
		ri.code = int64_t(er->ExceptionCode);
		ri.has_code = true;
		ri.code_hex = true;
		ri.fault_address = uint64_t(er->ExceptionAddress);
		ri.has_fault_address = true;
		if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
			ri.access = er->ExceptionInformation[0] == 0 ? "read" : (er->ExceptionInformation[0] == 8 ? "execute" : "write");
			ri.fault_address = uint64_t(er->ExceptionInformation[1]);
		}
		if (g_crash.pointers->ContextRecord) {
			g_crash.frame_count = _unwind(g_crash.pointers->ContextRecord, g_crash.frames, FRAMES_MAX);
		}
	}
	ri.frames = g_crash.frames;
	ri.frame_count = g_crash.frame_count;
	char name[160];
	const uint32_t sequence = g.sequence.fetch_add(1);
	_report_name("crash", "json", sequence, name, sizeof(name));
	char dump_name[160];
	_report_name("crash", "dmp", sequence, dump_name, sizeof(dump_name));
	const bool dump = g.minidumps && g_minidump;
	ri.minidump = dump ? dump_name : nullptr;
	_join_path(g.last_report, sizeof(g.last_report), g.dir, name);
	ReportFile f;
	if (!_write_core(ri, g.last_report, f)) {
		return 0;
	}
	g.reports_written.fetch_add(1);
	if (dump) {
		_append_minidump(f, dump_name, g_crash.pointers, g_crash.thread);
	}
	f.close();
	return 0;
}

// Runs the report's writing on a thread of its own (a stack overflow leaves the faulting thread no stack; a
// minidump is best written from another thread) and waits for it.
// Apart from _windows_report(), whose frame must stay small: after a stack overflow the faulting thread has only the
// stack the guarantee below leaves it.
_NO_INLINE_ void _windows_append_scripts() {
	ReportFile f;
	if (f.open(g.last_report, true)) {
		_append_script_backtraces(f);
		f.close();
	}
}

void _windows_report(EXCEPTION_POINTERS *p_pointers, const char *p_reason) {
	g_crash.pointers = p_pointers;
	g_crash.thread = GetCurrentThreadId();
	g_crash.main_thread = Thread::is_main_thread();
	g_crash.thread_id = uint64_t(Thread::get_caller_id());
	g_crash.reason = p_reason;
	if (!p_pointers) {
		// abort(), a pure virtual call: this thread's own frames (no exception context).
		g_crash.frame_count = int(RtlCaptureStackBackTrace(0, FRAMES_MAX, g_crash.frames, nullptr));
	}
	HANDLE t = CreateThread(nullptr, 512 * 1024, _windows_report_thread, nullptr, 0, nullptr);
	if (t) {
		WaitForSingleObject(t, 60000);
		CloseHandle(t);
	}
	const bool overflow = p_pointers && p_pointers->ExceptionRecord && p_pointers->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW;
	if (!overflow) {
		_windows_append_scripts();
	}
}

bool _claim() {
	int expected = 0;
	if (g.reporting.compare_exchange_strong(expected, 1)) {
		g.reporting_thread.store(Thread::get_caller_id());
		return true;
	}
	if (expected == 1 && g.reporting_thread.load() != Thread::get_caller_id()) {
		for (int i = 0; i < 500 && g.reporting.load() == 1; i++) {
			Sleep(10);
		}
	}
	return false;
}

LONG WINAPI _windows_filter(EXCEPTION_POINTERS *p_pointers) {
	if (_claim()) {
		_windows_report(p_pointers, "exception");
		g.reporting.store(2);
	}
	return g_previous_filter ? g_previous_filter(p_pointers) : EXCEPTION_CONTINUE_SEARCH;
}

void __cdecl _windows_abort(int p_sig) {
	if (_claim()) {
		_windows_report(nullptr, "SIGABRT");
		g.reporting.store(2);
	}
	if (g_previous_abort && g_previous_abort != SIG_DFL && g_previous_abort != SIG_IGN) {
		g_previous_abort(p_sig);
	}
}

void __cdecl _windows_purecall() {
	if (_claim()) {
		_windows_report(nullptr, "pure virtual call");
		g.reporting.store(2);
	}
	abort();
}

void _install_handlers() {
	if (g.minidumps) {
		// Loaded now: loading a library in the filter could wait on the loader's lock the crash holds.
		HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
		g_minidump = dbghelp ? (MiniDumpWriteDumpFn)(void *)GetProcAddress(dbghelp, "MiniDumpWriteDump") : nullptr;
	}
	_resident_bytes(); // resolves its function now
	// The main thread's stack overflowing leaves the filter 64 KiB (by default a page or two: not enough to start the
	// thread that writes the report).
	ULONG guarantee = 64 * 1024;
	SetThreadStackGuarantee(&guarantee);
	g_previous_filter = SetUnhandledExceptionFilter(_windows_filter);
	g_previous_abort = signal(SIGABRT, _windows_abort);
	_set_purecall_handler(_windows_purecall);
}

void _capture_current(void **r_frames, int &r_count) {
	r_count = int(RtlCaptureStackBackTrace(0, FRAMES_MAX, r_frames, nullptr));
}

// Milestone 98: the stalled main thread's frames, from its context while it is suspended for a moment (no script
// frames: GDScript's stack is the thread's own; the minidump has every thread's).
HANDLE g_main_thread = nullptr;
void *g_hang_frames[FRAMES_MAX];

void _install_hang_signal() {
	g_main_thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
}

int _hang_capture(void **&r_frames, ScriptFrame *&r_script, int &r_script_count) {
	r_script = nullptr;
	r_script_count = 0;
	if (!g_main_thread || SuspendThread(g_main_thread) == DWORD(-1)) {
		return 0;
	}
	CONTEXT ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.ContextFlags = CONTEXT_FULL;
	int n = 0;
	if (GetThreadContext(g_main_thread, &ctx)) {
		n = _unwind(&ctx, g_hang_frames, FRAMES_MAX); // no heap, no lock the main thread could hold
	}
	ResumeThread(g_main_thread);
	r_frames = g_hang_frames;
	return n;
}

void _hang_release() {}

bool _debugger_attached() {
	return IsDebuggerPresent();
}

#else

void _install_handlers() {}

void _capture_current(void **r_frames, int &r_count) {
	(void)r_frames;
	r_count = 0;
}

void _install_hang_signal() {}

int _hang_capture(void **&r_frames, ScriptFrame *&r_script, int &r_script_count) {
	r_frames = nullptr;
	r_script = nullptr;
	r_script_count = 0;
	return 0;
}

void _hang_release() {}

bool _debugger_attached() {
	return false;
}

#endif

// ------------------------------------------------------------------------------- milestone 98: the watchdog

std::atomic<bool> g_watch_quit{ false };
Thread *g_watch_thread = nullptr;
uint64_t g_hang_ms = 0;
uint64_t g_main_id = 0;
std::atomic<uint32_t> g_hangs{ 0 };

// A clock that stops while the machine sleeps (a laptop's lid closed is no hang).
uint64_t _awake_msec() {
#if defined(FELLOW_CRASH_WINDOWS)
	ULONGLONG t = 0;
	QueryUnbiasedInterruptTime(&t);
	return uint64_t(t / 10000);
#elif defined(__APPLE__)
	struct timespec ts;
	clock_gettime(CLOCK_UPTIME_RAW, &ts);
	return uint64_t(ts.tv_sec) * 1000 + uint64_t(ts.tv_nsec) / 1000000;
#else
	return _now_msec(); // CLOCK_MONOTONIC stops while Linux is suspended
#endif
}

// The stalled main thread's report: written from the watchdog's thread without the heap (the main thread may hold its
// lock), its frames taken from the main thread.
void _report_hang(uint64_t p_stalled_ms, char *r_path, size_t p_cap) {
	void **frames = nullptr;
	ScriptFrame *script = nullptr;
	int script_count = 0;
	const int count = _hang_capture(frames, script, script_count);
	ReportInfo ri;
	ri.kind = "hang";
	ri.reason = "the main thread made no progress";
	ri.frames = frames;
	ri.frame_count = count;
	ri.main_thread = true;
	ri.thread_id = g_main_id;
	ri.script_frames = script;
	ri.script_frame_count = script_count;
	ri.stalled_ms = p_stalled_ms;
	const uint32_t sequence = g.sequence.fetch_add(1);
	char name[160];
	_report_name("hang", "json", sequence, name, sizeof(name));
#if defined(FELLOW_CRASH_WINDOWS)
	char dump_name[160];
	_report_name("hang", "dmp", sequence, dump_name, sizeof(dump_name));
	const bool dump = g.minidumps && g_minidump;
	ri.minidump = dump ? dump_name : nullptr;
#endif
	_join_path(r_path, p_cap, g.dir, name);
	ReportFile f;
	if (_write_core(ri, r_path, f)) {
		g.reports_written.fetch_add(1);
#if defined(FELLOW_CRASH_WINDOWS)
		if (dump) {
			_append_minidump(f, dump_name, nullptr, GetThreadId(g_main_thread));
		}
#endif
		f.close();
	} else {
		r_path[0] = 0;
	}
	_hang_release();
	g_hangs.fetch_add(1);
}

void _watchdog(void *) {
	uint64_t last_frame = g.frame.load(std::memory_order_relaxed);
	uint64_t last_progress = _awake_msec();
	uint64_t last_tick = last_progress;
	bool stalled = false;
	char hang_path[PATH_BYTES] = {};
	while (!g_watch_quit.load(std::memory_order_acquire)) {
		OS::get_singleton()->delay_usec(100000);
		const uint64_t now = _awake_msec();
		if (now - last_tick > 3000) {
			last_progress = now; // this thread was not run for seconds: the process was stopped, not the main thread
		}
		last_tick = now;
		const uint64_t frame = g.frame.load(std::memory_order_relaxed);
		if (frame != last_frame) {
			if (stalled) {
				// It recovered: how long the stall lasted, in the report and as a breadcrumb.
				const uint64_t stall = now - last_progress;
				if (hang_path[0]) {
					ReportFile f;
					if (f.open(hang_path, true)) {
						char fields[64];
						const int k = snprintf(fields, sizeof(fields), ",\"recovered_after_ms\":%llu", (unsigned long long)stall);
						_append(f, fields, size_t(k));
						f.close();
					}
				}
				_add_crumb("hang", vformat("the main thread stalled for %.1f s", double(stall) / 1000.0));
				stalled = false;
			}
			last_frame = frame;
			last_progress = now;
			continue;
		}
		if (stalled || frame == 0 || now - last_progress < g_hang_ms || g.reporting.load() != 0) {
			continue;
		}
		if (EngineDebugger::is_active() || _debugger_attached()) {
			last_progress = now; // stopped at a breakpoint
			continue;
		}
		stalled = true;
		_report_hang(now - last_progress, hang_path, sizeof(hang_path));
	}
}

// What debug_crash() runs.
volatile int g_never = 0;

int _overflow(int p_depth) {
	volatile char buf[2048];
	buf[p_depth & 2047] = char(p_depth);
	if (g_never) {
		return 0;
	}
	return _overflow(p_depth + 1) + buf[(p_depth * 7) & 2047];
}

void _segfault_on_thread(void *) {
	volatile int *volatile p = nullptr; // both volatile: GCC drops a plain store through a null pointer
	*p = 7;
}

BinaryMutex g_write_mutex;

struct SpinGuard {
	const SpinLock &l;
	explicit SpinGuard(const SpinLock &p_lock) :
			l(p_lock) { l.lock(); }
	~SpinGuard() { l.unlock(); }
};

// The report key a name sorts by: what follows the kind ("20261113_001502_1234_0").
String _report_key(const String &p_file) {
	const int under = p_file.find_char('_');
	return under >= 0 ? p_file.substr(under + 1).get_basename() : p_file;
}

int _key_sequence(const String &p_key) {
	return p_key.get_slicec('_', 3).to_int();
}

// Newest first: by time, then by process and sequence.
struct ReportOrder {
	bool operator()(const String &a, const String &b) const {
		const String ka = _report_key(a.get_file());
		const String kb = _report_key(b.get_file());
		const String ta = ka.get_slicec('_', 0) + ka.get_slicec('_', 1);
		const String tb = kb.get_slicec('_', 0) + kb.get_slicec('_', 1);
		if (ta != tb) {
			return ta > tb;
		}
		if (ka.get_slicec('_', 2) != kb.get_slicec('_', 2)) {
			return ka.get_slicec('_', 2).to_int() > kb.get_slicec('_', 2).to_int();
		}
		return _key_sequence(ka) > _key_sequence(kb);
	}
};

} // namespace

// ------------------------------------------------------------------------------------------------ the reporter

void FellowCrashReporter::create() {
	if (!singleton) {
		memnew(FellowCrashReporter);
	}
}

void FellowCrashReporter::destroy() {
	if (singleton) {
		memdelete(singleton);
	}
}

FellowCrashReporter::FellowCrashReporter() {
	singleton = this;
	const bool setting = GLOBAL_DEF(PropertyInfo(Variant::BOOL, "debug/fellow/crash_reports/enabled"), false);
	const String dir = GLOBAL_DEF(PropertyInfo(Variant::STRING, "debug/fellow/crash_reports/directory"), "user://crash_reports");
	const int log_lines = GLOBAL_DEF(PropertyInfo(Variant::INT, "debug/fellow/crash_reports/log_lines", PROPERTY_HINT_RANGE, "0,1024,1"), 200);
	max_reports = GLOBAL_DEF(PropertyInfo(Variant::INT, "debug/fellow/crash_reports/max_reports", PROPERTY_HINT_RANGE, "1,100,1"), 10);
	g.minidumps = GLOBAL_DEF(PropertyInfo(Variant::BOOL, "debug/fellow/crash_reports/minidumps"), true);
	const double hang_seconds = GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "debug/fellow/crash_reports/hang_seconds", PROPERTY_HINT_RANGE, "0,120,0.5,suffix:s"), 10.0);
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "debug/fellow/crash_reports/upload_url"), "");
	upload_minidumps = GLOBAL_DEF(PropertyInfo(Variant::BOOL, "debug/fellow/crash_reports/upload_minidumps"), true);
	upload_automatically = GLOBAL_DEF(PropertyInfo(Variant::BOOL, "debug/fellow/crash_reports/upload_automatically"), false);
	ProjectSettings *ps = ProjectSettings::get_singleton();
	const bool fellow = !ps->has_setting("rendering/fellow/enabled") || bool(ps->get_setting("rendering/fellow/enabled"));
	enabled = setting && fellow && !Engine::get_singleton()->is_editor_hint();
	if (!enabled) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	directory = ProjectSettings::get_singleton()->globalize_path(dir);
	if (!directory.ends_with("/")) {
		directory += "/";
	}
	DirAccess::make_dir_recursive_absolute(directory);
	if (g.installed.load()) {
		return; // a second reporter in the same process (not expected): the first one's handlers stay
	}
	g.start_msec = _now_msec();
	const CharString dir_utf8 = directory.utf8();
	if (dir_utf8.length() >= PATH_BYTES - 128) {
		ERR_PRINT("FellowCrashReporter: the report directory's path is too long: " + directory);
		enabled = false;
		return;
	}
	memcpy(g.dir, dir_utf8.get_data(), size_t(dir_utf8.length()) + 1);
	g.log_capacity.store(uint32_t(CLAMP(log_lines, 0, LOG_MAX)));
	if (!g.logger_added) {
		OS::get_singleton()->add_logger(memnew(FellowCrashLogger)); // owned by the OS from here
		g.logger_added = true;
	}
	_build_context();
	_install_handlers();
	if (!g.idle_added) {
		SceneTree::add_idle_callback(&_idle);
		g.idle_added = true;
	}
	g.scripts_ok.store(true);
	g.installed.store(true);
	_add_crumb("engine", "started");
	_prune_reports(directory);
	_prune_reports(directory + "sent/");
	// Milestone 98: the watchdog, for the main thread making no progress.
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		ScriptLanguage *sl = ScriptServer::get_language(i);
		if (sl && sl->get_name() == "GDScript") {
			g_gdscript = sl;
		}
	}
	if (hang_seconds > 0.0) {
		g_hang_ms = uint64_t(hang_seconds * 1000.0);
		g_main_id = uint64_t(Thread::get_caller_id());
		_install_hang_signal();
		g_watch_quit.store(false);
		g_watch_thread = memnew(Thread);
		g_watch_thread->start(&_watchdog, nullptr);
	}
	setup_usec = OS::get_singleton()->get_ticks_usec() - t0;
}

FellowCrashReporter::~FellowCrashReporter() {
	if (upload_thread) {
		upload_cancel.store(true);
		upload_thread->wait_to_finish();
		memdelete(upload_thread);
		upload_thread = nullptr;
	}
	if (g_watch_thread) {
		g_watch_quit.store(true);
		g_watch_thread->wait_to_finish();
		memdelete(g_watch_thread);
		g_watch_thread = nullptr;
	}
	// The handlers stay (a crash while the engine shuts down is reported too), without the scripts' backtraces.
	g.scripts_ok.store(false);
	if (singleton == this) {
		singleton = nullptr;
	}
}

// A directory's reports, newest first.
static PackedStringArray _list_reports(const String &p_dir) {
	PackedStringArray out;
	if (p_dir.is_empty() || !DirAccess::dir_exists_absolute(p_dir)) {
		return out;
	}
	Vector<String> files;
	for (const String &file : DirAccess::get_files_at(p_dir)) {
		if (file.get_extension() == "json" && (file.begins_with("crash_") || file.begins_with("report_") || file.begins_with("hang_"))) {
			files.push_back(p_dir + file);
		}
	}
	files.sort_custom<ReportOrder>();
	for (const String &f : files) {
		out.push_back(f);
	}
	return out;
}

void FellowCrashReporter::_prune_reports(const String &p_dir) {
	PackedStringArray reports = _list_reports(p_dir);
	for (int i = max_reports; i < reports.size(); i++) {
		DirAccess::remove_absolute(reports[i]);
		const String dump = reports[i].get_basename() + ".dmp";
		if (FileAccess::exists(dump)) {
			DirAccess::remove_absolute(dump);
		}
	}
}

void FellowCrashReporter::add_breadcrumb(const String &p_message, const String &p_category) {
	if (!enabled) {
		return;
	}
	_add_crumb(p_category, p_message);
}

void FellowCrashReporter::set_custom_value(const String &p_key, const Variant &p_value) {
	if (!enabled) {
		return;
	}
	ERR_FAIL_COND_MSG(p_key.is_empty(), "A custom value needs a key.");
	const CharString key = p_key.utf8();
	ERR_FAIL_COND_MSG(key.length() >= CUSTOM_KEY - 1, vformat("The key \"%s\" is longer than %d bytes.", p_key, CUSTOM_KEY - 2));
	char value[CUSTOM_VALUE];
	_copy_string(value, sizeof(value), String(p_value));
	SpinGuard lock(g.custom_lock);
	int slot = -1;
	for (int i = 0; i < CUSTOM_MAX; i++) {
		if (strcmp(g.custom[i].key, key.get_data()) == 0) {
			slot = i;
			break;
		}
		if (slot < 0 && !g.custom[i].key[0]) {
			slot = i;
		}
	}
	ERR_FAIL_COND_MSG(slot < 0, vformat("No room for another custom value (%d at most).", CUSTOM_MAX));
	memcpy(g.custom[slot].value, value, sizeof(value));
	if (strcmp(g.custom[slot].key, key.get_data()) != 0) {
		memset(g.custom[slot].key, 0, CUSTOM_KEY);
		memcpy(g.custom[slot].key, key.get_data(), size_t(key.length()));
	}
}

void FellowCrashReporter::remove_custom_value(const String &p_key) {
	const CharString key = p_key.utf8();
	SpinGuard lock(g.custom_lock);
	for (int i = 0; i < CUSTOM_MAX; i++) {
		if (g.custom[i].key[0] && strcmp(g.custom[i].key, key.get_data()) == 0) {
			g.custom[i].key[0] = 0;
		}
	}
}

Dictionary FellowCrashReporter::get_custom_values() const {
	Dictionary d;
	SpinGuard lock(g.custom_lock);
	for (int i = 0; i < CUSTOM_MAX; i++) {
		if (g.custom[i].key[0]) {
			d[String::utf8(g.custom[i].key)] = String::utf8(g.custom[i].value);
		}
	}
	return d;
}

String FellowCrashReporter::write_report(const String &p_reason) {
	if (!enabled) {
		return String();
	}
	MutexLock lock(g_write_mutex);
	void *frames[FRAMES_MAX];
	int count = 0;
	_capture_current(frames, count);
	const CharString reason = p_reason.utf8();
	ReportInfo ri;
	ri.kind = "report";
	ri.reason = reason.get_data();
	ri.frames = frames;
	ri.frame_count = count;
	ri.main_thread = Thread::is_main_thread();
	ri.thread_id = uint64_t(Thread::get_caller_id());
	char name[160];
	_report_name("report", "json", g.sequence.fetch_add(1), name, sizeof(name));
	char path[PATH_BYTES];
	_join_path(path, sizeof(path), g.dir, name);
	_add_crumb("report", p_reason);
	ReportFile f;
	ERR_FAIL_COND_V_MSG(!_write_core(ri, path, f), String(), vformat("Could not write the report \"%s\".", String::utf8(path)));
	_append_script_backtraces(f);
	f.close();
	g.reports_written.fetch_add(1);
	const String written_path = String::utf8(path);
	written.push_back(written_path.get_file());
	return written_path;
}

PackedStringArray FellowCrashReporter::get_reports() const {
	return _list_reports(directory);
}

Dictionary FellowCrashReporter::read_report(const String &p_path) {
	const String text = FileAccess::get_file_as_string(p_path);
	ERR_FAIL_COND_V_MSG(text.is_empty(), Dictionary(), vformat("Could not read the report \"%s\".", p_path));
	const Variant parsed = JSON::parse_string(text);
	ERR_FAIL_COND_V_MSG(parsed.get_type() != Variant::DICTIONARY, Dictionary(), vformat("The report \"%s\" is not a JSON object.", p_path));
	Dictionary d = parsed;
	d["path"] = p_path;
	const String dump = p_path.get_basename() + ".dmp";
	if (d.has("minidump") && FileAccess::exists(dump)) {
		d["minidump_path"] = dump;
	}
	return d;
}

Dictionary FellowCrashReporter::get_pending_report() const {
	if (!enabled) {
		return Dictionary();
	}
	const String acknowledged = FileAccess::get_file_as_string(directory + "acknowledged.txt").strip_edges();
	for (const String &path : get_reports()) {
		const String file = path.get_file();
		if (written.has(file)) {
			continue; // this session's own
		}
		if (!acknowledged.is_empty() && !ReportOrder()(file, acknowledged)) {
			return Dictionary(); // the newest earlier report is acknowledged
		}
		return read_report(path);
	}
	return Dictionary();
}

void FellowCrashReporter::acknowledge_reports() {
	if (!enabled) {
		return;
	}
	const PackedStringArray reports = get_reports();
	if (reports.is_empty()) {
		return;
	}
	Ref<FileAccess> f = FileAccess::open(directory + "acknowledged.txt", FileAccess::WRITE);
	ERR_FAIL_COND(f.is_null());
	f->store_line(reports[0].get_file());
}

// ------------------------------------------------------------------------- milestone 102: the reports uploaded

namespace {

void _append_bytes(PackedByteArray &r_body, const uint8_t *p_data, int p_size) {
	if (p_size > 0) {
		const int at = r_body.size();
		r_body.resize(at + p_size);
		memcpy(r_body.ptrw() + at, p_data, p_size);
	}
}

// One part of a multipart/form-data body.
void _part(PackedByteArray &r_body, const String &p_boundary, const String &p_name, const String &p_filename, const String &p_type, const uint8_t *p_data, int p_size) {
	String head = "--" + p_boundary + "\r\nContent-Disposition: form-data; name=\"" + p_name + "\"";
	if (!p_filename.is_empty()) {
		head += "; filename=\"" + p_filename + "\"";
	}
	head += "\r\n";
	if (!p_type.is_empty()) {
		head += "Content-Type: " + p_type + "\r\n";
	}
	head += "\r\n";
	const CharString h = head.utf8();
	_append_bytes(r_body, (const uint8_t *)h.get_data(), h.length());
	_append_bytes(r_body, p_data, p_size);
	r_body.append('\r');
	r_body.append('\n');
}

// A POST of p_body to p_url: the response's code, or a negative number when the request got none (UploadError).
enum UploadError {
	UPLOAD_BAD_URL = -1,
	UPLOAD_CANT_RESOLVE = -2,
	UPLOAD_CANT_CONNECT = -3,
	UPLOAD_TLS_HANDSHAKE = -4,
	UPLOAD_NO_RESPONSE = -5,
};

int _post(const String &p_url, const String &p_content_type, const PackedByteArray &p_body, const std::atomic<bool> &p_cancel) {
	String scheme, host, path, fragment;
	int port = -1;
	if (p_url.parse_url(scheme, host, port, path, fragment) != OK || host.is_empty() || (scheme != "http://" && scheme != "https://")) {
		return UPLOAD_BAD_URL;
	}
	const bool tls = scheme == "https://";
	if (port < 0) {
		port = tls ? 443 : 80;
	}
	if (path.is_empty()) {
		path = "/";
	}
	Ref<HTTPClient> client = Ref<HTTPClient>(HTTPClient::create());
	if (client.is_null() || client->connect_to_host(host, port, tls ? TLSOptions::client() : Ref<TLSOptions>()) != OK) {
		return UPLOAD_CANT_CONNECT;
	}
	// The connection (with TLS, its handshake) within 30 s — on Windows a refused one is reported only at the TCP
	// connection's own timeout, and a host's addresses are tried in turn — then the request within 30 s and a second
	// more for each 100 KB sent.
	uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 30000;
	auto wait_while = [&](HTTPClient::Status p_a, HTTPClient::Status p_b) {
		while ((client->get_status() == p_a || client->get_status() == p_b) && OS::get_singleton()->get_ticks_msec() < deadline && !p_cancel.load()) {
			client->poll();
			OS::get_singleton()->delay_usec(1000);
		}
	};
	wait_while(HTTPClient::STATUS_CONNECTING, HTTPClient::STATUS_RESOLVING);
	deadline = OS::get_singleton()->get_ticks_msec() + 30000 + uint64_t(p_body.size()) / 100;
	switch (client->get_status()) {
		case HTTPClient::STATUS_CONNECTED:
			break;
		case HTTPClient::STATUS_CANT_RESOLVE:
			return UPLOAD_CANT_RESOLVE;
		case HTTPClient::STATUS_TLS_HANDSHAKE_ERROR:
			return UPLOAD_TLS_HANDSHAKE;
		default:
			return UPLOAD_CANT_CONNECT;
	}
	Vector<String> headers;
	headers.push_back("Content-Type: " + p_content_type);
	headers.push_back("User-Agent: FellowEngine-CrashReporter/1");
	if (client->request(HTTPClient::METHOD_POST, path, headers, p_body.ptr(), p_body.size()) != OK) {
		return UPLOAD_NO_RESPONSE;
	}
	wait_while(HTTPClient::STATUS_REQUESTING, HTTPClient::STATUS_REQUESTING);
	if (!client->has_response()) {
		return UPLOAD_NO_RESPONSE;
	}
	const int code = client->get_response_code();
	while (client->get_status() == HTTPClient::STATUS_BODY && OS::get_singleton()->get_ticks_msec() < deadline && !p_cancel.load()) {
		client->poll();
		client->read_response_body_chunk();
		OS::get_singleton()->delay_usec(500);
	}
	client->close();
	return code;
}

} // namespace

Error FellowCrashReporter::upload_reports(const String &p_url) {
	ERR_FAIL_COND_V_MSG(!enabled, ERR_UNCONFIGURED, "The crash reports are off (debug/fellow/crash_reports/enabled).");
	const String url = p_url.is_empty() ? String(GLOBAL_GET("debug/fellow/crash_reports/upload_url")) : p_url;
	ERR_FAIL_COND_V_MSG(url.is_empty(), ERR_INVALID_PARAMETER, "No URL to upload the reports to (debug/fellow/crash_reports/upload_url).");
	ERR_FAIL_COND_V_MSG(!url.begins_with("http://") && !url.begins_with("https://"), ERR_INVALID_PARAMETER, vformat("The reports are uploaded over HTTP or HTTPS, not to \"%s\".", url));
	ERR_FAIL_COND_V_MSG(uploading.load(), ERR_BUSY, "The reports are being uploaded already.");
	if (upload_thread) {
		upload_thread->wait_to_finish();
		memdelete(upload_thread);
		upload_thread = nullptr;
	}
	// Oldest first.
	upload_queue.clear();
	const PackedStringArray reports = get_reports();
	for (int i = reports.size() - 1; i >= 0; i--) {
		upload_queue.push_back(reports[i]);
	}
	upload_target = url;
	upload_cancel.store(false);
	uploading.store(true);
	upload_thread = memnew(Thread);
	upload_thread->start(&FellowCrashReporter::_upload_thread, this);
	return OK;
}

void FellowCrashReporter::auto_upload() {
	if (auto_upload_done || !enabled || !upload_automatically) {
		return;
	}
	auto_upload_done = true;
	if (!String(GLOBAL_GET("debug/fellow/crash_reports/upload_url")).is_empty() && !get_reports().is_empty()) {
		upload_reports();
	}
}

// Each report a POST of multipart/form-data — "report" (its JSON), "upload_file_minidump" (its minidump, the field
// Sentry's, BugSplat's and Backtrace's minidump endpoints read) and its kind, reason, project, version and commit as
// fields — moved into sent/ once the collector answers 2xx.
void FellowCrashReporter::_upload_thread(void *p_self) {
	FellowCrashReporter *self = static_cast<FellowCrashReporter *>(p_self);
	int sent = 0;
	int failed = 0;
	const String sent_dir = self->directory + "sent/";
	int unreachable = 0; // the collector not reached (its host, no connection, the TLS handshake): the rest not tried
	for (const String &path : self->upload_queue) {
		if (unreachable != 0) {
			failed++;
			callable_mp(self, &FellowCrashReporter::_emit_uploaded).call_deferred(path, false, unreachable);
			continue;
		}
		if (self->upload_cancel.load()) {
			break;
		}
		const PackedByteArray json = FileAccess::get_file_as_bytes(path);
		if (json.is_empty()) {
			failed++;
			continue;
		}
		const Variant parsed = JSON::parse_string(String::utf8((const char *)json.ptr(), json.size()));
		const Dictionary report = parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
		const Dictionary context = report.get("context", Dictionary());
		const String boundary = "FellowCrashReport" + String::num_uint64(OS::get_singleton()->get_ticks_usec(), 16);
		PackedByteArray body;
		const String fields[5][2] = { { "kind", report.get("kind", "") }, { "reason", report.get("reason", "") }, { "project", context.get("project", "") }, { "version", context.get("project_version", "") }, { "commit", context.get("commit", "") } };
		for (const auto &f : fields) {
			const CharString v = f[1].utf8();
			_part(body, boundary, f[0], String(), String(), (const uint8_t *)v.get_data(), v.length());
		}
		_part(body, boundary, "report", path.get_file(), "application/json", json.ptr(), json.size());
		const String dump = path.get_basename() + ".dmp";
		const bool has_dump = FileAccess::exists(dump);
		if (has_dump && self->upload_minidumps) {
			const PackedByteArray dmp = FileAccess::get_file_as_bytes(dump);
			_part(body, boundary, "upload_file_minidump", dump.get_file(), "application/octet-stream", dmp.ptr(), dmp.size());
		}
		const CharString tail = ("--" + boundary + "--\r\n").utf8();
		_append_bytes(body, (const uint8_t *)tail.get_data(), tail.length());
		const int code = _post(self->upload_target, "multipart/form-data; boundary=" + boundary, body, self->upload_cancel);
		const bool ok = code >= 200 && code < 300;
		if (ok) {
			DirAccess::make_dir_recursive_absolute(sent_dir);
			DirAccess::rename_absolute(path, sent_dir + path.get_file());
			if (has_dump) {
				DirAccess::rename_absolute(dump, sent_dir + dump.get_file());
			}
			sent++;
		} else {
			failed++;
			if (code == UPLOAD_CANT_RESOLVE || code == UPLOAD_CANT_CONNECT || code == UPLOAD_TLS_HANDSHAKE) {
				unreachable = code;
			}
		}
		callable_mp(self, &FellowCrashReporter::_emit_uploaded).call_deferred(path, ok, code);
	}
	callable_mp(self, &FellowCrashReporter::_upload_done).call_deferred(sent, failed);
	self->uploading.store(false);
}

void FellowCrashReporter::_emit_uploaded(const String &p_path, bool p_ok, int p_code) {
	emit_signal(SNAME("report_uploaded"), p_path, p_ok, p_code);
}

void FellowCrashReporter::_upload_done(int p_sent, int p_failed) {
	_prune_reports(directory + "sent/");
	emit_signal(SNAME("uploads_finished"), p_sent, p_failed);
}

void FellowCrashReporter::debug_crash(const String &p_kind) {
	print_line(vformat("FellowCrashReporter: crashing on purpose (%s).", p_kind));
	if (p_kind == "segfault") {
		volatile int *volatile p = nullptr; // both volatile: GCC drops a plain store through a null pointer
		*p = 42;
	} else if (p_kind == "abort") {
		abort();
	} else if (p_kind == "trap") {
		CRASH_NOW_MSG("FellowCrashReporter.debug_crash(\"trap\")");
	} else if (p_kind == "stack_overflow") {
		_overflow(0);
	} else if (p_kind == "thread") {
		Thread t;
		t.start(&_segfault_on_thread, nullptr);
		t.wait_to_finish();
	} else if (p_kind == "divide") {
		volatile int a = 1;
		volatile int b = 0;
		volatile int c = a / b;
		(void)c;
	} else {
		ERR_FAIL_MSG(vformat("Unknown crash kind \"%s\" (segfault, abort, trap, stack_overflow, thread, divide).", p_kind));
	}
}

Dictionary FellowCrashReporter::get_stats() const {
	Dictionary d;
	d["enabled"] = enabled;
	d["installed"] = g.installed.load();
	const uint32_t cap = g.log_capacity.load();
	const uint32_t head = g.log_head.load();
	d["log_capacity"] = cap;
	d["log_lines_seen"] = head;
	d["log_lines_kept"] = cap ? MIN(head, cap) : 0;
	d["breadcrumbs"] = MIN(g.crumb_head.load(), uint32_t(CRUMB_MAX));
	d["reports_written"] = g.reports_written.load();
	d["setup_usec"] = setup_usec;
	d["watchdog"] = g_watch_thread != nullptr;
	d["hang_ms"] = g_hang_ms;
	d["hangs"] = g_hangs.load();
#if defined(FELLOW_CRASH_POSIX)
	d["hang_signal"] = g_hang_signal;
#endif
	d["frame"] = g.frame.load();
	d["scene"] = String::utf8(g.scene);
	d["context"] = JSON::parse_string(String::utf8(g.context[g.context_index.load() & 1]));
	return d;
}

void FellowCrashReporter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_enabled"), &FellowCrashReporter::is_enabled);
	ClassDB::bind_method(D_METHOD("get_report_directory"), &FellowCrashReporter::get_report_directory);
	ClassDB::bind_method(D_METHOD("add_breadcrumb", "message", "category"), &FellowCrashReporter::add_breadcrumb, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("set_custom_value", "key", "value"), &FellowCrashReporter::set_custom_value);
	ClassDB::bind_method(D_METHOD("remove_custom_value", "key"), &FellowCrashReporter::remove_custom_value);
	ClassDB::bind_method(D_METHOD("get_custom_values"), &FellowCrashReporter::get_custom_values);
	ClassDB::bind_method(D_METHOD("write_report", "reason"), &FellowCrashReporter::write_report);
	ClassDB::bind_method(D_METHOD("get_reports"), &FellowCrashReporter::get_reports);
	ClassDB::bind_method(D_METHOD("get_pending_report"), &FellowCrashReporter::get_pending_report);
	ClassDB::bind_method(D_METHOD("acknowledge_reports"), &FellowCrashReporter::acknowledge_reports);
	ClassDB::bind_static_method("FellowCrashReporter", D_METHOD("read_report", "path"), &FellowCrashReporter::read_report);
	ClassDB::bind_method(D_METHOD("upload_reports", "url"), &FellowCrashReporter::upload_reports, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("is_uploading"), &FellowCrashReporter::is_uploading);
	ClassDB::bind_method(D_METHOD("debug_crash", "kind"), &FellowCrashReporter::debug_crash);
	ADD_SIGNAL(MethodInfo("report_uploaded", PropertyInfo(Variant::STRING, "path"), PropertyInfo(Variant::BOOL, "ok"), PropertyInfo(Variant::INT, "response_code")));
	ADD_SIGNAL(MethodInfo("uploads_finished", PropertyInfo(Variant::INT, "sent"), PropertyInfo(Variant::INT, "failed")));
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowCrashReporter::get_stats);
}
