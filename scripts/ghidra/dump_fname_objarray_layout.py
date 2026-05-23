OUT  = r"C:\tmp\sub2_fname_objarray.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, label, ep, limit=200):
    a = addr(ep)
    fn = fm.getFunctionContaining(a)
    f.write("=" * 74 + "\n%s @ 0x%x (RVA 0x%x)\n" % (label, ep, ep - BASE))
    if fn is None:
        f.write("  (no fn)\n\n"); return
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit:
            f.write("  ... truncated\n"); break
        note = ""
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta and ta.getOffset() >= BASE and blk(ta) in (".data", ".bss"):
                note = "   ; GLOBAL [0x%08x] %s" % (ta.getOffset() - BASE, blk(ta))
        f.write("  +0x%08x  %s%s\n" % (ins.getAddress().getOffset() - BASE,
                                        ins.toString(), note))
    f.write("\n")

def fn_for_string(needle):
    for data in listing.getDefinedData(True):
        if not data.hasStringValue():
            continue
        try: s = str(data.getValue())
        except: continue
        if needle in s:
            for r in ref_mgr.getReferencesTo(data.getAddress()):
                fn = fm.getFunctionContaining(r.getFromAddress())
                if fn:
                    return fn.getEntryPoint().getOffset()
    return None

with open(OUT, "w") as f:
    # FName resolver internals: the store fn reveals the pool global + Blocks.
    dump_fn(f, "FName_store_wide_0x141483c80", 0x141483c80, limit=160)

    # Pool allocator references this exact error string and writes the
    # GUObjectArray fields right after.
    for needle in ["Max UObject count is invalid",
                   "Dumping allocated UObject counts to log:"]:
        ep = fn_for_string(needle)
        f.write("\n### string %r -> fn RVA %s\n" % (
            needle, ("0x%x" % (ep - BASE)) if ep else "NOT FOUND"))
        if ep:
            dump_fn(f, "fn_for_%s" % needle[:24].replace(" ", "_"), ep, limit=220)

print("Wrote %s" % OUT)
