# Disassemble the class-construction thunk referenced by the FClassParams
# struct that owns bIsHovering, and extract the class-name string it
# LEAs into RDX (the Z_Construct_UClass_<Class>_Statics::NewClass pattern
# emits `lea rdx, [class_name_string]` then `call uobject_register`).

from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.program.model.address import AddressSet

OUT   = r"C:\tmp\sub2_isHovering_thunk.txt"
THUNK = 0x1465241f0  # qword at FClassParams +0x10 (a.k.a. xref -0x28)

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_byte(a):
    try:
        return mem.getByte(a) & 0xff
    except:
        return None

def safe_qword(a):
    try:
        return mem.getLong(a) & 0xffffffffffffffff
    except:
        return None

def safe_bytes(a, n):
    out = bytearray()
    for i in range(n):
        b = safe_byte(addr(a + i))
        if b is None:
            break
        out.append(b)
    return bytes(out)

def read_cstring(a, maxlen=512):
    b = safe_bytes(a, maxlen)
    end = b.find(b"\x00")
    if end < 0:
        return None
    try:
        return b[:end].decode("ascii", errors="replace")
    except:
        return None

with open(OUT, "w") as f:
    f.write("Trace bIsHovering UClass construction thunk @ 0x%x\n" % THUNK)
    f.write("=" * 78 + "\n\n")

    a_start = addr(THUNK)
    a_end   = addr(THUNK + 0x200 - 1)
    DisassembleCommand(AddressSet(a_start, a_end), None, True).applyTo(currentProgram, monitor)

    cur = a_start
    end_addr = THUNK + 0x200
    while cur.getOffset() < end_addr:
        ins = listing.getInstructionAt(cur)
        if ins is None:
            b = safe_byte(cur)
            f.write("  %s  .db 0x%02x\n" % (cur, b if b is not None else 0))
            cur = cur.add(1)
            continue
        s = "  %s  %s" % (ins.getAddress(), ins.toString())
        # For LEAs of .rdata targets, decode the string at the destination.
        if ins.getMnemonicString().lower() == "lea":
            for op_i in range(ins.getNumOperands()):
                refs = ins.getOperandReferences(op_i)
                for r in refs:
                    ta = r.getToAddress()
                    if ta is None:
                        continue
                    cs = read_cstring(ta.getOffset())
                    if cs and len(cs) >= 3 and all(0x20 <= ord(c) < 0x7f for c in cs):
                        s += '   ; "%s"' % cs[:80]
        f.write(s + "\n")
        if ins.getMnemonicString().lower() in ("ret", "jmp"):
            break
        nxt = ins.getNext()
        cur = nxt.getAddress() if nxt else cur.add(ins.getLength())

print("Wrote %s" % OUT)
