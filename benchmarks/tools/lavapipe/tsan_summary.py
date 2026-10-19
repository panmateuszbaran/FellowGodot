import sys, re, glob, collections
# Groups ThreadSanitizer reports by the first engine frame (not libtsan/libc/mesa) of each of the two accesses.
files = []
for a in sys.argv[1:]:
    files += glob.glob(a)
groups = collections.Counter()
examples = {}
def engine_frame(lines):
    for l in lines:
        m = re.match(r'\s+#\d+ (.*?) (\S+\.(?:cpp|h|inc)):(\d+)', l)
        if m and 'sanitizer' not in m.group(2) and '/usr/include' not in m.group(2):
            fn = m.group(1).split('(')[0][-80:]
            return f"{fn} {m.group(2)}:{m.group(3)}"
    return None
def first_frame(lines):
    for l in lines:
        m = re.match(r'\s+#\d+ (.*)', l)
        if m: return m.group(1)[:120]
    return None
for f in files:
    txt = open(f, errors='replace').read()
    for rep in txt.split('==================')[1:]:
        if 'WARNING: ThreadSanitizer' not in rep: continue
        kind = re.search(r'WARNING: ThreadSanitizer: ([^(]+)', rep).group(1).strip()
        blocks = re.split(r'\n  (?=(?:Previous )?(?:[Rr]ead|[Ww]rite|[Aa]tomic)|Location|Mutex|Thread T)', rep)
        acc = [b.splitlines() for b in blocks if re.match(r'(Previous )?(read|write|Read|Write|Atomic|atomic)', b)]
        key = [kind]
        top = []
        for a in acc[:2]:
            ef = engine_frame(a[1:]); ff = first_frame(a[1:])
            key.append(ef or ('?' + (ff or '')))
            top.append(ff)
        def in_driver(a):
            fr = [l for l in a[1:] if re.match(r'\s+#\d+ ', l)]
            fr = [l for l in fr if 'libtsan' not in l and 'sanitizer' not in l]
            return bool(fr) and re.search(r'lib(vulkan_lvp|LLVM|gallium|xcb|X11|GL|asound|dbus|pulse)', fr[0]) is not None
        if acc and all(in_driver(a) for a in acc[:2]):
            key = ['(driver-internal) ' + kind]
        k = ' || '.join(key)
        groups[k] += 1
        examples.setdefault(k, (f, top))
for k, n in groups.most_common():
    print(n, k)
    print('     top:', examples[k][1])
