# Disassemble the OLD allocator + FName decoder far enough to see which
# rip-relative instruction touches ObjObjects (0x0cd16500) and the FNamePool
# init flag, recording the instruction's byte-offset within its function. The
# patched build runs the identical code, so the instruction at the same offset
# (with a relocated disp32) points at the relocated global.
OUT  = r"C:\tmp\sub2_old_global_writers.txt"
BASE = 0x140000000

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm   = currentProgram.getFunctionManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

OLD_OBJOBJECTS = 0x0cd16500
OLD_FNAMEPOOL  = 0x0cc32300
OLD_FNAME_FLAG = 0x0cc32099

def disasm_fn(f, rva, limit=160):
    fn = fm.getFunctionContaining(addr(BASE + rva))
    if fn is None:
        f.write("  no fn at 0x%x\n" % rva); return
    ep = fn.getEntryPoint().getOffset() - BASE
    f.write("fn entry RVA 0x%08x  body=%d\n" % (ep, fn.getBody().getNumAddresses()))
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        off = ins.getAddress().getOffset() - BASE - ep
        note = ""
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta is None: continue
            t = ta.getOffset() - BASE
            if t in (OLD_OBJOBJECTS, OLD_OBJOBJECTS+0x14):
                note += "  <== ObjObjects(+0x%x)" % (t - OLD_OBJOBJECTS)
            if t in (OLD_FNAMEPOOL, OLD_FNAME_FLAG):
                note += "  <== FNamePool%s" % ("(flag)" if t==OLD_FNAME_FLAG else "")
        f.write("  +0x%03x  %s%s\n" % (off, ins.toString(), note))

with open(OUT, "w") as f:
    f.write("## allocator 0x016ed040 (ObjObjects writer)\n")
    disasm_fn(f, 0x016ed040)
    f.write("\n## FName decoder 0x0147d030 (FNamePool ref)\n")
    disasm_fn(f, 0x0147d030, limit=60)

print("Wrote %s" % OUT)
