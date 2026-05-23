# Disassemble the functions that reference reticle/crosshair strings, to
# decide whether any is a clean visibility predicate we can hook.

OUT  = r"C:\tmp\sub2_reticle_funcs.txt"
BASE = 0x140000000

# (label, RVA) - RVAs from find_reticle_strings.py xref report.
FUNCS = [
    ("xref_ShouldReticleBeVisible", 0x010c9820),
    ("xref_Crosshairs",             0x05a09930),
    ("xref_BlockHoverTargetInfoReticle", 0x010d67b0),
    ("xref_ShouldShowHoverTargetInfoReticle_area", 0x010d67b0),
]

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

with open(OUT, "w") as f:
    for label, rva in FUNCS:
        ep = BASE + rva
        a = addr(ep)
        fn = fm.getFunctionContaining(a)
        f.write("=" * 74 + "\n")
        f.write("%s  @ 0x%x (RVA 0x%x)\n" % (label, ep, rva))
        if fn is None:
            f.write("  (no function here)\n\n")
            continue
        f.write("  fn entry 0x%x  body=%s\n" % (
            fn.getEntryPoint().getOffset(), fn.getBody().getNumAddresses()))
        ins = listing.getInstructions(fn.getBody(), True)
        count = 0
        for i in ins:
            count += 1
            if count > 120:
                f.write("  ... (truncated)\n")
                break
            rva_i = i.getAddress().getOffset() - BASE
            f.write("  +0x%08x  %s\n" % (rva_i, i.toString()))
        f.write("\n")

print("Wrote %s" % OUT)
