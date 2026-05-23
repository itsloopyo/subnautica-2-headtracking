OUT  = r"C:\tmp\sub2_confirm.txt"
BASE = 0x140000000
GPV_RVA = 0x043ed8f0

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, label, rva, limit=60):
    a = addr(BASE + rva)
    fn = fm.getFunctionContaining(a)
    f.write("=" * 74 + "\n%s @ RVA 0x%x  %s\n" % (
        label, rva, "(no fn)" if fn is None else
        "entry RVA 0x%x" % (fn.getEntryPoint().getOffset()-BASE)))
    if fn is None: return
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        note = ""
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta and ta.getOffset() >= BASE and blk(ta) in (".data",".bss",".rdata"):
                note = "   ; [0x%08x] %s" % (ta.getOffset()-BASE, blk(ta))
        f.write("  +0x%08x  %s%s\n" % (ins.getAddress().getOffset()-BASE,
                                        ins.toString(), note))

with open(OUT, "w") as f:
    # 1. Confirm GPV signature.
    dump_fn(f, "GetPlayerViewPoint (candidate)", GPV_RVA, limit=70)

    # 2. All call sites to GPV: caller fn + return RVA (instr after the call).
    f.write("\n\n## Call-xrefs to GPV 0x%x (return RVA = _ReturnAddress)\n" % GPV_RVA)
    gpv = addr(BASE + GPV_RVA)
    rows = []
    for r in ref_mgr.getReferencesTo(gpv):
        if not r.getReferenceType().isCall():
            continue
        site = r.getFromAddress()
        ins = listing.getInstructionAt(site)
        if ins is None:
            continue
        ret = site.getOffset() + ins.getLength()
        fn = fm.getFunctionContaining(site)
        fnrva = (fn.getEntryPoint().getOffset()-BASE) if fn else 0
        rows.append((ret - BASE, site.getOffset()-BASE, fnrva))
    rows.sort()
    f.write("  %d call site(s):\n" % len(rows))
    for ret, callsite, fnrva in rows:
        f.write("    retRVA 0x%08x   call@0x%08x   in fn RVA 0x%08x\n" %
                (ret, callsite, fnrva))

    # 3. FNamePool getter -> pool global.
    dump_fn(f, "FName pool getter 0x1475930", 0x1475930, limit=40)

    # 4. GUObjectArray allocator store region.
    dump_fn(f, "GUObjectArray allocator 0x16ec1d0 (tail)", 0x16ec1d0, limit=120)

print("Wrote %s" % OUT)
