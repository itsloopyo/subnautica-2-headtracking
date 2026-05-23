# Dump raw bytes (as qwords + dwords) at a list of addresses. Useful for
# inspecting static data blobs (FClassParams, FCppClassTypeInfoStatic, etc)
# that Ghidra hasn't auto-typed.

OUT = r"C:\tmp\sub2_bytes_dump.txt"

REGIONS = [
    ("AUWEPlayerCameraManager_vtable",      0x14ac694d0, 0x600),
    ("APlayerCameraManager_parent_ctor",    0x1443ceee0, 0x100),
    ("UWEPlayerCameraManagerSettings_vtable", 0x14ac686b8, 0x200),
    ("AUWE_class_third_vtable",             0x14ac68a50, 0x200),
]

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
sym_tab = currentProgram.getSymbolTable()
fm = currentProgram.getFunctionManager()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_qword(a):
    try: return mem.getLong(a) & 0xffffffffffffffff
    except: return None

def annotate(v):
    if v is None: return ""
    if not (image_base <= v <= image_base + 0x10000000):
        return ""
    a = addr(v)
    bn = block_name(a)
    sym = sym_tab.getPrimarySymbol(a)
    sname = sym.getName() if sym else "-"
    fn = fm.getFunctionContaining(a)
    fnname = " fn=%s" % fn.getName() if fn else ""
    return "  [%s] %s%s" % (bn, sname, fnname)

with open(OUT, "w") as f:
    for label, start, length in REGIONS:
        f.write("=" * 70 + "\n")
        f.write("%s  @ 0x%x  (%d bytes)\n" % (label, start, length))
        for off in range(0, length, 8):
            a = addr(start + off)
            q = safe_qword(a)
            anno = annotate(q)
            f.write("  +0x%03x  0x%016x%s\n" % (off, q if q is not None else 0, anno))
        f.write("\n")

print("Wrote %s" % OUT)
