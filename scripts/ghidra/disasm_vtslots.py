# Disassemble the controller-vtable slot RVAs dumped by the PE-probe and flag
# the real UObject::ProcessEvent(this, UFunction* RDX, void* Parms R8): 3-arg,
# tests Function->FunctionFlags & FUNC_Native via [RDX+0xb0]&0x400, no R9 flags
# arg. Slot 76 (0x3b3a1d0) is suspected to be an AActor override, not PE.
OUT  = r"C:\tmp\sub2_vtslots.txt"
BASE = 0x140000000
SLOTS = [
    (72, 0x01629e50), (73, 0x014f7c10), (74, 0x014f7aa0), (75, 0x014f7aa0),
    (76, 0x03b3a1d0), (77, 0x03b30630), (78, 0x03b236d0), (79, 0x014fa030),
    (80, 0x014f7c10),
]

fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

def n_callers(ep):
    s = set()
    for r in ref_mgr.getReferencesTo(addr(BASE+ep)):
        if r.getReferenceType().isCall(): s.add(r.getFromAddress().getOffset())
    return len(s)

with open(OUT, "w") as f:
    done = set()
    for slot, rva in SLOTS:
        if rva in done:
            f.write("\n=== vt[%d] RVA 0x%08x (same as earlier) ===\n" % (slot, rva)); continue
        done.add(rva)
        fn = fm.getFunctionContaining(addr(BASE+rva))
        sz = fn.getBody().getNumAddresses() if fn else 0
        f.write("\n=== vt[%d] RVA 0x%08x  body=%d  callers=%d ===\n" % (
            slot, rva, sz, n_callers(rva)))
        if fn is None: continue
        uses_r9 = has_native = indirect = False
        n = 0
        for ins in listing.getInstructions(fn.getBody(), True):
            n += 1
            if n > 45: break
            t = ins.toString()
            if "R9" in t: uses_r9 = True
            if "0x400" in t: has_native = True
            if ins.getMnemonicString().upper()=="CALL" and "qword" in t.lower() and "[" in t:
                indirect = True
            f.write("  +0x%08x  %s\n" % (ins.getAddress().getOffset()-BASE, t))
        f.write("  -> uses_R9=%s tests_0x400=%s indirect_call=%s\n" % (
            uses_r9, has_native, indirect))
print("Wrote %s" % OUT)
