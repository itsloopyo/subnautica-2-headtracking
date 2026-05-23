# Read ASCII strings at the LEA-target addresses identified in the
# trace_isHovering_thunk.py disassembly. The class name typically sits
# at the first LEA-loaded .rdata pointer in the construction thunk
# (UE5 emits `lea rax, [class_name_string]` near the top).

OUT = r"C:\tmp\sub2_class_strings.txt"

# Target addresses pulled from the thunk disasm.
ADDRS = [
    0x141292410,
    0x141292480,
    0x1414fa2c0,
    0x14ada6248,
    0x14ad73bc0,
    0x146522440,
    0x146531a50,
    0x1498a59b8,
    0x14d1032a8,  # the cached UClass*; deref it then dump UClass.Name
]

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_byte(a):
    try:
        return mem.getByte(a) & 0xff
    except:
        return None

def safe_qword(a):
    try:
        return mem.getLong(a) & 0xffffffffffffffff
    except:
        return None

def read_cstring(start, maxlen=512):
    out = []
    for i in range(maxlen):
        b = safe_byte(addr(start + i))
        if b is None:
            break
        if b == 0:
            return "".join(out)
        if 0x20 <= b < 0x7f:
            out.append(chr(b))
        else:
            out.append("\\x%02x" % b)
    return "".join(out) if out else None

with open(OUT, "w") as f:
    for a in ADDRS:
        try:
            blk = block_name(addr(a))
        except:
            blk = "?"
        f.write("=== 0x%x [%s] ===\n" % (a, blk))
        try:
            s = read_cstring(a)
        except:
            s = None
        f.write("  cstring: %r\n" % s)
        f.write("  hex:")
        for i in range(0x20):
            try:
                b = safe_byte(addr(a + i))
            except:
                b = None
            f.write(" %s" % ("%02x" % b if b is not None else "??"))
        f.write("\n\n")
        f.flush()

print("Wrote %s" % OUT)
