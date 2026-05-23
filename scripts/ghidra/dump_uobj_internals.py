OUT  = r"C:\tmp\sub2_uobj_internals.txt"
BASE = 0x140000000

FUNCS = [
    ("fname_helper_A", 0x141475860),
    ("fname_helper_B", 0x141475690),
    ("LogUObjectArray_fn", 0x140df8bf0),
    ("DumpUObjectCounts_cvar_fn", 0x140df8190),
]

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

with open(OUT, "w") as f:
    for label, ep in FUNCS:
        a = addr(ep)
        fn = fm.getFunctionContaining(a)
        f.write("=" * 74 + "\n%s @ 0x%x (RVA 0x%x)\n" % (label, ep, ep - BASE))
        if fn is None:
            # Maybe a thunk - disassemble raw a few instructions.
            f.write("  (no Ghidra function; raw disasm)\n")
            cur = a
            for _ in range(12):
                ins = listing.getInstructionAt(cur)
                if ins is None:
                    f.write("  +0x%08x  <no instr>\n" % (cur.getOffset() - BASE))
                    break
                f.write("  +0x%08x  %s\n" % (cur.getOffset() - BASE, ins.toString()))
                cur = ins.getAddress().add(ins.getLength())
            f.write("\n")
            continue
        n = 0
        for ins in listing.getInstructions(fn.getBody(), True):
            n += 1
            if n > 160:
                f.write("  ... truncated\n"); break
            note = ""
            for r in ins.getReferencesFrom():
                ta = r.getToAddress()
                if ta and ta.getOffset() >= BASE:
                    note = "   ; -> [0x%08x] %s" % (ta.getOffset() - BASE, blk(ta))
            f.write("  +0x%08x  %s%s\n" % (ins.getAddress().getOffset() - BASE,
                                            ins.toString(), note))
        f.write("\n")

print("Wrote %s" % OUT)
