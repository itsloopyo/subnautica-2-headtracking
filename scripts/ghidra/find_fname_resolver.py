OUT  = r"C:\tmp\sub2_fname_resolver.txt"
BASE = 0x140000000
POOL_RVA = 0xcc31300

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, ep, limit=90):
    a = addr(ep)
    fn = fm.getFunctionContaining(a)
    f.write("-" * 70 + "\nfn RVA 0x%x  body=%s\n" % (
        ep - BASE, "?" if fn is None else fn.getBody().getNumAddresses()))
    if fn is None:
        return
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit:
            f.write("  ...\n"); break
        note = ""
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta and ta.getOffset() >= BASE and blk(ta) in (".data", ".bss"):
                note = "   ; GLOBAL [0x%08x]" % (ta.getOffset() - BASE)
        f.write("  +0x%08x  %s%s\n" % (ins.getAddress().getOffset()-BASE,
                                        ins.toString(), note))

# Find smallest functions that reference the pool global - the resolver is
# tiny (handle -> entry pointer arithmetic).
pa = addr(BASE + POOL_RVA)
cand = {}
for r in ref_mgr.getReferencesTo(pa):
    fn = fm.getFunctionContaining(r.getFromAddress())
    if fn:
        ep = fn.getEntryPoint().getOffset()
        cand[ep] = fn.getBody().getNumAddresses()

with open(OUT, "w") as f:
    f.write("xrefs to FNamePool 0x%x : %d functions\n" % (POOL_RVA, len(cand)))
    f.write("=" * 70 + "\n")
    # smallest first - resolver/accessor helpers
    for ep, sz in sorted(cand.items(), key=lambda kv: kv[1])[:8]:
        dump_fn(f, ep, limit=90)
    f.write("\n")

print("Wrote %s  (%d xref fns)" % (OUT, len(cand)))
