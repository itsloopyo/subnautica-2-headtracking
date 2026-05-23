# Disassemble the UpdateComponentToWorld candidates found by
# find_updatecomponenttoworld.py and annotate the AttachParent read (+0x120)
# and ComponentToWorld writes (+0x1f0..+0x230) plus call targets, so we can
# identify which is USceneComponent::UpdateComponentToWorldWithParent.

from ghidra.program.model.scalar import Scalar

OUT = r"C:\tmp\sub2_uctw_candidates.txt"

CANDIDATES = [0x143e35d50, 0x143e42650, 0x143e6d410, 0x143e1b2b0, 0x143e42ff0]

CTW_DISPS = set(range(0x1f0, 0x238, 8))
ATTACH_PARENT = 0x120

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
sym_tab = currentProgram.getSymbolTable()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def fn_name_at(v):
    fn = fm.getFunctionContaining(addr(v))
    return fn.getName() if fn else "?"

with open(OUT, "w") as f:
    for cand in CANDIDATES:
        fn = fm.getFunctionAt(addr(cand))
        f.write("=" * 80 + "\n")
        if fn is None:
            f.write("0x%x : NO FUNCTION\n\n" % cand)
            continue
        f.write("fn @ 0x%x  name=%s  size=%d bytes\n\n" %
                (cand, fn.getName(), fn.getBody().getNumAddresses()))
        n = 0
        for ins in listing.getInstructions(fn.getBody(), True):
            n += 1
            if n > 500:
                f.write("  ... (truncated)\n")
                break
            note = ""
            for i in range(ins.getNumOperands()):
                for obj in ins.getOpObjects(i):
                    if isinstance(obj, Scalar):
                        val = obj.getUnsignedValue()
                        if val in CTW_DISPS:
                            note += "  <<< CTW+0x%x" % val
                        elif val == ATTACH_PARENT:
                            note += "  <<< AttachParent(+0x120)"
            mnem = ins.getMnemonicString().lower()
            if mnem == "call":
                tgt = None
                for i in range(ins.getNumOperands()):
                    for r in ins.getOperandReferences(i):
                        tgt = r.getToAddress()
                if tgt is not None:
                    note += "  -> %s @ 0x%x" % (fn_name_at(tgt.getOffset()), tgt.getOffset())
            f.write("  0x%x  %s%s\n" % (ins.getAddress().getOffset(), ins.toString(), note))
        f.write("\n")

print("Wrote %s" % OUT)
