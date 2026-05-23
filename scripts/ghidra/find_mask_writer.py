# The hardware watchpoint proved the mask's ComponentToWorld is written by a
# single instruction at RVA 0x3e6e42c (VA 0x143e6e42c). Identify the containing
# function: entry (hook target), size, prologue (this/arg regs), the write-site
# context, and callers - so we can hook it safely and re-apply mask comp in its
# post as the guaranteed last writer.

from ghidra.program.model.scalar import Scalar

OUT = r"C:\tmp\sub2_mask_writer.txt"
WRITE_VA = 0x143e6e42c

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
ref_mgr = currentProgram.getReferenceManager()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def fn_name_at(v):
    fn = fm.getFunctionContaining(addr(v))
    return fn.getName() if fn else "?"

with open(OUT, "w") as f:
    wa = addr(WRITE_VA)
    fn = fm.getFunctionContaining(wa)
    if fn is None:
        f.write("No function contains 0x%x\n" % WRITE_VA)
    else:
        entry = fn.getEntryPoint().getOffset()
        f.write("Write-site 0x%x is in function:\n" % WRITE_VA)
        f.write("  entry = 0x%x  (RVA 0x%x)\n" % (entry, entry - image_base))
        f.write("  name  = %s\n" % fn.getName())
        f.write("  size  = %d bytes\n\n" % fn.getBody().getNumAddresses())

        # Prologue (first 30 instructions): reveals this/arg register moves.
        f.write("--- prologue ---\n")
        ins = listing.getInstructionAt(addr(entry))
        n = 0
        while ins is not None and n < 30:
            f.write("  0x%x  %s\n" % (ins.getAddress().getOffset(), ins.toString()))
            ins = ins.getNext()
            n += 1

        # Context around the write site (+/- 16 instructions).
        f.write("\n--- write-site context ---\n")
        cur = wa
        for _ in range(16):
            prev = listing.getInstructionBefore(cur)
            if prev is None:
                break
            cur = prev.getAddress()
        n = 0
        ins = listing.getInstructionAt(cur)
        while ins is not None and n < 34:
            mark = "  <<< WRITE" if ins.getAddress().getOffset() == WRITE_VA else ""
            tgt = ""
            if ins.getMnemonicString().lower() == "call":
                for i in range(ins.getNumOperands()):
                    for r in ins.getOperandReferences(i):
                        t = r.getToAddress()
                        if t is not None:
                            tgt = "  -> %s @ 0x%x" % (fn_name_at(t.getOffset()), t.getOffset())
            f.write("  0x%x  %s%s%s\n" % (ins.getAddress().getOffset(), ins.toString(), tgt, mark))
            ins = ins.getNext()
            n += 1

        # Callers of this function.
        f.write("\n--- callers (xrefs to entry) ---\n")
        cnt = 0
        for r in ref_mgr.getReferencesTo(addr(entry)):
            fa = r.getFromAddress()
            cf = fm.getFunctionContaining(fa)
            f.write("  from 0x%x (RVA 0x%x)  in %s\n" % (
                fa.getOffset(), fa.getOffset() - image_base,
                cf.getName() if cf else "?"))
            cnt += 1
            if cnt >= 20:
                break
        if cnt == 0:
            f.write("  (none - likely called only virtually / via vtable)\n")

print("Wrote %s" % OUT)
