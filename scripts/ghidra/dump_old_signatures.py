# Dump raw bytes at the OLD build's known-good RVAs so we can build masked
# byte-signatures and relocate the same functions in a patched EXE without
# relying on Ghidra full analysis of the new binary. Run against the OLD,
# fully-analyzed project (C:\temp\subnautica-2 / Subnautica2).
OUT  = r"C:\tmp\sub2_old_signatures.txt"
BASE = 0x140000000

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

def dump(rva, n):
    a = addr(BASE + rva)
    out = []
    for i in range(n):
        try:
            out.append(mem.getByte(a.add(i)) & 0xFF)
        except:
            out.append(-1)
    return out

# Known-good old (2026-05-22 Steam) RVAs from steam_offsets.cpp.
TARGETS = [
    ("GPV",            0x043ed6f0, 96),
    ("render_caller",  0x041718b0, 64),   # containing fn of retRVA 0x04171af7
    ("ObjObjects_fn",  0x016ed040, 48),   # allocator (per NOTES) - may differ
    ("FNamePool_decoder", 0x0147d030, 64),
]

with open(OUT, "w") as f:
    for name, rva, n in TARGETS:
        bs = dump(rva, n)
        f.write("%s @ RVA 0x%08x (%d bytes)\n" % (name, rva, n))
        f.write("  " + " ".join(("%02x" % b) if b >= 0 else "??" for b in bs) + "\n\n")

print("Wrote %s" % OUT)
