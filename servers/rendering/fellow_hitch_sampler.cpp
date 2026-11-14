/**************************************************************************/
/*  fellow_hitch_sampler.cpp                                              */
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

#include "fellow_hitch_sampler.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/math/math_funcs.h"
#include "core/os/memory.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/string/print_string.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/templates/local_vector.h"
#include "core/templates/sort_array.h"

#include <atomic>
#include <cstring>

#if defined(WINDOWS_ENABLED) && (defined(_M_X64) || defined(__x86_64__))
#define FELLOW_HITCH_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dbghelp.h>
#include <tlhelp32.h>
#ifdef _MSC_VER
#include <intrin.h>
#pragma comment(lib, "dbghelp.lib") // the platform links it only for its crash handler's builds
#else
#include <x86intrin.h>
#endif
#elif defined(__linux__) && defined(__GLIBC__)
#define FELLOW_HITCH_LINUX
#include <cxxabi.h>
#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <link.h>
#include <signal.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#endif

#if defined(FELLOW_HITCH_WINDOWS) || defined(FELLOW_HITCH_LINUX)
#define FELLOW_HITCH_SUPPORTED
#endif

namespace {
std::atomic<uint64_t> hitch_count{ 0 };
std::atomic<uint64_t> sample_count{ 0 };
} // namespace

#ifdef FELLOW_HITCH_SUPPORTED

namespace {

constexpr uint32_t MAX_DEPTH = 64;
constexpr uint32_t MAX_SAMPLES = 256; // the stepping thread's, a hitch (about a quarter of a second)
constexpr uint32_t MAX_THREADS = 128; // other threads followed during a hitch
constexpr uint32_t MAX_OTHER_STACKS = 2048; // their samples, a hitch
constexpr uint32_t OTHERS_EVERY = 4; // the other threads sampled every fourth sample of the stepping thread
constexpr uint32_t MAX_HITCHES = 64;
constexpr uint32_t DURATION_RING = 16;
constexpr uint32_t NAME_SIZE = 64;

struct Stack {
	uint32_t depth = 0;
	uint64_t pc[MAX_DEPTH]; // [0] where the thread was, then return addresses outwards
};

struct NativeThread {
	uint64_t os_id = 0;
#ifdef FELLOW_HITCH_WINDOWS
	HANDLE handle = nullptr;
#endif
};

enum SlotMode {
	SLOT_UNREGISTERED,
	SLOT_REGISTERED,
	SLOT_IGNORED, // the draw slot on the main thread: the main slot covers it
};

struct SlotState {
	std::atomic<int> mode{ SLOT_UNREGISTERED };
	std::atomic<uint64_t> begin_usec{ 0 }; // 0 between steps
	std::atomic<uint64_t> seq{ 0 };
	std::atomic<uint64_t> frame{ 0 };
	std::atomic<uint64_t> ring_seq[DURATION_RING];
	std::atomic<uint64_t> ring_usec[DURATION_RING];
	NativeThread thread;
};

struct OtherThread {
	NativeThread thread;
	char name[NAME_SIZE];
	uint64_t cpu_first = 0;
	uint64_t cpu_last = 0;
	uint64_t t_first = 0;
	uint64_t t_last = 0;
	uint32_t cpu_reads = 0;
};

struct OtherSample {
	uint32_t thread = 0;
	Stack stack;
};

struct Capture {
	bool active = false;
	uint64_t seq = 0;
	uint64_t step_begin = 0;
	uint64_t frame = 0;
	uint64_t first_usec = 0;
	uint64_t last_usec = 0;
	uint64_t cpu_first = 0;
	uint64_t cpu_last = 0;
	uint32_t cpu_reads = 0;
	uint32_t samples = 0;
	uint32_t ticks = 0;
	Stack *stacks = nullptr; // MAX_SAMPLES
	bool threads_listed = false;
	uint32_t thread_count = 0;
	OtherThread *threads = nullptr; // MAX_THREADS
	uint32_t other_count = 0;
	OtherSample *others = nullptr; // MAX_OTHER_STACKS
};

struct HitchOther {
	String name;
	uint64_t os_id = 0;
	double cpu_share = -1.0;
	LocalVector<Stack> stacks;
};

struct Hitch {
	uint32_t slot = 0;
	uint64_t frame = 0;
	uint64_t step_usec = 0;
	bool step_known = true;
	uint64_t at_usec = 0; // the step's start, from the sampler's start
	uint64_t first_offset_usec = 0; // the first sample, from the step's start
	uint64_t last_offset_usec = 0;
	double cpu_share = -1.0; // of the time between the first and the last sample
	LocalVector<Stack> stacks;
	LocalVector<HitchOther> others;
};

std::atomic<int> sampler_state{ 0 }; // 0 not started, 1 running, -1 off
SlotState slots[FellowHitchSampler::SLOT_MAX];
Capture captures[FellowHitchSampler::SLOT_MAX];
Thread watchdog_thread;
std::atomic<bool> exit_requested{ false };
uint64_t watchdog_os_id = 0;
uint64_t start_threshold_usec = 0;
uint64_t report_threshold_usec = 0;
uint64_t sampler_start_usec = 0;
LocalVector<Hitch> hitches; // the watchdog's until it has stopped
uint32_t hitches_dropped = 0;
uint64_t steps_sampled = 0; // captures started (steps past half the threshold)
double cpu_units_per_usec = 1.0;

const char *slot_name(uint32_t p_slot) {
	return p_slot == FellowHitchSampler::SLOT_MAIN ? "the main thread's frame step" : "the render thread's draw";
}

/* Platforms: the current thread's id and a handle the watchdog can sample it by; a thread's CPU time; a thread's stack;
 * the process's threads; the names of addresses. */

#ifdef FELLOW_HITCH_WINDOWS

uint8_t *stack_copy = nullptr;
constexpr uint64_t STACK_COPY_SIZE = 1024 * 1024;

typedef HRESULT(WINAPI *GetThreadDescriptionFn)(HANDLE, PWSTR *);
GetThreadDescriptionFn get_thread_description = nullptr;

bool platform_init() {
	stack_copy = (uint8_t *)memalloc(STACK_COPY_SIZE);
	HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
	if (kernel) {
		get_thread_description = (GetThreadDescriptionFn)(void *)GetProcAddress(kernel, "GetThreadDescription");
	}
	// QueryThreadCycleTime() counts the time-stamp counter's cycles: measure its rate.
	LARGE_INTEGER freq, q0, q1;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&q0);
	const uint64_t c0 = __rdtsc();
	Sleep(20);
	const uint64_t c1 = __rdtsc();
	QueryPerformanceCounter(&q1);
	const double usec = double(q1.QuadPart - q0.QuadPart) * 1000000.0 / double(freq.QuadPart);
	cpu_units_per_usec = usec > 0.0 ? double(c1 - c0) / usec : 1.0;
	return true;
}

void platform_finish() {
	if (stack_copy) {
		memfree(stack_copy);
		stack_copy = nullptr;
	}
}

uint64_t current_os_id() {
	return GetCurrentThreadId();
}

bool register_current(NativeThread &r_thread) {
	HANDLE handle = nullptr;
	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
				THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
		return false;
	}
	r_thread.handle = handle;
	r_thread.os_id = GetCurrentThreadId();
	return true;
}

void close_thread(NativeThread &r_thread) {
	if (r_thread.handle) {
		CloseHandle(r_thread.handle);
		r_thread.handle = nullptr;
	}
}

bool thread_cpu(const NativeThread &p_thread, uint64_t &r_units) {
	ULONG64 cycles = 0;
	if (!p_thread.handle || !QueryThreadCycleTime(p_thread.handle, &cycles)) {
		return false;
	}
	r_units = cycles;
	return true;
}

// Unwinds a copied stack from its registers with the executable images' unwind tables (no locks are taken while the
// thread is suspended: it runs again before this). Plain data only, for the structured exception guard.
uint32_t unwind_copy(CONTEXT *p_context, uint64_t p_lo, uint64_t p_hi, uint64_t *r_pc, uint32_t p_max) {
	uint32_t depth = 0;
#ifdef _MSC_VER
	__try {
#endif
		while (depth < p_max) {
			const DWORD64 pc = p_context->Rip;
			if (pc == 0) {
				break;
			}
			r_pc[depth++] = pc;
			if (p_context->Rsp < p_lo || p_context->Rsp + 8 > p_hi) {
				break;
			}
			DWORD64 image_base = 0;
			PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image_base, nullptr);
			if (function) {
				PVOID handler_data = nullptr;
				DWORD64 establisher_frame = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, function, p_context, &handler_data, &establisher_frame, nullptr);
			} else {
				// A leaf function: the return address is on the top of the stack.
				p_context->Rip = *(const DWORD64 *)p_context->Rsp;
				p_context->Rsp += 8;
			}
		}
#ifdef _MSC_VER
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
#endif
	return depth;
}

bool sample_thread(const NativeThread &p_thread, Stack &r_stack) {
	r_stack.depth = 0;
	if (!p_thread.handle || !stack_copy) {
		return false;
	}
	if (SuspendThread(p_thread.handle) == (DWORD)-1) {
		return false;
	}
	// While the thread is suspended: its registers and a copy of its stack, nothing that could take a lock it holds.
	CONTEXT context;
	memset(&context, 0, sizeof(context));
	context.ContextFlags = CONTEXT_FULL;
	bool ok = GetThreadContext(p_thread.handle, &context) != 0; // also waits for the suspension
	uint64_t lo = 0;
	uint64_t size = 0;
	if (ok) {
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery((LPCVOID)context.Rsp, &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT) {
			lo = context.Rsp;
			const uint64_t top = uint64_t(info.BaseAddress) + uint64_t(info.RegionSize);
			size = MIN(top - lo, STACK_COPY_SIZE) & ~uint64_t(7);
			memcpy(stack_copy, (const void *)lo, size);
		} else {
			ok = false;
		}
	}
	ResumeThread(p_thread.handle);
	if (!ok || size == 0) {
		return false;
	}

	// Pointers into the stack (in the registers and on the stack itself) moved to the copy.
	const uint64_t hi = lo + size;
	const uint64_t copy_lo = uint64_t(stack_copy);
	const uint64_t delta = copy_lo - lo;
	DWORD64 *registers[] = { &context.Rax, &context.Rbx, &context.Rcx, &context.Rdx, &context.Rsi, &context.Rdi,
		&context.Rbp, &context.Rsp, &context.R8, &context.R9, &context.R10, &context.R11, &context.R12, &context.R13,
		&context.R14, &context.R15 };
	for (DWORD64 *reg : registers) {
		if (*reg >= lo && *reg < hi) {
			*reg += delta;
		}
	}
	uint64_t *words = (uint64_t *)stack_copy;
	for (uint64_t i = 0; i < size / 8; i++) {
		if (words[i] >= lo && words[i] < hi) {
			words[i] += delta;
		}
	}
	r_stack.depth = unwind_copy(&context, copy_lo, copy_lo + size, r_stack.pc, MAX_DEPTH);
	return r_stack.depth > 0;
}

void thread_name(HANDLE p_handle, char *r_name) {
	r_name[0] = 0;
	if (!get_thread_description || !p_handle) {
		return;
	}
	PWSTR description = nullptr;
	if (SUCCEEDED(get_thread_description(p_handle, &description)) && description) {
		WideCharToMultiByte(CP_UTF8, 0, description, -1, r_name, NAME_SIZE, nullptr, nullptr);
		r_name[NAME_SIZE - 1] = 0;
		LocalFree(description);
	}
}

void list_threads(Capture &r_capture, const uint64_t *p_skip, uint32_t p_skip_count) {
	r_capture.thread_count = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return;
	}
	const DWORD pid = GetCurrentProcessId();
	THREADENTRY32 entry;
	entry.dwSize = sizeof(entry);
	if (Thread32First(snapshot, &entry)) {
		do {
			if (entry.th32OwnerProcessID != pid || r_capture.thread_count >= MAX_THREADS) {
				continue;
			}
			bool skip = false;
			for (uint32_t i = 0; i < p_skip_count; i++) {
				skip = skip || p_skip[i] == entry.th32ThreadID;
			}
			if (skip) {
				continue;
			}
			HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
			if (!handle) {
				continue;
			}
			OtherThread &other = r_capture.threads[r_capture.thread_count++];
			other = OtherThread();
			other.thread.handle = handle;
			other.thread.os_id = entry.th32ThreadID;
			thread_name(handle, other.name);
		} while (Thread32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);
}

// A linker map's functions (linkflags=/MAP): their addresses relative to the image and decorated names.
struct MapSymbols {
	Vector<uint8_t> text;
	struct Entry {
		uint32_t rva = 0;
		uint32_t name = 0; // offset in text, zero-terminated
		bool operator<(const Entry &p_other) const { return rva < p_other.rva; }
	};
	LocalVector<Entry> entries;

	static bool parse_hex(const uint8_t *p_from, const uint8_t *p_to, uint64_t &r_value) {
		r_value = 0;
		if (p_from >= p_to) {
			return false;
		}
		for (const uint8_t *c = p_from; c < p_to; c++) {
			const uint8_t ch = *c;
			uint32_t digit;
			if (ch >= '0' && ch <= '9') {
				digit = ch - '0';
			} else if (ch >= 'a' && ch <= 'f') {
				digit = ch - 'a' + 10;
			} else if (ch >= 'A' && ch <= 'F') {
				digit = ch - 'A' + 10;
			} else {
				return false;
			}
			r_value = (r_value << 4) | digit;
		}
		return true;
	}

	bool load(const String &p_path) {
		if (!FileAccess::exists(p_path)) {
			return false;
		}
		text = FileAccess::get_file_as_bytes(p_path);
		if (text.is_empty()) {
			return false;
		}
		uint8_t *data = text.ptrw();
		const int64_t size = text.size();
		uint64_t preferred_base = 0x140000000ull;
		int64_t line = 0;
		while (line < size) {
			int64_t end = line;
			while (end < size && data[end] != '\n') {
				end++;
			}
			// Tokens: "0001:00000000  ?name@@...  0000000140001000 f  lib:object.obj"
			const uint8_t *tokens[5];
			const uint8_t *token_ends[5];
			int count = 0;
			int64_t i = line;
			while (i < end && count < 5) {
				while (i < end && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r')) {
					i++;
				}
				if (i >= end) {
					break;
				}
				tokens[count] = data + i;
				while (i < end && data[i] != ' ' && data[i] != '\t' && data[i] != '\r') {
					i++;
				}
				token_ends[count] = data + i;
				count++;
			}
			if (count >= 4 && token_ends[0] - tokens[0] == 13 && tokens[0][4] == ':' && token_ends[2] - tokens[2] == 16 &&
					token_ends[3] - tokens[3] == 1 && tokens[3][0] == 'f') {
				uint64_t address = 0;
				if (parse_hex(tokens[2], token_ends[2], address) && address >= preferred_base) {
					Entry entry;
					entry.rva = uint32_t(address - preferred_base);
					entry.name = uint32_t(tokens[1] - data);
					data[token_ends[1] - data] = 0; // the name terminated in place
					entries.push_back(entry);
				}
			} else if (count >= 5 && memcmp(tokens[0], "Preferred", 9) == 0) {
				uint64_t base = 0;
				if (parse_hex(tokens[4], token_ends[4], base) && base != 0) {
					preferred_base = base;
				}
			}
			line = end + 1;
		}
		entries.sort();
		return !entries.is_empty();
	}

	String lookup(uint32_t p_rva) const {
		if (entries.is_empty() || p_rva < entries[0].rva) {
			return String();
		}
		uint32_t lo = 0;
		uint32_t hi = entries.size();
		while (hi - lo > 1) {
			const uint32_t mid = (lo + hi) / 2;
			if (entries[mid].rva <= p_rva) {
				lo = mid;
			} else {
				hi = mid;
			}
		}
		const char *decorated = (const char *)text.ptr() + entries[lo].name;
		char undecorated[1024];
		if (decorated[0] == '?' && UnDecorateSymbolName(decorated, undecorated, sizeof(undecorated), UNDNAME_NAME_ONLY)) {
			return String::utf8(undecorated);
		}
		return String::utf8(decorated);
	}
};

void symbolize(const LocalVector<uint64_t> &p_addresses, HashMap<uint64_t, String> &r_names) {
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
	const String exe_path = OS::get_singleton()->get_executable_path();
	const String search = exe_path.get_base_dir().replace("/", "\\");
	const bool symbols = SymInitialize(process, search.utf8().get_data(), TRUE) != 0;
	HMODULE exe_module = GetModuleHandleW(nullptr);
	bool exe_pdb = false;
	if (symbols) {
		IMAGEHLP_MODULE64 info;
		memset(&info, 0, sizeof(info));
		info.SizeOfStruct = sizeof(info);
		if (SymGetModuleInfo64(process, (DWORD64)exe_module, &info)) {
			exe_pdb = info.SymType == SymPdb;
		}
	}
	MapSymbols map;
	bool map_loaded = false;
	if (!exe_pdb) {
		map_loaded = map.load(exe_path.get_basename() + ".map");
	}
	print_line(vformat("FellowEngine hitch sampler: names from %s.", exe_pdb ? "the executable's PDB" : (map_loaded ? "the executable's linker map" : "exports only (no PDB or .map next to the executable)")));

	alignas(8) uint8_t symbol_buffer[sizeof(SYMBOL_INFO) + 1024];
	for (uint64_t address : p_addresses) {
		HMODULE module = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(uintptr_t)address, &module);
		String module_name = "?";
		if (module) {
			wchar_t path[MAX_PATH];
			const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
			if (length > 0) {
				module_name = String::utf16((const char16_t *)path, length).replace("\\", "/").get_file();
			}
		}
		String name;
		if (module == exe_module && !exe_pdb) {
			if (map_loaded) {
				name = map.lookup(uint32_t(address - uint64_t(module)));
			}
		} else if (symbols) {
			SYMBOL_INFO *symbol = (SYMBOL_INFO *)symbol_buffer;
			memset(symbol, 0, sizeof(SYMBOL_INFO));
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 1024;
			DWORD64 displacement = 0;
			if (SymFromAddr(process, address, &displacement, symbol)) {
				name = String::utf8(symbol->Name);
				if ((symbol->Flags & SYMFLAG_EXPORT) && displacement > 0x400) {
					name += "+0x" + String::num_uint64(displacement, 16); // the nearest export: a hint only
				}
			}
		}
		if (name.is_empty()) {
			name = module_name + "+0x" + String::num_uint64(module ? address - uint64_t(module) : address, 16);
		} else if (module != exe_module) {
			name = module_name.get_basename() + "!" + name;
		}
		r_names.insert(address, name);
	}
	if (symbols) {
		SymCleanup(process);
	}
}

#elif defined(FELLOW_HITCH_LINUX)

int hitch_signal = 0;
std::atomic<int64_t> signal_target{ 0 };
std::atomic<int> signal_done{ 0 };
void *signal_frames[MAX_DEPTH + 8];
std::atomic<int> signal_count{ 0 };

void signal_handler(int, siginfo_t *, void *) {
	const int saved_errno = errno;
	if ((int64_t)syscall(SYS_gettid) == signal_target.load(std::memory_order_acquire)) {
		const int count = backtrace(signal_frames, MAX_DEPTH + 8);
		signal_count.store(count, std::memory_order_relaxed);
		signal_done.store(1, std::memory_order_release);
	}
	errno = saved_errno;
}

bool platform_init() {
	void *prime[4];
	backtrace(prime, 4); // loads the unwinder outside a signal handler
	hitch_signal = SIGRTMIN + 7;
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_sigaction = signal_handler;
	action.sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&action.sa_mask);
	if (sigaction(hitch_signal, &action, nullptr) != 0) {
		return false;
	}
	cpu_units_per_usec = 1000.0; // nanoseconds
	return true;
}

void platform_finish() {
}

uint64_t current_os_id() {
	return (uint64_t)syscall(SYS_gettid);
}

bool register_current(NativeThread &r_thread) {
	r_thread.os_id = current_os_id();
	return true;
}

void close_thread(NativeThread &r_thread) {
	r_thread.os_id = r_thread.os_id; // nothing to close
}

bool thread_cpu(const NativeThread &p_thread, uint64_t &r_units) {
	// The thread's scheduler CPU clock (MAKE_THREAD_CPUCLOCK(tid, CPUCLOCK_SCHED)).
	const clockid_t clock = clockid_t((~uint32_t(p_thread.os_id) << 3) | 6u);
	struct timespec ts;
	if (clock_gettime(clock, &ts) != 0) {
		return false;
	}
	r_units = uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
	return true;
}

bool sample_thread(const NativeThread &p_thread, Stack &r_stack) {
	r_stack.depth = 0;
	signal_done.store(0, std::memory_order_relaxed);
	signal_target.store((int64_t)p_thread.os_id, std::memory_order_release);
	if (syscall(SYS_tgkill, getpid(), (pid_t)p_thread.os_id, hitch_signal) != 0) {
		signal_target.store(0, std::memory_order_release);
		return false;
	}
	for (int i = 0; i < 2000 && !signal_done.load(std::memory_order_acquire); i++) {
		usleep(10);
	}
	signal_target.store(0, std::memory_order_release);
	if (!signal_done.load(std::memory_order_acquire)) {
		return false;
	}
	// [0] the handler, [1] the signal trampoline, [2] where the thread was.
	const int count = signal_count.load(std::memory_order_relaxed);
	for (int i = 2; i < count && r_stack.depth < MAX_DEPTH; i++) {
		r_stack.pc[r_stack.depth++] = (uint64_t)signal_frames[i];
	}
	return r_stack.depth > 0;
}

void list_threads(Capture &r_capture, const uint64_t *p_skip, uint32_t p_skip_count) {
	r_capture.thread_count = 0;
	DIR *dir = opendir("/proc/self/task");
	if (!dir) {
		return;
	}
	while (struct dirent *entry = readdir(dir)) {
		if (entry->d_name[0] < '0' || entry->d_name[0] > '9' || r_capture.thread_count >= MAX_THREADS) {
			continue;
		}
		const uint64_t tid = strtoull(entry->d_name, nullptr, 10);
		bool skip = false;
		for (uint32_t i = 0; i < p_skip_count; i++) {
			skip = skip || p_skip[i] == tid;
		}
		if (skip) {
			continue;
		}
		OtherThread &other = r_capture.threads[r_capture.thread_count++];
		other = OtherThread();
		other.thread.os_id = tid;
		other.name[0] = 0;
		char path[64];
		snprintf(path, sizeof(path), "/proc/self/task/%llu/comm", (unsigned long long)tid);
		if (FILE *f = fopen(path, "r")) {
			if (fgets(other.name, NAME_SIZE, f)) {
				const size_t length = strlen(other.name);
				if (length > 0 && other.name[length - 1] == '\n') {
					other.name[length - 1] = 0;
				}
			}
			fclose(f);
		}
	}
	closedir(dir);
}

void symbolize(const LocalVector<uint64_t> &p_addresses, HashMap<uint64_t, String> &r_names) {
	Dl_info self;
	void *exe_base = nullptr;
	if (dladdr((void *)&FellowHitchSampler::finish, &self)) {
		exe_base = self.dli_fbase;
	}
	const uintptr_t relocation = _r_debug.r_map->l_addr; // a PIE executable's
	List<String> args;
	LocalVector<uint64_t> exe_addresses;
	for (uint64_t address : p_addresses) {
		Dl_info info;
		String name;
		if (dladdr((void *)address, &info)) {
			if (info.dli_fbase == exe_base) {
				exe_addresses.push_back(address);
				args.push_back("0x" + String::num_uint64(address - relocation, 16));
				continue;
			}
			const String module = String::utf8(info.dli_fname ? info.dli_fname : "?").get_file();
			if (info.dli_sname) {
				int status = 0;
				char *demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
				name = module + "!" + String::utf8(status == 0 && demangled ? demangled : info.dli_sname);
				if (demangled) {
					free(demangled);
				}
			} else {
				name = module + "+0x" + String::num_uint64(address - uint64_t(info.dli_fbase), 16);
			}
		} else {
			name = "0x" + String::num_uint64(address, 16);
		}
		r_names.insert(address, name);
	}
	if (exe_addresses.is_empty()) {
		return;
	}
	List<String> exec_args;
	exec_args.push_back("-f");
	exec_args.push_back("-C");
	exec_args.push_back("-e");
	exec_args.push_back(OS::get_singleton()->get_executable_path());
	for (const String &a : args) {
		exec_args.push_back(a);
	}
	String output;
	int exit_code = 0;
	Vector<String> lines;
	if (OS::get_singleton()->execute("addr2line", exec_args, &output, &exit_code) == OK && exit_code == 0) {
		lines = output.split("\n");
	}
	print_line(vformat("FellowEngine hitch sampler: names from %s.", lines.size() >= int(exe_addresses.size() * 2) ? "addr2line (the executable's symbol table: FELLOW_KEEP_SYMBOLS=1 at link)" : "nothing (addr2line failed)"));
	for (uint32_t i = 0; i < exe_addresses.size(); i++) {
		String name;
		if (int(i * 2) < lines.size()) {
			name = lines[i * 2].strip_edges();
		}
		if (name.is_empty() || name == "??") {
			name = OS::get_singleton()->get_executable_path().get_file() + "+0x" + String::num_uint64(exe_addresses[i] - relocation, 16);
		}
		r_names.insert(exe_addresses[i], name);
	}
}

#endif

void capture_reset(Capture &r_capture) {
	for (uint32_t i = 0; i < r_capture.thread_count; i++) {
		close_thread(r_capture.threads[i].thread);
	}
	r_capture.active = false;
	r_capture.samples = 0;
	r_capture.ticks = 0;
	r_capture.cpu_reads = 0;
	r_capture.threads_listed = false;
	r_capture.thread_count = 0;
	r_capture.other_count = 0;
}

void capture_finish(uint32_t p_slot, Capture &r_capture) {
	SlotState &slot = slots[p_slot];
	const uint32_t ring = r_capture.seq % DURATION_RING;
	uint64_t duration = r_capture.last_usec - r_capture.step_begin;
	bool known = false;
	if (slot.ring_seq[ring].load(std::memory_order_acquire) == r_capture.seq) {
		duration = slot.ring_usec[ring].load(std::memory_order_relaxed);
		known = true;
	}
	if (duration >= report_threshold_usec && r_capture.samples > 0) {
		if (hitches.size() < MAX_HITCHES) {
			Hitch hitch;
			hitch.slot = p_slot;
			hitch.frame = r_capture.frame;
			hitch.step_usec = duration;
			hitch.step_known = known;
			hitch.at_usec = r_capture.step_begin - sampler_start_usec;
			hitch.first_offset_usec = r_capture.first_usec - r_capture.step_begin;
			hitch.last_offset_usec = r_capture.last_usec - r_capture.step_begin;
			if (r_capture.cpu_reads >= 2 && r_capture.last_usec > r_capture.first_usec) {
				hitch.cpu_share = double(r_capture.cpu_last - r_capture.cpu_first) / cpu_units_per_usec / double(r_capture.last_usec - r_capture.first_usec);
			}
			for (uint32_t i = 0; i < r_capture.samples; i++) {
				hitch.stacks.push_back(r_capture.stacks[i]);
			}
			for (uint32_t t = 0; t < r_capture.thread_count; t++) {
				const OtherThread &other = r_capture.threads[t];
				HitchOther h;
				h.name = String::utf8(other.name);
				h.os_id = other.thread.os_id;
				if (other.cpu_reads >= 2 && other.t_last > other.t_first) {
					h.cpu_share = double(other.cpu_last - other.cpu_first) / cpu_units_per_usec / double(other.t_last - other.t_first);
				}
				for (uint32_t i = 0; i < r_capture.other_count; i++) {
					if (r_capture.others[i].thread == t) {
						h.stacks.push_back(r_capture.others[i].stack);
					}
				}
				if (!h.stacks.is_empty()) {
					hitch.others.push_back(h);
				}
			}
			hitches.push_back(hitch);
			hitch_count.fetch_add(1, std::memory_order_relaxed);
		} else {
			hitches_dropped++;
		}
	}
	capture_reset(r_capture);
}

void sample_others(uint32_t p_slot, Capture &r_capture) {
	if (!r_capture.threads_listed) {
		r_capture.threads_listed = true;
		uint64_t skip[FellowHitchSampler::SLOT_MAX + 1];
		uint32_t skip_count = 0;
		skip[skip_count++] = watchdog_os_id;
		for (uint32_t s = 0; s < FellowHitchSampler::SLOT_MAX; s++) {
			if (slots[s].mode.load(std::memory_order_acquire) == SLOT_REGISTERED) {
				skip[skip_count++] = slots[s].thread.os_id;
			}
		}
		list_threads(r_capture, skip, skip_count);
	}
	for (uint32_t t = 0; t < r_capture.thread_count; t++) {
		OtherThread &other = r_capture.threads[t];
		uint64_t cpu = 0;
		if (thread_cpu(other.thread, cpu)) {
			const uint64_t now = OS::get_singleton()->get_ticks_usec();
			if (other.cpu_reads == 0) {
				other.cpu_first = cpu;
				other.t_first = now;
			}
			other.cpu_last = cpu;
			other.t_last = now;
			other.cpu_reads++;
		}
		if (r_capture.other_count < MAX_OTHER_STACKS) {
			OtherSample &sample = r_capture.others[r_capture.other_count];
			if (sample_thread(other.thread, sample.stack)) {
				sample.thread = t;
				r_capture.other_count++;
			}
		}
	}
}

void watchdog(void *) {
	Thread::set_name("FellowEngine hitch sampler");
	watchdog_os_id = current_os_id();
	while (!exit_requested.load(std::memory_order_acquire)) {
		OS::get_singleton()->delay_usec(1000);
		for (uint32_t s = 0; s < FellowHitchSampler::SLOT_MAX; s++) {
			SlotState &slot = slots[s];
			if (slot.mode.load(std::memory_order_acquire) != SLOT_REGISTERED) {
				continue;
			}
			Capture &capture = captures[s];
			const uint64_t now = OS::get_singleton()->get_ticks_usec();
			const uint64_t begin = slot.begin_usec.load(std::memory_order_acquire);
			const uint64_t seq = slot.seq.load(std::memory_order_acquire);
			const bool long_step = begin != 0 && now > begin && now - begin >= start_threshold_usec;
			if (capture.active && (!long_step || capture.seq != seq)) {
				capture_finish(s, capture);
			}
			if (!long_step) {
				continue;
			}
			if (!capture.active) {
				capture.active = true;
				capture.seq = seq;
				capture.step_begin = begin;
				capture.frame = slot.frame.load(std::memory_order_relaxed);
				steps_sampled++;
			}
			uint64_t cpu = 0;
			const bool cpu_ok = thread_cpu(slot.thread, cpu);
			const uint64_t t = OS::get_singleton()->get_ticks_usec();
			if (capture.samples < MAX_SAMPLES && sample_thread(slot.thread, capture.stacks[capture.samples])) {
				if (capture.samples == 0) {
					capture.first_usec = t;
				}
				capture.samples++;
				sample_count.fetch_add(1, std::memory_order_relaxed);
			}
			if (capture.samples > 0) {
				capture.last_usec = t;
				if (cpu_ok) {
					if (capture.cpu_reads == 0) {
						capture.cpu_first = cpu;
						capture.first_usec = t;
					}
					capture.cpu_last = cpu;
					capture.cpu_reads++;
				}
			}
			if (capture.ticks % OTHERS_EVERY == 0) {
				sample_others(s, capture);
			}
			capture.ticks++;
		}
	}
	for (uint32_t s = 0; s < FellowHitchSampler::SLOT_MAX; s++) {
		if (captures[s].active) {
			capture_finish(s, captures[s]);
		}
	}
}

/* The report. */

struct NameTable {
	HashMap<String, int> ids;
	Vector<String> names;
	int intern(const String &p_name) {
		const int *found = ids.getptr(p_name);
		if (found) {
			return *found;
		}
		const int id = names.size();
		names.push_back(p_name);
		ids.insert(p_name, id);
		return id;
	}
};

// A stack's frames as name ids, outermost first. Return addresses (all but the innermost frame) are looked up one byte
// back, inside their call.
LocalVector<int> named_stack(const Stack &p_stack, const HashMap<uint64_t, int> &p_ids) {
	LocalVector<int> frames;
	for (int i = int(p_stack.depth) - 1; i >= 0; i--) {
		const uint64_t address = i == 0 ? p_stack.pc[0] : p_stack.pc[i] - 1;
		const int *id = p_ids.getptr(address);
		frames.push_back(id ? *id : -1);
	}
	return frames;
}

struct Count {
	int id = 0;
	uint32_t count = 0;
	bool operator<(const Count &p_other) const { return count > p_other.count || (count == p_other.count && id < p_other.id); }
};

String share(uint32_t p_count, uint32_t p_total) {
	return itos(int(Math::round(100.0 * double(p_count) / double(MAX(1u, p_total))))).lpad(3) + " %";
}

struct PathStep {
	int id = -1;
	uint32_t count = 0;
};

// The hottest call path: from the outermost frame, the most common function at each depth among the samples that
// followed the path so far, while it holds a fifth of the samples.
LocalVector<PathStep> hottest_path(const LocalVector<LocalVector<int>> &p_stacks) {
	LocalVector<PathStep> path;
	LocalVector<uint32_t> following;
	for (uint32_t i = 0; i < p_stacks.size(); i++) {
		following.push_back(i);
	}
	for (uint32_t depth = 0; depth < MAX_DEPTH; depth++) {
		HashMap<int, uint32_t> counts;
		for (uint32_t i : following) {
			if (depth < p_stacks[i].size()) {
				counts[p_stacks[i][depth]]++;
			}
		}
		PathStep best;
		for (const KeyValue<int, uint32_t> &kv : counts) {
			if (kv.value > best.count || (kv.value == best.count && kv.key < best.id)) {
				best.id = kv.key;
				best.count = kv.value;
			}
		}
		if (best.count == 0 || best.count * 5 < p_stacks.size()) {
			break;
		}
		path.push_back(best);
		LocalVector<uint32_t> next;
		for (uint32_t i : following) {
			if (depth < p_stacks[i].size() && p_stacks[i][depth] == best.id) {
				next.push_back(i);
			}
		}
		following = next;
	}
	return path;
}

String frame_name(const NameTable &p_names, int p_id) {
	return p_id >= 0 ? p_names.names[p_id] : String("?");
}

LocalVector<Count> innermost(const LocalVector<LocalVector<int>> &p_stacks) {
	HashMap<int, uint32_t> leaves;
	for (const LocalVector<int> &stack : p_stacks) {
		if (!stack.is_empty()) {
			leaves[stack[stack.size() - 1]]++;
		}
	}
	LocalVector<Count> sorted;
	for (const KeyValue<int, uint32_t> &kv : leaves) {
		Count c;
		c.id = kv.key;
		c.count = kv.value;
		sorted.push_back(c);
	}
	sorted.sort();
	return sorted;
}

void print_profile(const LocalVector<LocalVector<int>> &p_stacks, const NameTable &p_names, const String &p_indent, int p_path_lines, int p_leaves) {
	const uint32_t total = p_stacks.size();
	if (total == 0) {
		return;
	}
	if (p_path_lines > 0) {
		const LocalVector<PathStep> path = hottest_path(p_stacks);
		// The frames every sample shares at the top (the thread's start, the main loop) are cut down to the eight above the
		// deepest one they all share (the callers of a wait, or of the work).
		uint32_t shared = 0;
		while (shared + 1 < path.size() && path[shared + 1].count == total) {
			shared++;
		}
		uint32_t from = shared > 8 ? shared - 8 : 0;
		// When the path is longer than the lines, its innermost part is shown.
		if (path.size() - from > uint32_t(p_path_lines)) {
			from = path.size() - p_path_lines;
		}
		print_line(p_indent + "hottest call path (share of the samples)" + (from > 0 ? vformat(", from depth %d:", from) : String(":")));
		for (uint32_t i = from; i < path.size(); i++) {
			print_line(p_indent + "  " + share(path[i].count, total) + "  " + frame_name(p_names, path[i].id));
		}
	}
	const LocalVector<Count> leaves = innermost(p_stacks);
	String line = p_indent + "innermost:";
	for (uint32_t i = 0; i < leaves.size() && int(i) < p_leaves; i++) {
		line += (i > 0 ? "; " : " ") + share(leaves[i].count, total).strip_edges() + " " + frame_name(p_names, leaves[i].id);
	}
	print_line(line);
}

// A thread whose innermost frames are mostly a wait (a kernel wait, a futex, a sleep) is idle.
bool waits_mostly(const LocalVector<LocalVector<int>> &p_stacks, const NameTable &p_names) {
	static const char *waits[] = { "Wait", "wait", "Delay", "Sleep", "sleep", "RemoveIoCompletion", "syscall", "futex", "poll", "select", "pause", "sigtimedwait", "Alert" };
	uint32_t waiting = 0;
	for (const LocalVector<int> &stack : p_stacks) {
		bool found = false;
		for (uint32_t k = 0; k < 4 && k < stack.size() && !found; k++) {
			const String name = frame_name(p_names, stack[stack.size() - 1 - k]);
			for (const char *w : waits) {
				if (name.contains(w)) {
					found = true;
					break;
				}
			}
		}
		waiting += found ? 1 : 0;
	}
	return waiting * 2 >= p_stacks.size();
}

void report() {
	if (hitches.is_empty()) {
		print_line(vformat("FellowEngine hitch sampler: no hitch over %.1f ms (%d steps sampled past %.1f ms).", report_threshold_usec / 1000.0, (int64_t)steps_sampled, start_threshold_usec / 1000.0));
		return;
	}
	// Every address named once.
	LocalVector<uint64_t> addresses;
	HashMap<uint64_t, int> seen;
	auto add_stack = [&](const Stack &p_stack) {
		for (uint32_t i = 0; i < p_stack.depth; i++) {
			const uint64_t address = i == 0 ? p_stack.pc[0] : p_stack.pc[i] - 1;
			if (!seen.has(address)) {
				seen.insert(address, 0);
				addresses.push_back(address);
			}
		}
	};
	for (const Hitch &hitch : hitches) {
		for (const Stack &stack : hitch.stacks) {
			add_stack(stack);
		}
		for (const HitchOther &other : hitch.others) {
			for (const Stack &stack : other.stacks) {
				add_stack(stack);
			}
		}
	}
	HashMap<uint64_t, String> names;
	symbolize(addresses, names);
	NameTable table;
	HashMap<uint64_t, int> ids;
	for (const KeyValue<uint64_t, String> &kv : names) {
		ids.insert(kv.key, table.intern(kv.value));
	}

	print_line(vformat("FellowEngine hitch sampler: %d hitches over %.1f ms%s (%d steps sampled past %.1f ms).", hitches.size(), report_threshold_usec / 1000.0,
			hitches_dropped > 0 ? vformat(", %d more not kept", hitches_dropped) : String(), (int64_t)steps_sampled, start_threshold_usec / 1000.0));
	for (uint32_t h = 0; h < hitches.size(); h++) {
		const Hitch &hitch = hitches[h];
		String cpu = hitch.cpu_share >= 0.0 ? vformat("; the thread on a CPU %d %% of that time", int(Math::round(100.0 * MIN(1.0, hitch.cpu_share)))) : String();
		print_line(vformat("FellowEngine hitch %d: %s (process frame %d, %.2f s in) took %s%.1f ms; %d samples from %.1f to %.1f ms of it%s.",
				h + 1, slot_name(hitch.slot), (int64_t)hitch.frame, hitch.at_usec / 1000000.0, hitch.step_known ? "" : "at least ", hitch.step_usec / 1000.0,
				hitch.stacks.size(), hitch.first_offset_usec / 1000.0, hitch.last_offset_usec / 1000.0, cpu));
		LocalVector<LocalVector<int>> stacks;
		for (const Stack &stack : hitch.stacks) {
			stacks.push_back(named_stack(stack, ids));
		}
		print_profile(stacks, table, "  ", 40, 6);

		// The other threads: those on a CPU a fifth of the time or more, and those not waiting (a thread that holds what
		// the stepping thread waits for may not be running either), the busiest first.
		struct OtherInfo {
			uint32_t index = 0;
			double cpu = 0.0;
			LocalVector<LocalVector<int>> stacks;
		};
		LocalVector<OtherInfo> shown;
		for (uint32_t i = 0; i < hitch.others.size(); i++) {
			OtherInfo info;
			info.index = i;
			info.cpu = hitch.others[i].cpu_share;
			for (const Stack &stack : hitch.others[i].stacks) {
				info.stacks.push_back(named_stack(stack, ids));
			}
			if (info.cpu >= 0.2 || !waits_mostly(info.stacks, table)) {
				shown.push_back(info);
			}
		}
		for (uint32_t i = 0; i < shown.size(); i++) {
			for (uint32_t j = i + 1; j < shown.size(); j++) {
				if (shown[j].cpu > shown[i].cpu) {
					SWAP(shown[i], shown[j]);
				}
			}
		}
		print_line(vformat("  other threads: %d sampled, %d busy or not waiting%s", hitch.others.size(), shown.size(), shown.is_empty() ? "." : ":"));
		for (uint32_t i = 0; i < shown.size() && i < 10; i++) {
			const HitchOther &other = hitch.others[shown[i].index];
			const String cpu_text = other.cpu_share >= 0.0 ? vformat("on a CPU %d %% of the time", int(Math::round(100.0 * MIN(1.0, other.cpu_share)))) : String("its CPU time unknown");
			print_line(vformat("  thread \"%s\" (id %d): %s, %d samples", other.name, (int64_t)other.os_id, cpu_text, other.stacks.size()));
			print_profile(shown[i].stacks, table, "    ", 12, 3);
		}
	}
}

void start() {
	int expected = 0;
	if (!sampler_state.compare_exchange_strong(expected, -1)) {
		return; // started already (or off)
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (!settings || !settings->has_setting("rendering/fellow/debug/hitch_sampler") || !bool(settings->get_setting("rendering/fellow/debug/hitch_sampler"))) {
		return; // stays off
	}
	const double threshold_ms = MAX(1.0, double(settings->get_setting("rendering/fellow/debug/hitch_sampler_ms", 33.3)));
	report_threshold_usec = uint64_t(threshold_ms * 1000.0);
	start_threshold_usec = report_threshold_usec / 2;
	if (!platform_init()) {
		WARN_PRINT("FellowEngine hitch sampler: could not be set up on this system.");
		return;
	}
	for (uint32_t s = 0; s < FellowHitchSampler::SLOT_MAX; s++) {
		captures[s].stacks = (Stack *)memalloc(sizeof(Stack) * MAX_SAMPLES);
		captures[s].threads = (OtherThread *)memalloc(sizeof(OtherThread) * MAX_THREADS);
		captures[s].others = (OtherSample *)memalloc(sizeof(OtherSample) * MAX_OTHER_STACKS);
	}
	sampler_start_usec = OS::get_singleton()->get_ticks_usec();
	exit_requested.store(false, std::memory_order_release);
	Thread::Settings thread_settings;
	thread_settings.priority = Thread::PRIORITY_HIGH;
	watchdog_thread.start(watchdog, nullptr, thread_settings);
	print_line(vformat("FellowEngine hitch sampler: on (steps over %.1f ms reported when the engine exits).", threshold_ms));
	sampler_state.store(1, std::memory_order_release);
}

} // namespace

#endif // FELLOW_HITCH_SUPPORTED

void FellowHitchSampler::step_begin(Slot p_slot) {
#ifdef FELLOW_HITCH_SUPPORTED
	int state = sampler_state.load(std::memory_order_acquire);
	if (state < 0) {
		return;
	}
	if (state == 0) {
		if (p_slot != SLOT_MAIN) {
			return;
		}
		start();
		if (sampler_state.load(std::memory_order_acquire) <= 0) {
			return;
		}
	}
	SlotState &slot = slots[p_slot];
	int mode = slot.mode.load(std::memory_order_acquire);
	if (mode == SLOT_UNREGISTERED) {
		const uint64_t id = current_os_id();
		if (p_slot != SLOT_MAIN && slots[SLOT_MAIN].mode.load(std::memory_order_acquire) == SLOT_REGISTERED && slots[SLOT_MAIN].thread.os_id == id) {
			slot.mode.store(SLOT_IGNORED, std::memory_order_release);
			return;
		}
		if (!register_current(slot.thread)) {
			slot.mode.store(SLOT_IGNORED, std::memory_order_release);
			return;
		}
		mode = SLOT_REGISTERED;
		slot.mode.store(SLOT_REGISTERED, std::memory_order_release);
	}
	if (mode != SLOT_REGISTERED) {
		return;
	}
	slot.seq.fetch_add(1, std::memory_order_acq_rel);
	slot.frame.store(Engine::get_singleton()->get_process_frames(), std::memory_order_relaxed);
	slot.begin_usec.store(OS::get_singleton()->get_ticks_usec(), std::memory_order_release);
#else
	(void)p_slot;
#endif
}

void FellowHitchSampler::step_end(Slot p_slot) {
#ifdef FELLOW_HITCH_SUPPORTED
	if (sampler_state.load(std::memory_order_acquire) <= 0) {
		return;
	}
	SlotState &slot = slots[p_slot];
	if (slot.mode.load(std::memory_order_acquire) != SLOT_REGISTERED) {
		return;
	}
	const uint64_t begin = slot.begin_usec.load(std::memory_order_relaxed);
	if (begin == 0) {
		return; // ended already
	}
	const uint64_t seq = slot.seq.load(std::memory_order_relaxed);
	const uint32_t ring = seq % DURATION_RING;
	slot.ring_usec[ring].store(OS::get_singleton()->get_ticks_usec() - begin, std::memory_order_relaxed);
	slot.ring_seq[ring].store(seq, std::memory_order_release);
	slot.begin_usec.store(0, std::memory_order_release);
#else
	(void)p_slot;
#endif
}

void FellowHitchSampler::finish() {
#ifdef FELLOW_HITCH_SUPPORTED
	if (sampler_state.load(std::memory_order_acquire) != 1) {
		sampler_state.store(-1, std::memory_order_release);
		return;
	}
	exit_requested.store(true, std::memory_order_release);
	watchdog_thread.wait_to_finish();
	sampler_state.store(-1, std::memory_order_release);
	report();
	hitches.clear();
	for (uint32_t s = 0; s < SLOT_MAX; s++) {
		capture_reset(captures[s]);
		memfree(captures[s].stacks);
		memfree(captures[s].threads);
		memfree(captures[s].others);
		captures[s].stacks = nullptr;
		captures[s].threads = nullptr;
		captures[s].others = nullptr;
		if (slots[s].mode.load(std::memory_order_acquire) == SLOT_REGISTERED) {
			close_thread(slots[s].thread);
		}
	}
	platform_finish();
#endif
}

uint64_t FellowHitchSampler::get_hitch_count() {
	return hitch_count.load(std::memory_order_relaxed);
}

uint64_t FellowHitchSampler::get_sample_count() {
	return sample_count.load(std::memory_order_relaxed);
}
