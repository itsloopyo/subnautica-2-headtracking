# Locate GNames (FNamePool) and GUObjectArray globals.
#
# Strategy:
#  - Disassemble the FName-intern helpers (called from static-init code that
#    builds FNames from literals) and record every absolute global they touch
#    via LEA/MOV [abs]. The FNamePool global shows up as a large .bss/.data
#    address that the helper indexes.
#  - Separately, find GUObjectArray by anchoring on the well-known UE log
#    string for object-array growth, then walking to the referencing fn and
#    recording its global accesses.

OUT  = r"C:\tmp\sub2_gnames_guobj.txt"
BASE = 0x140000000

FNAME_HELPERS = [0x141475860, 0x141475690]

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    b = mem.getBlock(a)
    return b.getName() if b else "?"

def globals_touched(ep, limit=400):
    a = addr(ep)
    fn = fm.getFunctionContaining(a)
    out = []
    if not fn:
        return None, out
    seen = set()
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > limit:
            break
        for r in ins.getReferencesFrom():
            ta = r.getToAddress()
            if ta is None:
                continue
            off = ta.getOffset()
            if off < BASE or off > BASE + 0x20000000:
                continue
            blk = block_name(ta)
            if blk in (".data", ".bss", ".rdata") and off not in seen:
                seen.add(off)
                out.append((ins.getAddress().getOffset() - BASE,
                            ins.getMnemonicString(), off - BASE, blk))
    return fn, out

# Anchor strings that sit inside functions that touch GUObjectArray.
GUOBJ_NEEDLES = [
    "Max number of UObjects", "UObject count", "GUObjectArray",
    "ObjectsToDestroy", "Object Hash", "OutOfMemory.*UObject",
    "Exceeded maximum object count",
]

with open(OUT, "w") as f:
    f.write("FName helper global accesses (hunting GNames/FNamePool)\n")
    f.write("=" * 74 + "\n")
    for ep in FNAME_HELPERS:
        fn, touched = globals_touched(ep)
        f.write("\nhelper @ RVA 0x%x  %s\n" % (ep - BASE,
                "(no fn)" if fn is None else ""))
        for site, mn, grva, blk in touched:
            f.write("  +0x%08x  %-6s -> [0x%08x] %s\n" % (site, mn, grva, blk))

    f.write("\n\n")
    f.write("GUObjectArray anchor-string search\n")
    f.write("=" * 74 + "\n")
    found = []
    for data in listing.getDefinedData(True):
        if not data.hasStringValue():
            continue
        try:
            s = str(data.getValue())
        except:
            continue
        low = s.lower()
        for n in GUOBJ_NEEDLES:
            if n.lower().replace(".*", "") in low:
                found.append((data.getAddress(), s))
                break
    for saddr, sval in found:
        f.write('\n"%s" (str RVA 0x%x)\n' % (
            sval if len(sval) <= 90 else sval[:90] + "...",
            saddr.getOffset() - BASE))
        for r in ref_mgr.getReferencesTo(saddr):
            fn = fm.getFunctionContaining(r.getFromAddress())
            if not fn:
                continue
            ep = fn.getEntryPoint().getOffset()
            _, touched = globals_touched(ep, limit=600)
            f.write("  fn RVA 0x%08x  touches %d globals\n" % (ep - BASE, len(touched)))
            for site, mn, grva, blk in touched[:25]:
                f.write("      +0x%08x  %-6s -> [0x%08x] %s\n" % (site, mn, grva, blk))

print("Wrote %s" % OUT)
