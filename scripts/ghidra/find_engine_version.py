# Subnautica 2 (UE5): identify engine version + dump useful UE marker strings.
#
# Broad pass: walk every initialized data block, extract ASCII strings,
# print every one containing UE keywords. Also dumps the memory block
# table so we can see what sections exist (UE shipping binaries sometimes
# put rdata under unusual names depending on compiler options).

OUT = r"C:\tmp\sub2_engine_version.txt"

KEYWORDS = [
    "Unreal Engine",
    "++UE",
    "UE_4_", "UE_5_",
    "Release-4.", "Release-5.",
    "EngineVersion",
    "GEngine",
    "PlayerCameraManager",
    "PlayerController",
    "MinimalViewInfo",
    "UWorld",
    "GameViewport",
    "CameraComponent",
    "BuildVersion",
    "BranchName",
    "Subnautica2",
]

mem = currentProgram.getMemory()

def is_printable_ascii(b):
    return 0x20 <= b <= 0x7e

def read_cstring(addr, max_len=512):
    out = []
    a = addr
    for _ in range(max_len):
        try:
            v = mem.getByte(a) & 0xff
        except:
            return None
        if v == 0:
            if len(out) < 4:
                return None
            return "".join(chr(c) for c in out)
        if not is_printable_ascii(v):
            return None
        out.append(v)
        a = a.add(1)
    return None

def read_wstring(addr, max_len=512):
    out = []
    a = addr
    for _ in range(max_len):
        try:
            lo = mem.getByte(a) & 0xff
            hi = mem.getByte(a.add(1)) & 0xff
        except:
            return None
        if lo == 0 and hi == 0:
            if len(out) < 4:
                return None
            return "".join(chr(c) for c in out)
        if hi != 0 or not is_printable_ascii(lo):
            return None
        out.append(lo)
        a = a.add(2)
    return None

print("Memory blocks:")
blocks = []
for blk in mem.getBlocks():
    flags = []
    if blk.isInitialized(): flags.append("I")
    if blk.isExecute(): flags.append("X")
    if blk.isWrite(): flags.append("W")
    if blk.isRead(): flags.append("R")
    sz = blk.getEnd().getOffset() - blk.getStart().getOffset() + 1
    print("  %-12s %s-%s %s %d" % (blk.getName(), blk.getStart(), blk.getEnd(), "".join(flags), sz))
    blocks.append(blk)

print("Scanning initialized non-exec blocks for UE strings...")
hits_a = []
hits_w = []
keywords_lower = [k.lower() for k in KEYWORDS]

def has_kw(s):
    sl = s.lower()
    for k in keywords_lower:
        if k in sl:
            return True
    return False

for blk in blocks:
    if not blk.isInitialized(): continue
    if blk.isExecute(): continue
    start = blk.getStart()
    end = blk.getEnd()
    a = start
    last = end.getOffset()
    while a.getOffset() < last - 1:
        try:
            b0 = mem.getByte(a) & 0xff
        except:
            a = a.add(1); continue

        if 0x20 <= b0 <= 0x7e:
            try:
                b1 = mem.getByte(a.add(1)) & 0xff
            except:
                b1 = 0xff
            if b1 == 0 and (0x20 <= (mem.getByte(a.add(2)) & 0xff) <= 0x7e if a.getOffset() + 2 < last else False):
                s = read_wstring(a, 256)
                if s:
                    if has_kw(s):
                        hits_w.append((a, s))
                    a = a.add(len(s) * 2 + 2)
                    continue
            s = read_cstring(a, 256)
            if s:
                if has_kw(s):
                    hits_a.append((a, s))
                a = a.add(len(s) + 1)
                continue
        a = a.add(1)

print("  ascii hits: %d, wide hits: %d" % (len(hits_a), len(hits_w)))

ref_mgr = currentProgram.getReferenceManager()
with open(OUT, "w") as f:
    f.write("Subnautica 2 - UE engine version + marker strings\n")
    f.write("=" * 60 + "\n\n")
    f.write("Memory blocks:\n")
    for blk in blocks:
        flags = []
        if blk.isInitialized(): flags.append("I")
        if blk.isExecute(): flags.append("X")
        if blk.isWrite(): flags.append("W")
        sz = blk.getEnd().getOffset() - blk.getStart().getOffset() + 1
        f.write("  %-12s %s-%s %s %d\n" % (blk.getName(), blk.getStart(), blk.getEnd(), "".join(flags), sz))
    f.write("\nASCII hits:\n")
    for addr, s in hits_a:
        f.write("%s  %r\n" % (addr, s))
        refs = ref_mgr.getReferencesTo(addr)
        n = 0
        for r in refs:
            if n >= 3: break
            f.write("    xref from %s\n" % r.getFromAddress())
            n += 1
    f.write("\nWide (UTF-16) hits:\n")
    for addr, s in hits_w:
        f.write("%s  %r\n" % (addr, s))
        refs = ref_mgr.getReferencesTo(addr)
        n = 0
        for r in refs:
            if n >= 3: break
            f.write("    xref from %s\n" % r.getFromAddress())
            n += 1

print("Wrote %s" % OUT)
