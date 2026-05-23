# Pin FNamePool for the patched build via the FName-decode signature.
# ResolveFName does:  block = Blocks[id>>16];  len = header>>6
# so the decoder contains BOTH `SHR ,0x10` and `SHR ,6` and references the
# pool global (Blocks live at pool+0x10). Report the .bss global it touches.
# Also fully disassemble the function that references the literal 'FNamePool'
# string (flagged by rediscover at RVA 0x14769a0) - it accesses the pool too.
OUT  = r"C:\tmp\sub2_fnamepool2.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, ep, limit=120):
    fn = fm.getFunctionContaining(addr(ep))
    f.write("-"*70 + "\nfn entry RVA 0x%x  body=%s\n" % (
        ep-BASE, "?" if fn is None else fn.getBody().getNumAddresses()))
    if fn is None: return
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
                try: note = "   ; STR %r" % str(d.getValue())[:40]
                except: pass
            elif ta.getOffset() >= BASE and blk(ta) in (".data",".bss"):
                note = "   ; [0x%08x] %s" % (ta.getOffset()-BASE, blk(ta))
        f.write("  +0x%08x  %s%s\n" % (ins.getAddress().getOffset()-BASE,
                                        ins.toString(), note))

with open(OUT, "w") as f:
    # 1. Signature scan: functions with both SHR ,0x10 and SHR ,6.
    f.write("## FName decoder candidates (SHR ,0x10 AND SHR ,6 + .bss global)\n")
    for fn in fm.getFunctions(True):
        body = fn.getBody()
        if body.getNumAddresses() > 600: continue   # decoder is small
        has16 = has6 = False
        gref = None
        n = 0
        for ins in listing.getInstructions(body, True):
            n += 1
            if n > 250: break
            t = ins.toString()
            m = ins.getMnemonicString().upper()
            if m in ("SHR", "SAR"):
                tail = t.split(",")[-1].strip()
                if tail == "0x10": has16 = True
                if tail == "0x6":  has6 = True
            for r in ins.getReferencesFrom():
                ta = r.getToAddress()
                if ta and blk(ta) == ".bss":
                    gref = ta.getOffset() - BASE
        if has16 and has6:
            ep = fn.getEntryPoint().getOffset()
            f.write("\n  >>> decoder fn RVA 0x%x  bss-global=%s\n" % (
                ep - BASE, ("0x%08x" % gref) if gref is not None else "none"))
            dump_fn(f, ep, limit=90)

    # 2. The 'FNamePool'-string function.
    f.write("\n\n## Function referencing literal 'FNamePool'\n")
    for data in listing.getDefinedData(True):
        if not data.hasStringValue(): continue
        try: s = str(data.getValue())
        except: continue
        if s == "FNamePool" or ("FNamePool" in s and len(s) < 40):
            for r in ref_mgr.getReferencesTo(data.getAddress()):
                fn = fm.getFunctionContaining(r.getFromAddress())
                if fn:
                    dump_fn(f, fn.getEntryPoint().getOffset(), limit=70)
            break

print("Wrote %s" % OUT)
