# Find UWidget visual-property offsets (RenderTransform, RenderOpacity,
# Visibility) from their setter implementations, plus UObject::ProcessEvent
# (needed to push UMG property changes to Slate each frame).

OUT  = r"C:\tmp\sub2_uwidget.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def dump_fn(f, label, rva, limit=50):
    a = addr(BASE + rva)
    fn = fm.getFunctionContaining(a)
    f.write("-" * 70 + "\n%s @ RVA 0x%x  %s\n" % (label, rva,
        "(no fn)" if fn is None else "size=%d" % fn.getBody().getNumAddresses()))
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

def fns_for_exact(name, maxhits=10):
    out = []
    for data in listing.getDefinedData(True):
        if not data.hasStringValue(): continue
        try: s = str(data.getValue())
        except: continue
        if s != name: continue
        for r in ref_mgr.getReferencesTo(data.getAddress()):
            fn = fm.getFunctionContaining(r.getFromAddress())
            if fn: out.append(fn.getEntryPoint().getOffset()-BASE)
        if out: break
    return out[:maxhits]

with open(OUT, "w") as f:
    # Setter UFUNCTION names -> exec thunk fn. The exec thunk
    # (execSetRenderOpacity) calls the _Implementation which writes the field.
    for nm in ["SetRenderOpacity", "SetRenderTransform", "SetRenderTransformPivot",
               "SetRenderTransformAngle", "SetRenderScale", "SetVisibility",
               "GetRenderOpacity", "SetRenderTranslation"]:
        f.write("\n### UFUNCTION %r\n" % nm)
        for frva in fns_for_exact(nm):
            dump_fn(f, "thunk/impl for %s" % nm, frva, limit=40)

    # ProcessEvent: anchor on the canonical fatal string.
    f.write("\n\n### ProcessEvent anchor strings\n")
    for anchor in ["Script Stack", "Script call stack", "ProcessEvent",
                   "Accessed None"]:
        for data in listing.getDefinedData(True):
            if not data.hasStringValue(): continue
            try: s = str(data.getValue())
            except: continue
            if anchor.lower() in s.lower():
                refs = list(ref_mgr.getReferencesTo(data.getAddress()))
                for r in refs[:3]:
                    fn = fm.getFunctionContaining(r.getFromAddress())
                    if fn:
                        f.write("  %r -> fn RVA 0x%08x\n" % (
                            anchor, fn.getEntryPoint().getOffset()-BASE))
                break

print("Wrote %s" % OUT)
