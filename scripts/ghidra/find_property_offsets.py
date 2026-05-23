# Find UE5 UPROPERTY offsets by locating the static FPropertyParams
# structs that reference each property name string. UE5 emits these as
# const data in .rdata; each struct has:
#   {const char* Name, const char* RepNotify, EPropertyFlags Flags,
#    Object flags, ArrayDim, ElementSize, ..., uint32 Offset, ...}
# The Offset field's position within the struct varies but is typically
# in the first 0x30 - 0x60 bytes.
#
# Strategy:
#   1. For each property name string addr, scan .rdata for a qword == addr.
#   2. The match is the start of an FPropertyParams struct.
#   3. Within the struct, read every 4-byte uint at offsets 0x10..0x60 and
#      report any that look like plausible class-field offsets (0x40..0x800).

OUT = r"C:\tmp\sub2_uproperty_offsets.txt"

# Property name strings, using SET 1 (UClass property registrations,
# not function-param builders). The UClass-register strings live in the
# 0x14a183xxx cluster; the function-param duplicates live at 0x14a299xxx
# and 0x14a2e8xxx. ComponentVelocity at 0x14a1835b0 already matched a
# struct, so SET 1 is the right one.
PROPS = [
    ("RelativeLocation",   0x14a183598),
    ("RelativeRotation",   0x14a167508),
    ("RelativeScale3D",    0x14a167520),
    ("AttachParent",       0x14a183560),
    ("AttachChildren",     0x14a183570),
    ("AttachSocketName",   0x149f8dcc8),
    ("ComponentVelocity",  0x14a1835b0),
    ("bAbsoluteLocation",  0x14a09ff28),
    ("bAbsoluteRotation",  0x14a1835e8),
    ("bAbsoluteScale",     0x14a183600),
    ("bVisible",           0x1499c10b0),
    ("bHiddenInGame",      0x14a1836a8),
]

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def safe_qword(a):
    try:
        return mem.getLong(a) & 0xffffffffffffffff
    except:
        return None

def safe_dword(a):
    try:
        return mem.getInt(a) & 0xffffffff
    except:
        return None

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def find_qword_in_rdata(target_value):
    """Scan .rdata for an 8-byte qword equal to target_value. Returns
    list of matching addresses (struct starts that point at the
    target_value as their first qword)."""
    matches = []
    for block in mem.getBlocks():
        if block.getName() != ".rdata":
            continue
        start = block.getStart().getOffset()
        end   = block.getEnd().getOffset()
        cur = start
        while cur + 8 <= end:
            v = safe_qword(addr(cur))
            if v == target_value:
                matches.append(cur)
                if len(matches) >= 12:
                    return matches
            cur += 8
    return matches

with open(OUT, "w") as f:
    f.write("Subnautica 2: UPROPERTY offsets via FPropertyParams struct search\n")
    f.write("=" * 78 + "\n\n")

    for prop_name, name_addr in PROPS:
        f.write("=" * 78 + "\n")
        f.write("Property: %s  name_str @ 0x%x\n" % (prop_name, name_addr))

        struct_addrs = find_qword_in_rdata(name_addr)
        f.write("  %d struct(s) start with this name ptr\n" % len(struct_addrs))

        for sa in struct_addrs:
            f.write("  --- struct @ 0x%x ---\n" % sa)
            # Dump 0x80 bytes as candidate offset positions. Show every
            # 4-byte uint at offsets 0x00..0x80 that looks like a small
            # plausible struct-field offset (0x40..0x800) or a count
            # (0..16).
            for relOff in range(0x00, 0x80, 4):
                v = safe_dword(addr(sa + relOff))
                if v is None:
                    continue
                if 0x40 <= v <= 0x800:
                    # Plausible class-field offset.
                    f.write("    +0x%02x  uint32=0x%x  (= %d, plausible field offset)\n" %
                            (relOff, v, v))

            # Also show all qwords up to 0x40 so we see the layout.
            f.write("    raw qwords (first 8):\n")
            for qOff in range(0, 0x40, 8):
                v = safe_qword(addr(sa + qOff))
                if v is None:
                    continue
                tag = ""
                # Check if this is a pointer to .rdata (another string?).
                if 0x100000000 < v < 0x1000000000000:
                    tag_block = block_name(addr(v))
                    tag = " -> [%s]" % tag_block
                f.write("      +0x%02x  0x%016x%s\n" % (qOff, v, tag))

        f.write("\n")

print("Wrote %s" % OUT)
