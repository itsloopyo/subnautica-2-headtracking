OUT  = r"C:\tmp\sub2_pool_v2.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, label, rva, limit=40):
    a = addr(BASE + rva)
    fn = fm.getFunctionContaining(a)
    f.write("=" * 70 + "\n%s @ RVA 0x%x\n" % (label, rva))
    if fn is None:
        f.write("  (no fn)\n"); return
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        note = ""
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta and ta.getOffset() >= BASE and blk(ta) in (".data",".bss"):
                note = "   ; [0x%08x] %s" % (ta.getOffset()-BASE, blk(ta))
        f.write("  +0x%08x  %s%s\n" % (ins.getAddress().getOffset()-BASE,
                                        ins.toString(), note))

with open(OUT, "w") as f:
    # FName accessor called by GPV - references the pool init-flag + pool base.
    dump_fn(f, "FName accessor 0x147bd40 (pool+flag)", 0x147bd40, limit=30)
    # A couple more FName helpers in the same cluster, in case 0x147bd40
    # uses a name-table side global rather than the entry pool.
    dump_fn(f, "FName helper 0x147be00", 0x147be00, limit=40)
    dump_fn(f, "FName helper 0x147bb10 (store)", 0x147bb10, limit=60)

print("Wrote %s" % OUT)
