# Find USceneComponent::AttachParent's byte offset. AttachParent is an
# FObjectProperty registered via an FObjectPropertyParams struct in .rdata that
# starts with a pointer to the "AttachParent" C string. The struct carries the
# field's Offset (a uint16). Dump candidate offsets so we can pick the one in
# the SceneComponent field range (RelativeLoc=0x148, ComponentToWorld=0x1f0, so
# AttachParent should be ~0x100-0x300).

OUT = r"C:\tmp\sub2_attachparent.txt"
NAMES = ["AttachParent", "AttachChildren", "RelativeLocation"]  # cross-check siblings

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_qword(a):
    try: return mem.getLong(a) & 0xffffffffffffffff
    except: return None

def safe_u32(a):
    try: return mem.getInt(a) & 0xffffffff
    except: return None

def safe_u16(a):
    try: return mem.getShort(a) & 0xffff
    except: return None

def find_string(s):
    target = bytearray(s + "\x00", "ascii")
    masks = bytearray([0xff] * len(target))
    hits = []
    start = currentProgram.getMinAddress()
    while True:
        found = mem.findBytes(start, bytes(target), bytes(masks), True, monitor)
        if found is None:
            break
        # require the byte before to be 0 so we match whole strings
        hits.append(found.getOffset())
        start = found.add(1)
        if len(hits) >= 24:
            break
    return hits

def find_qword_refs(target_value):
    matches = []
    for block in mem.getBlocks():
        if block.getName() not in (".rdata", ".data"):
            continue
        cur = block.getStart().getOffset()
        end = block.getEnd().getOffset()
        while cur + 8 <= end:
            if safe_qword(addr(cur)) == target_value:
                matches.append(cur)
                if len(matches) >= 24:
                    return matches
            cur += 8
    return matches

with open(OUT, "w") as f:
    for nm in NAMES:
        f.write("=" * 78 + "\n")
        f.write("Property: %s\n" % nm)
        saddrs = [a for a in find_string(nm)]
        # keep only exact-length strings (next byte after is the terminator we
        # already required; also filter out longer names that start with nm)
        f.write("  string at: %s\n" % ", ".join("0x%x" % a for a in saddrs))
        for sa in saddrs:
            refs = find_qword_refs(sa)
            for st in refs:
                f.write("  -- FPropertyParams candidate @ 0x%x [%s] --\n" %
                        (st, block_name(addr(st))))
                # Dump uint16 + uint32 fields 0x10..0x60; flag plausible offsets.
                for off in range(0x10, 0x60, 2):
                    u16 = safe_u16(addr(st + off))
                    if u16 is not None and 0x80 <= u16 <= 0x400:
                        f.write("     u16 +0x%02x = 0x%x (%d)\n" % (off, u16, u16))
                for off in range(0x10, 0x60, 4):
                    u32 = safe_u32(addr(st + off))
                    if u32 is not None and 0x80 <= u32 <= 0x400:
                        f.write("     u32 +0x%02x = 0x%x (%d)\n" % (off, u32, u32))
                # raw qwords for layout context
                for q in range(0, 0x40, 8):
                    v = safe_qword(addr(st + q))
                    tag = ""
                    if v is not None and 0x100000000 < v < 0x1000000000000:
                        tag = " -> [%s]" % block_name(addr(v))
                    f.write("       +0x%02x 0x%016x%s\n" % (q, v if v else 0, tag))
        f.write("\n")

print("Wrote %s" % OUT)
