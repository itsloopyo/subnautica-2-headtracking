# Read ANSI C strings at addresses we suspect hold class/property names
# from the identify_isHovering_class.py output. Also dump the .text
# thunk it references (likely Z_Construct_UClass_<Class>_Statics).

OUT = r"C:\tmp\sub2_class_at.txt"

ADDRS = [
    ("FClassParams_minus_18", 0x14ad7fa70),
    ("FClassParams_minus_10", 0x14ad7fa10),
    ("FProperty_entry_0",     0x14ad7f6b0),
    ("FProperty_entry_3_bIsHovering", 0x14ad7f7a0),
]

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
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

def read_cstring(a, maxlen=512):
    b = safe_bytes(a, maxlen)
    end = b.find(b"\x00")
    if end < 0:
        return None
    try:
        return b[:end].decode("ascii", errors="replace")
    except:
        return None

with open(OUT, "w") as f:
    for label, a in ADDRS:
        f.write("=== %s @ 0x%x [%s] ===\n" % (label, a, block_name(addr(a))))
        s = read_cstring(a)
        f.write("  cstring: %r\n" % s)
        # First-qword + nearby data dump
        for off in range(0, 0x40, 8):
            v = safe_qword(addr(a + off))
            if v is None:
                continue
            tag = ""
            cstr = None
            if image_base <= v < image_base + 0x10000000:
                bn = block_name(addr(v))
                tag = " -> [%s]" % bn
                if bn == ".rdata":
                    cstr = read_cstring(v)
            f.write("  +0x%02x  0x%016x%s%s\n" %
                    (off, v, tag,
                     (" \"%s\"" % cstr[:60]) if cstr and len(cstr) >= 3 else ""))
        f.write("\n")

    # The thunk at 0x1465241f0 (called from the FClassParams) is
    # almost certainly the Z_Construct_UClass_<Class>_Statics caller.
    # Find a string near it that looks like "/Script/...".
    THUNK = 0x1465241f0
    f.write("=== thunk @ 0x%x first 0x80 bytes ===\n" % THUNK)
    bs = safe_bytes(THUNK, 0x80)
    f.write("  hex: %s\n" % bs.hex())

    # Also enumerate any qwords this thunk loads via LEA that point at .rdata strings.
    # A lazy parse: just look at every 4-byte slice for an offset that lands
    # in .rdata and reveals a string.
    f.write("\n=== scan for .rdata pointers near thunk @ 0x%x ===\n" % THUNK)
    for i in range(0, len(bs) - 4):
        # x86-64 RIP-relative LEA: 0x48 0x8d ?? disp32, target = thunk+i+7+disp32
        if bs[i] == 0x48 and bs[i+1] == 0x8d and i + 7 <= len(bs):
            disp = int.from_bytes(bs[i+3:i+7], "little", signed=True)
            target = THUNK + i + 7 + disp
            cstr = read_cstring(target)
            if cstr and len(cstr) >= 4 and all(0x20 <= ord(c) < 0x7f for c in cstr):
                f.write("  LEA @ +0x%02x -> 0x%x \"%s\"\n" % (i, target, cstr[:80]))

print("Wrote %s" % OUT)
