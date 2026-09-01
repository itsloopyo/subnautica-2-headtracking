#!/usr/bin/env python3
"""Read UPROPERTY byte offsets out of a patched SN2 EXE's UECodeGen tables.

The 2026-08-20 patch inserted 0x10 bytes into AActor, shifting every field past
RootComponent in AActor and in each of its subclasses. Carrying the previous
build's offsets forward read garbage at each one, silently: bShowMouseCursor at
the stale offset reads 0, so tracking never suppressed in menus and nothing
crashed to point at it. So the struct offsets get re-read every patch, not
carried.

UE5 emits a static F*PropertyParams per UPROPERTY, starting with a
`const char* NameUTF8`. Find the name string, find the .rdata qwords pointing at
it, and the params struct sits there: ArrayDim at +0x30 (u16), Offset at +0x32
(u16). A bool bitfield has no Offset - it carries a SetBitFunc pointer at +0x38
whose whole body is `or dword [rcx+OFFSET], MASK`, giving both.

A short name like Pawn names a property on dozens of classes, so the name alone
picks out ~50 params structs. The owning class is what disambiguates, and it is
recoverable without knowing where the class's own metadata lives: UE emits one
PropPointers array per class, a run of qwords pointing at that class's params
structs and no others. A candidate is the right one when the pointer array
holding it also holds a params struct for a property only that class declares.

APlayerController::PlayerCameraManager is the control: GetPlayerViewPoint
dereferences it directly, so its property-table offset must equal the
displacement in GPV's own `mov rcx,[rbx+disp]`. If those disagree the table read
is wrong and nothing below it should be trusted.

Usage: py -3 derive_struct_offsets.py <path-to-exe> [--gpv 0xRVA]
"""
import argparse

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM

# (property, owning class, siblings declared on that same class). The siblings
# only have to be co-declared - their own offsets are never read.
PROPS = [
    ("PlayerCameraManager", "APlayerController", ["PlayerInput", "bShowMouseCursor"]),
    ("Pawn", "AController", ["PlayerState", "ControlRotation"]),
    ("RootComponent", "AActor", ["PrimaryActorTick", "bReplicates"]),
    ("CapsuleComponent", "ACharacter", ["CharacterMovement", "JumpMaxCount"]),
]
BOOL_PROPS = [
    ("bShowMouseCursor", "APlayerController", ["PlayerInput", "PlayerCameraManager"]),
]

ARRAYDIM_OFF = 0x30
OFFSET_OFF = 0x32
SETBITFUNC_OFF = 0x38
# How far either side of a PropPointers slot to look for a sibling's slot.
PROP_ARRAY_SPAN = 0x200

ap = argparse.ArgumentParser()
ap.add_argument("exe")
ap.add_argument("--gpv", type=lambda v: int(v, 0), default=None,
                help="GetPlayerViewPoint RVA, for the PlayerCameraManager control")
args = ap.parse_args()

pe = pefile.PE(args.exe, fast_load=True)
IMAGE_BASE = pe.OPTIONAL_HEADER.ImageBase
SECTIONS = [(s.Name.rstrip(b"\x00").decode(), s.VirtualAddress, s.get_data())
            for s in pe.sections]
RDATA = next(s for s in SECTIONS if s[0] == ".rdata")

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True


def section_for_rva(rva):
    for name, base, data in SECTIONS:
        if base <= rva < base + len(data):
            return name, base, data
    return None


def read_at(rva, n):
    hit = section_for_rva(rva)
    if hit is None:
        return None
    _, base, data = hit
    return data[rva - base: rva - base + n]


def u16(rva):
    b = read_at(rva, 2)
    return int.from_bytes(b, "little") if b else None


def find_cstring(name):
    """RVAs of the NUL-terminated ASCII name in read-only data."""
    target = name.encode() + b"\x00"
    out = []
    for sec, base, data in SECTIONS:
        if sec not in (".rdata", ".data"):
            continue
        start = 0
        while True:
            i = data.find(target, start)
            if i == -1:
                break
            if i == 0 or data[i - 1] in (0, 0x20):  # not a suffix of a longer name
                out.append(base + i)
            start = i + 1
    return out


def find_qword_refs(target_rva):
    """Aligned .rdata slots holding a qword == target VA."""
    want = (IMAGE_BASE + target_rva).to_bytes(8, "little")
    _, base, data = RDATA
    out, start = [], 0
    while True:
        i = data.find(want, start)
        if i == -1:
            break
        if i % 8 == 0:
            out.append(base + i)
        start = i + 1
    return out


def params_for(name):
    """Every plausible F*PropertyParams naming this property."""
    out = []
    for s in find_cstring(name):
        for p in find_qword_refs(s):
            if u16(p + ARRAYDIM_OFF) == 1:
                out.append(p)
    return out


def pick_by_siblings(name, siblings):
    """The params structs of name whose PropPointers array holds every sibling."""
    sibling_params = [set(params_for(sib)) for sib in siblings]
    winners = []
    for p in params_for(name):
        for slot in find_qword_refs(p):
            lo, hi = slot - PROP_ARRAY_SPAN, slot + PROP_ARRAY_SPAN
            neighbours = set()
            for q in range(max(RDATA[1], lo), hi, 8):
                v = read_at(q, 8)
                if v:
                    neighbours.add(int.from_bytes(v, "little") - IMAGE_BASE)
            if all(neighbours & sib for sib in sibling_params):
                winners.append(p)
                break
    return winners


def report(label, cls, values, fmt):
    if len(values) == 1:
        print("  %-22s %-20s %s" % (label, cls, fmt(values[0])))
        return values[0]
    print("  %-22s %-20s %s" % (label, cls,
          ("AMBIGUOUS: " + ", ".join(fmt(v) for v in sorted(values)))
          if values else "NOT FOUND"))
    return None


fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
print("EXE:", args.exe)
print("PE: ts=0x%08x size=0x%08x csum=0x%08x"
      % (fh.TimeDateStamp, oh.SizeOfImage, oh.CheckSum))
print()
print("== UPROPERTY offsets (UECodeGen params, class pinned by PropPointers) ==")
found = {}
for name, cls, siblings in PROPS:
    offs = {u16(p + OFFSET_OFF) for p in pick_by_siblings(name, siblings)}
    found[name] = report(name, cls, sorted(offs), lambda v: "0x%03x" % v)

for name, cls, siblings in BOOL_PROPS:
    vals = set()
    for p in pick_by_siblings(name, siblings):
        ptr = read_at(p + SETBITFUNC_OFF, 8)
        if not ptr:
            continue
        fn = int.from_bytes(ptr, "little") - IMAGE_BASE
        hit = section_for_rva(fn)
        if hit is None or hit[0] != ".text":
            continue
        _, base, data = hit
        for ins in md.disasm(data[fn - base: fn - base + 0x20], fn):
            if ins.mnemonic not in ("or", "bts", "mov", "and"):
                continue
            dst, src = ins.operands
            if dst.type == X86_OP_MEM and src.type == X86_OP_IMM:
                vals.add((dst.mem.disp, src.imm))
            break
    found[name] = report(name, cls, sorted(vals),
                         lambda v: "0x%03x mask 0x%x" % v)

if args.gpv is not None:
    print()
    print("== control: PlayerCameraManager as GetPlayerViewPoint dereferences it ==")
    _, base, data = next(s for s in SECTIONS if s[0] == ".text")
    disps = []
    for ins in md.disasm(data[args.gpv - base: args.gpv - base + 0x120], args.gpv):
        if ins.mnemonic == "mov" and ins.operands[1].type == X86_OP_MEM:
            d = ins.operands[1].mem.disp
            if 0x100 <= d < 0x1000:
                disps.append((ins.address, d, ins.op_str))
    for addrv, d, txt in disps[:6]:
        mark = "  <- matches the property table" if found.get("PlayerCameraManager") == d else ""
        print("  gpv+0x%03x  mov %s%s" % (addrv - args.gpv, txt, mark))
    if found.get("PlayerCameraManager") not in [d for _, d, _ in disps]:
        raise SystemExit("CONTROL FAILED: PlayerCameraManager is not dereferenced in "
                         "GPV; the property-table read is wrong, do not use these "
                         "offsets.")
    print("  control OK")
pe.close()
