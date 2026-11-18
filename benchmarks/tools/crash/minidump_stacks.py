#!/usr/bin/env python3
"""FellowEngine milestone 145: every thread's call chain from a Windows minidump (FellowEngine's crash and hang reports,
decisions 0205-0206, write one beside each report), named from the executable's linker map (`scons ...
linkflags=/MAP`). Standard library only: the minidump's documented layout read by hand.

The chains come from a scan of each thread's stack for values inside the executable's image (return addresses, and
some stale ones a stack scan cannot tell from them): not an unwind, but enough to see where each thread is.

    python3 minidump_stacks.py <report.dmp> <godot executable's .map> [--module godot] [--all]

Prints each thread whose stack holds the module's addresses (--all: every thread), innermost first."""
import argparse
import bisect
import re
import struct


def read_dump(path):
    with open(path, "rb") as f:
        data = f.read()

    def at(off, size):
        return data[off:off + size]

    sig, _ver, count, dir_rva = struct.unpack("<4sIII", at(0, 16))
    if sig != b"MDMP":
        raise SystemExit("not a minidump: %s" % path)
    streams = {}
    for i in range(count):
        kind, size, rva = struct.unpack("<III", at(dir_rva + i * 12, 12))
        streams.setdefault(kind, []).append((size, rva))

    def utf16(rva):
        n = struct.unpack("<I", at(rva, 4))[0]
        return at(rva + 4, n).decode("utf-16-le", "replace")

    modules = []
    for _size, rva in streams.get(4, []):  # ModuleListStream
        n = struct.unpack("<I", at(rva, 4))[0]
        for i in range(n):
            m = at(rva + 4 + i * 108, 108)
            base, size = struct.unpack("<QI", m[:12])
            modules.append((base, size, utf16(struct.unpack("<I", m[20:24])[0])))
    memory = []
    for _size, rva in streams.get(5, []):  # MemoryListStream
        n = struct.unpack("<I", at(rva, 4))[0]
        for i in range(n):
            start, size, mrva = struct.unpack("<QII", at(rva + 4 + i * 16, 16))
            memory.append((start, size, mrva))
    threads = []
    for _size, rva in streams.get(3, []):  # ThreadListStream
        n = struct.unpack("<I", at(rva, 4))[0]
        for i in range(n):
            t = at(rva + 4 + i * 48, 48)
            tid = struct.unpack("<I", t[:4])[0]
            ctx_size, ctx_rva = struct.unpack("<II", t[40:48])
            ctx = at(ctx_rva, ctx_size)
            rsp = struct.unpack("<Q", ctx[0x98:0xA0])[0] if len(ctx) >= 0x100 else 0
            rip = struct.unpack("<Q", ctx[0xF8:0x100])[0] if len(ctx) >= 0x100 else 0
            # The stack from the memory list (the thread's own descriptor can be empty).
            words = []
            for start, size, mrva in memory:
                if start <= rsp < start + size:
                    for k in range((rsp - start) & ~7, size - 7, 8):
                        words.append(struct.unpack("<Q", at(mrva + k, 8))[0])
                    break
            threads.append((tid, rip, words))
    return modules, threads


def read_map(path):
    base = 0x140000000
    symbols = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"\s*Preferred load address is ([0-9a-fA-F]+)", line)
            if m:
                base = int(m.group(1), 16)
            m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s", line)
            if m:
                symbols.append((int(m.group(2), 16) - base, m.group(1)))
    symbols.sort()
    return [s[0] for s in symbols], [s[1] for s in symbols]


def short(name):
    m = re.match(r"\?([^@?]+)@([^@]+)@", name)
    return "%s::%s" % (m.group(2), m.group(1)) if m else name[:100]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("map")
    ap.add_argument("--module", default="godot", help="the image whose addresses are named (a part of its file name)")
    ap.add_argument("--all", action="store_true", help="every thread, with the modules of its other addresses")
    args = ap.parse_args()
    modules, threads = read_dump(args.dump)
    image = next((m for m in modules if args.module.lower() in m[2].replace("\\", "/").split("/")[-1].lower()), None)
    if image is None:
        raise SystemExit("no module matching '%s' in the dump" % args.module)
    addrs, names = read_map(args.map)

    def module_of(a):
        for base, size, name in modules:
            if base <= a < base + size:
                return name.replace("\\", "/").split("/")[-1], a - base
        return None, None

    for tid, rip, words in threads:
        chain = []
        for v in words:
            if image[0] <= v < image[0] + image[1]:
                off = v - image[0]
                i = bisect.bisect_right(addrs, off) - 1
                if i < 0 or off == addrs[i]:
                    continue  # a function's own address: a pointer, not a return address
                name = short(names[i])
                if not chain or chain[-1] != name:
                    chain.append(name)
        if not chain and not args.all:
            continue
        rip_module, rip_off = module_of(rip)
        print("thread %d (at %s+%s): %s" % (tid, rip_module, hex(rip_off) if rip_off is not None else "?", " < ".join(chain) or "-"))


if __name__ == "__main__":
    main()
