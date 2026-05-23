# Re-derive the load-bearing RVAs after a game patch relocated everything.
# Robust, string-anchored where possible. Image base 0x140000000.
#
#  1. GUObjectArray.ObjObjects  - anchored on the gc.MaxObjects allocator.
#  2. FNamePool                  - anchored on the FName ANSI store helper.
#  3. GetPlayerViewPoint hunt    - string + structural candidates.

OUT  = r"C:\tmp\sub2_rediscover.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def fns_for_string(needle, want_substr=True, maxhits=8):
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

def globals_written(rva, limit=400):
    """Return list of (site_rva, mnem, global_rva) for MOV [abs],reg etc."""
    a = addr(BASE + rva)
    fn = fm.getFunctionContaining(a)
    res = []
    if not fn: return res
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
    f.write("Re-discovery after patch. image base 0x%x\n" % BASE)
    f.write("=" * 74 + "\n\n")

    # 1. GUObjectArray via gc.MaxObjects allocator (the big fn that does
    #    MUL 0x18 and chunk loop). Its global writes give ObjObjects base.
    f.write("## GUObjectArray (anchor: 'gc.MaxObjects' / 'MaxObjectsInGame')\n")
    seen = set()
    for needle in ["MaxObjectsInGame", "gc.MaxObjects"]:
        for s, frva, srva in fns_for_string(needle):
            if frva in seen: continue
            seen.add(frva)
            gw = globals_written(frva, limit=400)
            # the allocator is the large fn (many global writes clustered)
            if len(gw) < 4: continue
            f.write("  fn RVA 0x%08x  (str %r)  %d global writes\n" % (frva, needle, len(gw)))
            for site, mn, g in gw:
                f.write("      +0x%08x %-5s [0x%08x]\n" % (site, mn, g))
            f.write("\n")

    # 2. FNamePool via FName store. The store fn references the pool global
    #    twice with a nearby init-flag byte (CMP byte [flag],0).
    f.write("\n## FNamePool (anchor: structural - find via large .data global\n")
    f.write("   referenced right after a 'CMP byte [x],0 ; LEA pool' pattern)\n")
    # Heuristic: search for the init-flag pattern is hard in pure listing;
    # instead report functions referencing 'NamePool'/'FName' strings.
    for s, frva, srva in fns_for_string("FNamePool"):
        f.write("  'FNamePool' str ref: fn RVA 0x%08x\n" % frva)
    for s, frva, srva in fns_for_string("Max FName"):
        f.write("  'Max FName' str ref: fn RVA 0x%08x\n" % frva)

    # 3. GetPlayerViewPoint string hunt.
    f.write("\n## GetPlayerViewPoint / camera method strings\n")
    for needle in ["GetPlayerViewPoint", "GetActorEyesViewPoint",
                   "PlayerCameraManager", "CalcCamera", "GetCameraViewPoint"]:
        hits = fns_for_string(needle)
        f.write("  %r -> %d ref(s)\n" % (needle, len(hits)))
        for s, frva, srva in hits[:6]:
            f.write("      fn RVA 0x%08x   (str rva 0x%x  %r)\n" % (
                frva, srva, s if len(s) <= 60 else s[:60]+"..."))

print("Wrote %s" % OUT)
