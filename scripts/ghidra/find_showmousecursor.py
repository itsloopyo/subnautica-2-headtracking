# Locate APlayerController::bShowMouseCursor's byte offset + bitmask.
#
# bShowMouseCursor is a 1-bit bitfield packed with other bools, so the
# FPropertyParams Offset alone is not enough - we also need the mask. UE5
# emits an FBoolPropertyParams whose SetBitFunc is a tiny thunk:
#     void SetBit(void* Obj) { ((Cls*)Obj)->bShowMouseCursor = 1; }
# which compiles to `or byte ptr [rcx + DISP], MASK` (or a bts). The
# displacement is the byte offset, the immediate is the bitmask.
#
# Strategy:
#   1. Find the "bShowMouseCursor" C string.
#   2. Scan .rdata for a qword == that string addr (FBoolPropertyParams start).
#   3. Dump the struct's qwords; any pointing into .text is a SetBit thunk.
#   4. Disassemble that thunk and report the `or/and/mov/bts byte [rcx+disp]`
#      displacement (offset) and immediate (mask).

from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.program.model.address import AddressSet

OUT = r"C:\tmp\sub2_showmousecursor.txt"
NAME = "bShowMouseCursor"

mem   = currentProgram.getMemory()
fact  = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_qword(a):
    try:
        return mem.getLong(a) & 0xffffffffffffffff
    except:
        return None

def find_string(s):
    target = bytearray(s + "\x00", "ascii")
    masks = bytearray([0xff] * len(target))
    hits = []
    start = currentProgram.getMinAddress()
    while True:
        found = mem.findBytes(start, bytes(target), bytes(masks), True, monitor)
        if found is None:
            break
        hits.append(found.getOffset())
        start = found.add(1)
        if len(hits) >= 16:
            break
    return hits

def find_qword_refs(target_value, want_blocks=(".rdata", ".data")):
    matches = []
    for block in mem.getBlocks():
        if block.getName() not in want_blocks:
            continue
        start = block.getStart().getOffset()
        end   = block.getEnd().getOffset()
        cur = start
        while cur + 8 <= end:
            if safe_qword(addr(cur)) == target_value:
                matches.append(cur)
                if len(matches) >= 24:
                    return matches
            cur += 8
    return matches

def disasm(start, length, f):
    a_start = addr(start)
    a_end   = addr(start + length - 1)
    DisassembleCommand(AddressSet(a_start, a_end), None, True).applyTo(currentProgram, monitor)
    cur = a_start
    while cur.getOffset() < start + length:
        ins = listing.getInstructionAt(cur)
        if ins is None:
            try:
                b = mem.getByte(cur) & 0xff
            except:
                cur = cur.add(1); continue
            f.write("      %s  .db 0x%02x\n" % (cur, b))
            cur = cur.add(1)
            continue
        f.write("      %s  %s\n" % (ins.getAddress(), ins.toString()))
        if ins.getMnemonicString().lower() in ("ret", "jmp"):
            break
        nxt = ins.getNext()
        cur = nxt.getAddress() if nxt else cur.add(ins.getLength())

with open(OUT, "w") as f:
    f.write("Search for %s\n" % NAME)
    f.write("=" * 78 + "\n\n")

    str_addrs = find_string(NAME)
    f.write("string '%s' found at: %s\n\n" % (NAME, ", ".join("0x%x" % a for a in str_addrs)))

    for sa in str_addrs:
        refs = find_qword_refs(sa)
        f.write("--- string @ 0x%x : %d struct(s) reference it ---\n" % (sa, len(refs)))
        for st in refs:
            f.write("  FPropertyParams candidate @ 0x%x  [%s]\n" % (st, block_name(addr(st))))
            # Dump first 0x60 bytes as qwords; flag .text pointers (SetBit thunks).
            for off in range(0, 0x60, 8):
                v = safe_qword(addr(st + off))
                if v is None:
                    continue
                tag = ""
                if image_base <= v < image_base + 0x10000000:
                    tag = " -> [%s]" % block_name(addr(v))
                f.write("    +0x%02x  0x%016x%s\n" % (off, v, tag))
                if tag.endswith("[.text]\n") or tag.endswith("[.text]"):
                    f.write("    disasm SetBit thunk @ 0x%x:\n" % v)
                    disasm(v, 0x40, f)
            # Also dump uint16/uint32 fields that look like plausible offsets.
            for off in range(0x10, 0x60, 2):
                try:
                    u16 = mem.getShort(addr(st + off)) & 0xffff
                except:
                    continue
                if 0x40 <= u16 <= 0x1000:
                    f.write("    u16 +0x%02x = 0x%x (%d)  plausible offset\n" % (off, u16, u16))
            f.write("\n")

print("Wrote %s" % OUT)
