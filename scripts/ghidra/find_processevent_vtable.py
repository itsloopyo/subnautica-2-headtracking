# ProcessEvent is VIRTUAL (in every UObject vtable); ProcessInternal et al are
# not. Scan the script-processing neighbourhood (~0x524xxxx) for functions that
# (a) are referenced from .rdata (vtable membership) and (b) read a uint16 from
# an arg at +0xb6 (PropertiesSize alloca). That intersection is ProcessEvent.

OUT  = r"C:\tmp\sub2_pe_vtable.txt"
IMG  = 0x140000000

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def blk(a):
    b = mem.getBlock(a); return b.getName() if b else "?"

def in_vtable(ep):
    # referenced from .rdata = appears in a vtable
    cnt = 0
    for r in ref_mgr.getReferencesTo(addr(ep)):
        fa = r.getFromAddress()
        if blk(fa) == ".rdata":
            cnt += 1
    return cnt

def reads_b6(ep, limit=120):
    fn = fm.getFunctionContaining(addr(ep))
    if not fn: return False
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit: break
        s = ins.toString()
        if "0xb6" in s and ("MOVZX" in s or "word" in s):
            return True
    return False

# Walk all functions in the script-processing neighbourhood.
candidates = []
fnIter = fm.getFunctions(True)
for fn in fnIter:
    ep = fn.getEntryPoint().getOffset()
    rva = ep - IMG
    if rva < 0x5230000 or rva > 0x5250000:
        continue
    vt = in_vtable(ep)
    b6 = reads_b6(ep)
    if vt > 0 or b6:
        candidates.append((rva, vt, b6, fn.getBody().getNumAddresses()))

with open(OUT, "w") as f:
    f.write("script-processing neighbourhood candidates (0x523xxxx-0x525xxxx)\n")
    f.write("=" * 70 + "\n")
    f.write("(virtual = in .rdata vtable; b6 = reads PropertiesSize@0xb6)\n\n")
    # ProcessEvent: virtual AND reads 0xb6 -> top
    candidates.sort(key=lambda c: (not (c[1] > 0 and c[2]), -c[1]))
    for rva, vt, b6, size in candidates[:30]:
        mark = "  <-- ProcessEvent?" if (vt > 0 and b6) else ""
        f.write("  RVA 0x%08x  vtableRefs=%d  reads0xb6=%s  size=%d%s\n" % (
            rva, vt, str(b6), size, mark))

print("Wrote %s  (%d candidates)" % (OUT, len(candidates)))
