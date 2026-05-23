# Find UObject::ProcessEvent by signature:
#  - calls the stack-probe 0x149667b90 with a VARIABLE size (alloca of
#    Function->PropertiesSize), not a fixed immediate, and
#  - tests a loaded value against 0x400 (FUNC_Native) somewhere in the body.

OUT   = r"C:\tmp\sub2_pe_v2.txt"
BASE  = 0x140000000
PROBE = 0x149667b90

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

# Collect functions that call the probe.
fns = {}
for r in ref_mgr.getReferencesTo(addr(PROBE)):
    if not r.getReferenceType().isCall():
        continue
    fn = fm.getFunctionContaining(r.getFromAddress())
    if fn:
        fns.setdefault(fn.getEntryPoint().getOffset(), []).append(
            r.getFromAddress().getOffset())

def analyze(ep):
    fn = fm.getFunctionContaining(addr(ep))
    if not fn: return None
    variable_alloca = False
    has_400 = False
    probe_calls = 0
    prev = [None, None, None]
    for ins in listing.getInstructions(fn.getBody(), True):
        s = ins.toString()
        # detect call to probe + whether preceding EAX/RAX/ECX load was immediate
        if "CALL" in s and ("%x" % PROBE)[-6:] in s.lower().replace("0x",""):
            pass
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta and ta.getOffset() == PROBE and r.getReferenceType().isCall():
                probe_calls += 1
                # was the size (EAX/RAX) set by an immediate just before?
                imm = False
                for p in prev:
                    if p and (p.startswith("MOV EAX,0x") or p.startswith("MOV RAX,0x")
                              or p.startswith("MOV ECX,0x")):
                        imm = True
                if not imm:
                    variable_alloca = True
        if ("AND" in s or "TEST" in s) and "0x400" in s:
            has_400 = True
        prev = [s, prev[0], prev[1]]
    return (variable_alloca, has_400, probe_calls, fn.getBody().getNumAddresses())

with open(OUT, "w") as f:
    f.write("functions calling stack-probe 0x%x: %d\n" % (PROBE, len(fns)))
    f.write("=" * 70 + "\n")
    cands = []
    for ep in fns:
        a = analyze(ep)
        if a is None: continue
        var, has400, ncalls, size = a
        if var or has400:
            cands.append((ep, var, has400, size))
    # ProcessEvent: variable alloca AND tests 0x400. Rank those first.
    cands.sort(key=lambda c: (not (c[1] and c[2]), not c[2], -c[3]))
    for ep, var, has400, size in cands[:25]:
        f.write("  fn RVA 0x%08x  varAlloca=%-5s test0x400=%-5s size=%d\n" % (
            ep - BASE, str(var), str(has400), size))

print("Wrote %s  (%d call-sites, candidates listed)" % (OUT, len(fns)))
