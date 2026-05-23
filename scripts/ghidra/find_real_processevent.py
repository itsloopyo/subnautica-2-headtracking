# 0x16b6510 (the "Script call stack" fn, fixed 0x2078 frame) is likely the
# callstack logger, NOT ProcessEvent. Real UObject::ProcessEvent does a
# VARIABLE alloca(Function->PropertiesSize) and is virtual. Find it:
#  1. dump 0x16b6510 fully + list its callers (ProcessEvent likely calls it),
#  2. for each caller, note frame style (variable chkstk = ProcessEvent-shape).

OUT  = r"C:\tmp\sub2_real_pe.txt"
BASE = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

def dump_fn(f, label, ep, limit=70):
    a = addr(ep)
    fn = fm.getFunctionContaining(a)
    f.write("=" * 72 + "\n%s @ 0x%x (RVA 0x%x)  size=%s\n" % (
        label, ep, ep - BASE, "?" if fn is None else fn.getBody().getNumAddresses()))
    if fn is None: return None
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        f.write("  +0x%08x  %s\n" % (ins.getAddress().getOffset()-BASE, ins.toString()))
    return fn

with open(OUT, "w") as f:
    logger = BASE + 0x16b6510
    dump_fn(f, "the_0x16b6510_fn", logger, limit=40)

    # Callers of 0x16b6510 - ProcessEvent is a prime candidate.
    f.write("\n## callers of 0x16b6510:\n")
    callers = set()
    for r in ref_mgr.getReferencesTo(addr(logger)):
        if not r.getReferenceType().isCall():
            continue
        fn = fm.getFunctionContaining(r.getFromAddress())
        if fn:
            callers.add(fn.getEntryPoint().getOffset())
    for ep in sorted(callers):
        fn = fm.getFunctionContaining(addr(ep))
        # detect variable-frame (ProcessEvent shape): look for "SUB RSP,RAX"
        var_frame = False
        ndump = 0
        for ins in listing.getInstructions(fn.getBody(), True):
            ndump += 1
            if ndump > 60: break
            s = ins.toString()
            if s == "SUB RSP,RAX" or "SUB RSP,R" in s:
                var_frame = True
        f.write("  caller RVA 0x%08x  size=%d  varframe=%s\n" % (
            ep - BASE, fn.getBody().getNumAddresses(), var_frame))

    # Also dump the largest caller (likely ProcessEvent) in full-ish.
    if callers:
        biggest = max(callers, key=lambda e: fm.getFunctionContaining(addr(e)).getBody().getNumAddresses())
        dump_fn(f, "biggest_caller(likely ProcessEvent)", biggest, limit=90)

print("Wrote %s" % OUT)
