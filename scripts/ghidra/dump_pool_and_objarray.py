OUT  = r"C:\tmp\sub2_pool_objarray.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, label, ep, limit=220):
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

def fns_for_string(needle, maxfns=4):
    out = []
    for data in listing.getDefinedData(True):
        if not data.hasStringValue(): continue
        try: s = str(data.getValue())
        except: continue
        if needle.lower() in s.lower():
            seen = set()
            for r in ref_mgr.getReferencesTo(data.getAddress()):
                fn = fm.getFunctionContaining(r.getFromAddress())
                if fn:
                    ep = fn.getEntryPoint().getOffset()
                    if ep not in seen:
                        seen.add(ep); out.append((s, ep))
            if out: return out[:maxfns]
    return out

with open(OUT, "w") as f:
    dump_fn(f, "FName_store_ansi_0x14147bb10  (GNames pool)", 0x14147bb10, limit=130)

    for needle in ["MaxObjectsInGame", "gc.MaxObjects", "disregard for GC",
                   "Unable to add more objects", "objects to disregard"]:
        hits = fns_for_string(needle)
        f.write("\n### %r -> %d fn(s)\n" % (needle, len(hits)))
        for s, ep in hits:
            dump_fn(f, "fn(%s) for %r" % (("RVA 0x%x" % (ep-BASE)), needle), ep, limit=260)

print("Wrote %s" % OUT)
