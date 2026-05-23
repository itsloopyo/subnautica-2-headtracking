# Confirm the re-derived render caller's containing function (should assemble
# an FMinimalViewInfo for the renderer) and pin the FNamePool global.
OUT  = r"C:\tmp\sub2_confirm_render_pool.txt"
BASE = 0x140000000
RENDER_CALLER_RVA = 0x04171af7

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, ep, limit=70):
    fn = fm.getFunctionContaining(addr(ep))
    f.write("-"*70 + "\nfn entry RVA 0x%x  body=%s\n" % (
        ep-BASE, "?" if fn is None else fn.getBody().getNumAddresses()))
    if fn is None: return None
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        note = ""
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta is None: continue
            d = listing.getDataAt(ta)
            if d is not None and d.hasStringValue():
                try: note = "   ; STR %r" % str(d.getValue())[:50]
                except: pass
            elif ta.getOffset() >= BASE and blk(ta) in (".data",".bss"):
                note = "   ; [0x%08x] %s" % (ta.getOffset()-BASE, blk(ta))
        f.write("  +0x%08x  %s%s\n" % (ins.getAddress().getOffset()-BASE,
                                        ins.toString(), note))
    return fn

with open(OUT, "w") as f:
    f.write("## Render caller containing function (retRVA 0x%x)\n" % RENDER_CALLER_RVA)
    fn = fm.getFunctionContaining(addr(BASE + RENDER_CALLER_RVA))
    if fn is not None:
        f.write("   containing fn entry RVA 0x%x\n" % (fn.getEntryPoint().getOffset()-BASE))
    dump_fn(f, BASE + RENDER_CALLER_RVA, limit=90)

    # FNamePool: candidate 0xcc32300. Confirm by finding the FName accessor
    # (small fn that references a global in 0xcc32000-0xcc33000 with +0x10
    # Blocks indexing). List all functions referencing each candidate global
    # and dump the smallest one.
    f.write("\n\n## FNamePool candidates: functions touching 0xcc32xxx .bss\n")
    cands = {}
    for g in range(0x0cc32000, 0x0cc32400, 8):
        ga = addr(BASE + g)
        if blk(ga) not in (".bss", ".data"): continue
        for r in ref_mgr.getReferencesTo(ga):
            fn2 = fm.getFunctionContaining(r.getFromAddress())
            if fn2:
                ep = fn2.getEntryPoint().getOffset()
                cands.setdefault(g, set()).add(ep)
    for g in sorted(cands):
        eps = cands[g]
        f.write("\n  global [0x%08x]  referenced by %d fn(s)\n" % (g, len(eps)))
        # dump the smallest referencing fn (the accessor)
        sm = sorted(eps, key=lambda e: fm.getFunctionContaining(addr(e)).getBody().getNumAddresses())[:2]
        for ep in sm:
            dump_fn(f, ep, limit=45)

print("Wrote %s" % OUT)
