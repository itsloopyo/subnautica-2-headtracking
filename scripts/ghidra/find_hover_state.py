# Locate the SN2 game-state field that distinguishes "player is hovering a
# world interactable" (world-hover prompt context) from "no hover" (button-
# bar action prompt context, eg air-bladder Inhale/Ascend). The widget
# WBP_HoverTargetInfo is reused for both roles; we need a per-frame native
# state to gate DriveTooltipMove. Most UE5 games store it on the player
# character or controller as a UObject* "HoverTarget" / "FocusedActor" /
# "InteractTarget" / similar.
#
# Strategy (mirrors find_showmousecursor.py, find_property_offsets.py):
#   1. Search the binary for plausible UPROPERTY name strings.
#   2. For each found C string, find qword refs in .rdata - those are
#      FPropertyParams structs registering that property.
#   3. Dump the struct's qwords + plausible offset fields (uint16/uint32
#      in 0x40..0x1000 range, the typical APawn/APlayerController/AHUD
#      field-offset window for SN2-build UPROPERTY layout).
#   4. Mark any qword pointing into .text as a setter/getter thunk; the
#      thunk usually starts `mov [rcx + DISP], ...` or `or byte [rcx+DISP],
#      mask` and the displacement is the byte offset.
#
# Output: C:\tmp\sub2_hover_state.txt - manually grep for plausible
# candidates and feed them through find_property_offsets.py's deeper
# disassembly for the final byte-offset extraction.

from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.program.model.address import AddressSet

OUT = r"C:\tmp\sub2_hover_state.txt"

# Candidates worth searching. Common SN2-style and stock UE5
# interaction/aim/focus property names. Bool flags too. Cast a wide
# net first - we trim once we see what hits.
CANDIDATES = [
    # World-hover / interact targeting on the pawn or controller
    "HoverTarget", "HoveredActor", "HoverActor", "HoverObject",
    "FocusedActor", "FocusActor", "FocusTarget",
    "InteractTarget", "InteractActor", "InteractObject",
    "CurrentTarget", "CurrentInteractable", "AimedAtActor",
    "AimTarget", "TargetedActor", "TargetActor",
    "WorldObject", "WorldHint", "WorldObjectHint",
    "HitActor", "HitObject", "TraceHitActor",
    "LookAtActor", "LookTarget",
    # Bool flags that often indicate "world hover prompt visible"
    "bHasHoverTarget", "bIsHovering", "bShowHoverPrompt",
    "bWorldHover", "bIsInteracting", "bCanInteract",
    "bShowWorldHint", "bHoverPromptVisible",
    # SN2-specific guesses (subnautica 1 used PlayerTool, HandReticle,
    # cameraTarget; SN2 may keep the lineage)
    "HandReticle", "PlayerTool", "ToolTarget", "CameraTarget",
    "HoldingTool", "ActiveItem", "EquippedItem",
    # The widget itself + its likely state field
    "WBP_HoverTargetInfo",
    "TargetInfo", "HintTarget", "ActiveHint",
]

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

def find_string(s, limit=8):
    target = bytearray(s + "\x00", "ascii")
    masks  = bytearray([0xff] * len(target))
    hits = []
    start = currentProgram.getMinAddress()
    while True:
        found = mem.findBytes(start, bytes(target), bytes(masks), True, monitor)
        if found is None:
            break
        hits.append(found.getOffset())
        start = found.add(1)
        if len(hits) >= limit:
            break
    return hits

def find_qword_refs(target_value, want_blocks=(".rdata", ".data"), limit=12):
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
                if len(matches) >= limit:
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
            cur = cur.add(1); continue
        f.write("      %s  %s\n" % (ins.getAddress(), ins.toString()))
        if ins.getMnemonicString().lower() in ("ret", "jmp"):
            break
        nxt = ins.getNext()
        cur = nxt.getAddress() if nxt else cur.add(ins.getLength())

with open(OUT, "w") as f:
    f.write("Hover/interact state candidate sweep\n")
    f.write("=" * 78 + "\n\n")
    f.write("Goal: find a UPROPERTY whose byte offset gives us a per-frame\n")
    f.write("'is the player hovering a world interactable' signal.\n\n")

    any_hits = False
    for name in CANDIDATES:
        sa_list = find_string(name)
        if not sa_list:
            continue
        any_hits = True
        f.write("=== '%s' ===\n" % name)
        for sa in sa_list:
            f.write("  string @ 0x%x  [%s]\n" % (sa, block_name(addr(sa))))
            refs = find_qword_refs(sa)
            if not refs:
                f.write("    (no FPropertyParams xref)\n")
                continue
            for st in refs:
                f.write("  FPropertyParams candidate @ 0x%x  [%s]\n" %
                        (st, block_name(addr(st))))
                # Dump first 0x60 bytes as qwords; flag .text pointers.
                text_thunks = []
                for off in range(0, 0x60, 8):
                    v = safe_qword(addr(st + off))
                    if v is None:
                        continue
                    tag = ""
                    if image_base <= v < image_base + 0x10000000:
                        bn = block_name(addr(v))
                        tag = " -> [%s]" % bn
                        if bn == ".text":
                            text_thunks.append(v)
                    f.write("    +0x%02x  0x%016x%s\n" % (off, v, tag))
                # Plausible UPROPERTY offset bytes
                for off in range(0x10, 0x60, 2):
                    try:
                        u16 = mem.getShort(addr(st + off)) & 0xffff
                    except:
                        continue
                    if 0x40 <= u16 <= 0x1000:
                        f.write("    u16 +0x%02x = 0x%x (%d)  plausible offset\n" %
                                (off, u16, u16))
                # Disassemble any .text thunks - getter/setter often
                # reveals the field offset as an immediate displacement.
                for thunk in text_thunks[:2]:
                    f.write("    disasm thunk @ 0x%x:\n" % thunk)
                    disasm(thunk, 0x30, f)
                f.write("\n")
        f.write("\n")

    if not any_hits:
        f.write("No candidate strings matched. Either none of the names are\n")
        f.write("present in this build, or they live in .pdata / packed UTF-16.\n")
        f.write("Widen CANDIDATES and re-run.\n")

print("Wrote %s" % OUT)
