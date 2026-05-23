# UTF-16 string reader for addresses identified from the bIsHovering
# UClass construction thunk. UE5 stores class/package names as wide
# strings in .rdata. The previous ASCII read stopped at the first
# null byte of each char pair; we need to decode as UTF-16LE to get
# the full "/Script/<Module>.<Class>" path.

OUT = r"C:\tmp\sub2_wstrings.txt"

ADDRS = [
    0x14ad73bc0,  # starts "/Script/Interact..."
    0x14ada6248,  # starts "ScalableSphere..."
    0x1498a59b8,  # starts "Engine"
    0x14ad7fa70,  # mystery .rdata at FClassParams -0x18
    0x14ad7fa10,  # PropPointers array end
]

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    try:
        blk = mem.getBlock(a)
        return blk.getName() if blk else "?"
    except:
        return "?"

def safe_byte(a):
    try:
        return mem.getByte(a) & 0xff
    except:
        return None

def read_wstring(start, maxchars=256):
    out = []
    for i in range(maxchars):
        lo = safe_byte(addr(start + i*2))
        hi = safe_byte(addr(start + i*2 + 1))
        if lo is None or hi is None:
            break
        ch = lo | (hi << 8)
        if ch == 0:
            return "".join(out)
        if 0x20 <= ch < 0x7f:
            out.append(chr(ch))
        else:
            out.append("\\u%04x" % ch)
    return "".join(out) if out else None

with open(OUT, "w") as f:
    for a in ADDRS:
        f.write("=== 0x%x [%s] ===\n" % (a, block_name(addr(a))))
        s = read_wstring(a)
        f.write("  wstring: %r\n" % s)
        f.write("\n")
        f.flush()

print("Wrote %s" % OUT)
