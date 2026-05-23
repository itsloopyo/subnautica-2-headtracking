# Identify the UClass that owns the `bIsHovering` UPROPERTY whose
# FBoolPropertyParams lives at 0x14ad7f7a0 (the SetBit thunk
# 0x144534e60 writes byte 1 to [rcx + 0x8c], so we know the field
# offset; we just don't know the class).
#
# UE5 generates class registrations as nested static structs:
#   - Each UPROPERTY is an FPropertyParams.
#   - The class's UStruct/UClass params point at an array of
#     FPropertyParams pointers (the "PropPointers" array).
#   - That array is referenced by an FClassParams (or similar)
#     struct whose +0x00 field is a pointer to the class's ANSI
#     name string.
#
# Strategy:
#   1. Find qword refs in .rdata to FBoolPropertyParams @ 0x14ad7f7a0.
#      Each ref is an entry in a PropPointers array.
#   2. For each ref, walk backwards through the .rdata block looking
#      for the start of the array (a qword that points at .rdata
#      followed by structurally sensible data).
#   3. Find qword refs to the array start - those land in
#      FClassParams structs.
#   4. Dump those FClassParams; the first qword usually points at
#      the class name string ("/Script/<Package>.<Class>").
#
# Same approach handles any UPROPERTY whose FPropertyParams address
# is known - parameterised at the top.

OUT = r"C:\tmp\sub2_isHovering_class.txt"

# FBoolPropertyParams for bIsHovering. Found in sub2_hover_state.txt.
TARGET_PROPERTY_PARAMS = 0x14ad7f7a0
TARGET_LABEL           = "bIsHovering"

mem   = currentProgram.getMemory()
fact  = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_qword(a):
    try:
        return mem.getLong(a) & 0xffffffffffffffff
    except:
        return None

def safe_bytes(a, n):
    out = bytearray()
    for i in range(n):
        try:
            out.append(mem.getByte(addr(a + i)) & 0xff)
        except:
            break
    return bytes(out)

def read_cstring(a, maxlen=256):
    b = safe_bytes(a, maxlen)
    end = b.find(b"\x00")
    if end < 0:
        return None
    try:
        return b[:end].decode("ascii")
    except:
        return None

def find_qword_refs(target_value, want_blocks=(".rdata", ".data"), limit=64):
    matches = []
    for block in mem.getBlocks():
        if block.getName() not in want_blocks:
            continue
        start = block.getStart().getOffset()
        end   = block.getEnd().getOffset()
        cur = start
        while cur + 8 <= end:
            if safe_qword(addr(cur)) == target_value:
                matches.append(cur)
                if len(matches) >= limit:
                    return matches
            cur += 8
    return matches

with open(OUT, "w") as f:
    f.write("Identify UClass for %s @ FPropertyParams 0x%x\n" % (TARGET_LABEL, TARGET_PROPERTY_PARAMS))
    f.write("=" * 78 + "\n\n")

    # Step 1: refs in .rdata point to entries in PropPointers arrays.
    refs = find_qword_refs(TARGET_PROPERTY_PARAMS)
    f.write("PropPointers entry refs (%d):\n" % len(refs))
    for r in refs:
        f.write("  0x%x  [%s]\n" % (r, block_name(addr(r))))
    f.write("\n")

    # Step 2: for each ref, walk backwards finding the array head
    # (preceding qwords are either zero or point to OTHER FPropertyParams
    # in .rdata; the array head is preceded by a non-pointer or
    # mis-aligned data). Also find the array tail similarly.
    for r in refs:
        f.write("--- ref @ 0x%x ---\n" % r)
        # Walk back up to 0x100 bytes finding array start.
        start = r
        for k in range(0, 0x40):
            prev_addr = r - 8 - (k * 8)
            v = safe_qword(addr(prev_addr))
            if v is None:
                break
            # Heuristic: array entries point into .rdata (where
            # FPropertyParams live). Stop when we see a qword that
            # doesn't look like an .rdata pointer.
            if image_base <= v < image_base + 0x10000000:
                blk = block_name(addr(v))
                if blk in (".rdata",):
                    start = prev_addr
                    continue
            break
        # Walk forward to find end of array.
        end = r + 8
        for k in range(0, 0x40):
            next_addr = r + 8 + (k * 8)
            v = safe_qword(addr(next_addr))
            if v is None:
                break
            if image_base <= v < image_base + 0x10000000:
                blk = block_name(addr(v))
                if blk in (".rdata",):
                    end = next_addr + 8
                    continue
            break
        n_entries = (end - start) // 8
        f.write("  PropPointers array: 0x%x .. 0x%x (%d entries)\n" %
                (start, end, n_entries))
        # Dump entries
        for i in range(n_entries):
            ea = start + i * 8
            ev = safe_qword(addr(ea))
            f.write("    [%2d] @ 0x%x -> 0x%x\n" % (i, ea, ev or 0))

        # Step 3: who references the array start?
        array_refs = find_qword_refs(start, limit=16)
        f.write("  array-start xrefs (%d):\n" % len(array_refs))
        for ar in array_refs:
            f.write("    0x%x  [%s]\n" % (ar, block_name(addr(ar))))
            # Step 4: dump the containing FClassParams/UStructParams
            # struct - its earlier qwords often point at class name strings.
            base = ar & ~0x7
            for off in range(-0x40, 0x40, 8):
                a = base + off
                v = safe_qword(addr(a))
                if v is None:
                    continue
                tag = ""
                cstr = None
                if image_base <= v < image_base + 0x10000000:
                    bn = block_name(addr(v))
                    tag = " -> [%s]" % bn
                    if bn == ".rdata":
                        cstr = read_cstring(v)
                f.write("      +%+04x 0x%016x%s%s\n" %
                        (off, v, tag,
                         (" \"%s\"" % cstr) if cstr and len(cstr) >= 3 else ""))
        f.write("\n")

print("Wrote %s" % OUT)
