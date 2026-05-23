# Force-disassemble an address range and dump the instructions. Use this
# when follow_class_registration points at a .text address that Ghidra
# didn't auto-analyse as a function.

from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.program.model.address import AddressSet

OUT = r"C:\tmp\sub2_disasm_range.txt"

RANGES = [
    ("AUWEPlayerCameraManager_Statics_block", 0x1463040d0, 0x1000),
]

fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
mem = currentProgram.getMemory()
sym_tab = currentProgram.getSymbolTable()
fm = currentProgram.getFunctionManager()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def annotate(t):
    if t is None: return ""
    bn = block_name(t)
    sym = sym_tab.getPrimarySymbol(t)
    sname = sym.getName() if sym else "-"
    fn = fm.getFunctionContaining(t)
    fname = " fn=%s" % fn.getName() if fn else ""
    return "  ; -> 0x%x [%s] %s%s" % (t.getOffset(), bn, sname, fname)

with open(OUT, "w") as f:
    for label, start, length in RANGES:
        f.write("=" * 70 + "\n")
        f.write("%s  @ 0x%x  (%d bytes)\n\n" % (label, start, length))
        a_start = addr(start)
        a_end   = addr(start + length - 1)
        # Force disassembly.
        cmd = DisassembleCommand(AddressSet(a_start, a_end), None, True)
        cmd.applyTo(currentProgram, monitor)

        cur = a_start
        while cur.getOffset() < start + length:
            ins = listing.getInstructionAt(cur)
            if ins is None:
                # Skip padding (likely INT3 = 0xcc) until next instruction.
                try:
                    b = mem.getByte(cur) & 0xff
                except:
                    cur = cur.add(1); continue
                f.write("  %s  .db 0x%02x\n" % (cur, b))
                cur = cur.add(1)
                continue
            ops_text = ins.toString()
            extras = ""
            for i in range(ins.getNumOperands()):
                for r in ins.getOperandReferences(i):
                    t = r.getToAddress()
                    if t is not None and image_base <= t.getOffset() < image_base + 0x10000000:
                        extras += annotate(t)
            f.write("  %s  %s%s\n" % (ins.getAddress(), ops_text, extras))
            nxt = ins.getNext()
            cur = nxt.getAddress() if nxt else cur.add(ins.getLength())
        f.write("\n")

print("Wrote %s" % OUT)
