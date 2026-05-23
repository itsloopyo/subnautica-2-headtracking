# Consolidated post-patch re-derivation. String/structure-anchored so it does
# not depend on any previously-captured RVA (those all move on a patch).
# Emits the load-bearing RVAs for headtracking_mod.cpp / ghidra_offsets.h:
#   - GetPlayerViewPoint hook target (+ prologue bytes for sanity)
#   - every static call site to GPV (retRVA = _ReturnAddress) + containing fn,
#     annotated with string refs in that fn so the render-path caller stands out
#   - GUObjectArray.ObjObjects allocator + its global-write cluster
#   - FNamePool global
#   - ProcessEvent candidate
OUT  = r"C:\tmp\sub2_rederive_all.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def fn_at(rva):
    return fm.getFunctionContaining(addr(BASE + rva))

def fns_for_string(needle, want_substr=True, maxhits=12):
    out = []
    for data in listing.getDefinedData(True):
        if not data.hasStringValue(): continue
        try: s = str(data.getValue())
        except: continue
        ok = (needle.lower() in s.lower()) if want_substr else (needle == s)
        if not ok: continue
        for r in ref_mgr.getReferencesTo(data.getAddress()):
            fn = fm.getFunctionContaining(r.getFromAddress())
            if fn:
                out.append((s, fn.getEntryPoint().getOffset() - BASE,
                            data.getAddress().getOffset() - BASE))
        if len(out) >= maxhits: break
    return out

def strings_in_fn(fn, limit=4000):
    """Distinct .rdata string values referenced inside fn (render-path hint)."""
    res = []
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta is None: continue
            d = listing.getDataAt(ta)
            if d is not None and d.hasStringValue():
                try: res.append(str(d.getValue()))
                except: pass
    # de-dup, keep order
    seen = set(); uniq = []
    for s in res:
        if s not in seen:
            seen.add(s); uniq.append(s)
    return uniq

def globals_written(fn, limit=400):
    res = []
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta and ta.getOffset() >= BASE and blk(ta) in (".data", ".bss"):
                res.append((ins.getAddress().getOffset()-BASE,
                            ins.getMnemonicString(), ta.getOffset()-BASE))
    return res

with open(OUT, "w") as f:
    f.write("Consolidated re-derivation. image base 0x%x\n" % BASE)
    f.write("=" * 78 + "\n\n")

    # 1. GetPlayerViewPoint - anchored on the checkf string.
    f.write("## 1. GetPlayerViewPoint (anchor: checkf string)\n")
    gpv_rva = None
    for s, frva, srva in fns_for_string("APlayerController::GetPlayerViewPoint"):
        if gpv_rva is None: gpv_rva = frva
        f.write("   fn RVA 0x%08x  (str 0x%x %r)\n" % (frva, srva, s[:55]))
    if gpv_rva is not None:
        f.write("   -> GPV entry RVA 0x%08x\n" % gpv_rva)
        # prologue bytes
        bs = []
        a = addr(BASE + gpv_rva)
        for i in range(16):
            bs.append(mem.getByte(a.add(i)) & 0xFF)
        f.write("   prologue: %s\n" % " ".join("%02x" % b for b in bs))

    # 2. Static call sites to GPV.
    f.write("\n## 2. GPV call sites (retRVA = _ReturnAddress, with fn string hints)\n")
    if gpv_rva is not None:
        gpv = addr(BASE + gpv_rva)
        rows = []
        for r in ref_mgr.getReferencesTo(gpv):
            if not r.getReferenceType().isCall(): continue
            site = r.getFromAddress()
            ins = listing.getInstructionAt(site)
            if ins is None: continue
            ret = site.getOffset() + ins.getLength()
            fn = fm.getFunctionContaining(site)
            fnrva = (fn.getEntryPoint().getOffset()-BASE) if fn else 0
            rows.append((ret-BASE, site.getOffset()-BASE, fnrva, fn))
        rows.sort()
        f.write("   %d call site(s):\n" % len(rows))
        for ret, cs, fnrva, fn in rows:
            hints = ""
            if fn is not None:
                ss = strings_in_fn(fn)
                key = [x for x in ss if any(k in x for k in
                       ("View", "Camera", "FOV", "Scene", "Projection", "Aspect"))]
                hints = ("  hints=%s" % key[:4]) if key else ""
            f.write("     retRVA 0x%08x  call@0x%08x  fn 0x%08x%s\n" %
                    (ret, cs, fnrva, hints))

    # 3. GUObjectArray allocator + write cluster (ObjObjects base lives here).
    f.write("\n## 3. GUObjectArray (anchor: 'MaxObjectsInGame', largest writer)\n")
    best = None
    for s, frva, srva in fns_for_string("MaxObjectsInGame"):
        fn = fn_at(frva)
        if fn is None: continue
        gw = globals_written(fn)
        if best is None or len(gw) > len(best[1]):
            best = (frva, gw, fn)
    if best is not None:
        frva, gw, fn = best
        f.write("   allocator fn RVA 0x%08x  (%d global writes)\n" % (frva, len(gw)))
        for site, mn, g in gw:
            f.write("      +0x%08x %-6s [0x%08x]\n" % (site, mn, g))

    # 4. FNamePool global - the FName store/intern helper references it next to
    #    an init-flag byte. Anchor on the helper via 'FNamePool' / 'Max FName'.
    f.write("\n## 4. FNamePool (anchor: FName helper global accesses)\n")
    seen_fp = set()
    for needle in ["FNamePool", "Max FName", "FName"]:
        for s, frva, srva in fns_for_string(needle, maxhits=4):
            if frva in seen_fp: continue
            seen_fp.add(frva)
            fn = fn_at(frva)
            if fn is None: continue
            gw = globals_written(fn, limit=200)
            if not gw: continue
            f.write("   fn RVA 0x%08x (str %r) writes/reads:\n" % (frva, needle))
            for site, mn, g in gw[:20]:
                f.write("      +0x%08x %-6s [0x%08x]\n" % (site, mn, g))

    # 5. ProcessEvent - large UFunction dispatcher. Anchor on the well-known
    #    "Accessed None" / script-VM strings that live inside ProcessEvent's
    #    neighbourhood, then report the biggest nearby function.
    f.write("\n## 5. ProcessEvent candidates (anchor: script-VM strings)\n")
    for needle in ["ProcessEvent", "Script call stack", "Accessed None"]:
        hits = fns_for_string(needle, maxhits=6)
        f.write("   %r -> %d ref(s)\n" % (needle, len(hits)))
        for s, frva, srva in hits[:6]:
            fn = fn_at(frva)
            sz = fn.getBody().getNumAddresses() if fn else 0
            f.write("      fn RVA 0x%08x  size=%d  (str %r)\n" % (frva, sz, s[:40]))

print("Wrote %s" % OUT)
