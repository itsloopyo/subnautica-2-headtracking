#!/usr/bin/env python3
"""Generate scripts/signatures.local.json from your own copy of the game.

derive_rvas.py and derive_globals.py relocate functions in a patched EXE by
matching the prologue of the same function in a build that already works. Those
prologue bytes are game code, so they are generated locally and never committed.

Run this against the newest build the mod already supports, then run the derive
scripts against the patched EXE:

    py -3 scripts/make_signatures.py "...\\Subnautica2-Win64-Shipping.exe"
    py -3 scripts/derive_rvas.py    "...\\patched\\Subnautica2-Win64-Shipping.exe"
    py -3 scripts/derive_globals.py "...\\patched\\Subnautica2-Win64-Shipping.exe"

Bytes that move when the linker relocates code - rip-relative displacements and
call/jmp rel32 targets - are masked to `??`, so a signature stays valid across
builds that only shuffle addresses.

Reference RVAs default to steam-win64-20260820; every build profile records its
own in the comments above the offset table in builds/steam_offsets.cpp. Override
with --gpv / --allocator / --decoder when generating from a different build.
"""
import argparse
import json
import os
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

import signatures

# steam-win64-20260820. See the comments in builds/steam_offsets.cpp: the
# allocator is the function whose `mov [rip], rax` stores GUObjectArray's
# ObjObjects, the decoder is the first of the FName decoder pair.
DEFAULT_RVAS = {
    "gpv_prologue": 0x043eaa00,
    "objobjects_allocator": 0x016eadf0,
    "fname_decoder": 0x01479bc0,
}

# Minimum prologue length that makes each signature unique. The signature is
# extended to the end of the instruction that crosses this mark, so it never
# stops mid-instruction and the mask stays meaningful.
MIN_SIG_LENGTHS = {
    "gpv_prologue": 32,
    "objobjects_allocator": 42,
    "fname_decoder": 30,
}

# Read enough to disassemble past the minimum even if every instruction is long.
READ_AHEAD = 16


def masked_signature(code, rva, minimum):
    """Disassemble `code` at `rva`, masking bytes that move when code relocates."""
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    mask = []
    consumed = 0
    for ins in md.disasm(code, rva):
        enc = ins.encoding
        rip_relative = any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP
                           for op in ins.operands)
        wild = set()
        if rip_relative and enc.disp_size:
            wild.update(range(enc.disp_offset, enc.disp_offset + enc.disp_size))
        if ins.mnemonic in ("call", "jmp") and enc.imm_size:
            wild.update(range(enc.imm_offset, enc.imm_offset + enc.imm_size))
        mask.extend(i in wild for i in range(ins.size))
        consumed += ins.size
        if consumed >= minimum:
            break
    if consumed < minimum:
        raise SystemExit(
            "disassembly stopped after %d of %d bytes at RVA 0x%08x - the "
            "reference RVA is probably not a function start" % (consumed, minimum, rva))
    return " ".join("??" if mask[i] else "%02x" % code[i] for i in range(consumed))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", help="known-good Subnautica 2 shipping EXE")
    ap.add_argument("--gpv", dest="gpv_prologue", type=lambda v: int(v, 0),
                    default=DEFAULT_RVAS["gpv_prologue"],
                    help="RVA of GetPlayerViewPoint (default 0x%08x)"
                         % DEFAULT_RVAS["gpv_prologue"])
    ap.add_argument("--allocator", dest="objobjects_allocator", type=lambda v: int(v, 0),
                    default=DEFAULT_RVAS["objobjects_allocator"],
                    help="RVA of the FUObjectArray allocator (default 0x%08x)"
                         % DEFAULT_RVAS["objobjects_allocator"])
    ap.add_argument("--decoder", dest="fname_decoder", type=lambda v: int(v, 0),
                    default=DEFAULT_RVAS["fname_decoder"],
                    help="RVA of an FName decoder (default 0x%08x)"
                         % DEFAULT_RVAS["fname_decoder"])
    args = ap.parse_args()

    pe = pefile.PE(args.exe, fast_load=True)
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    tva = text.VirtualAddress
    data = text.get_data()
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
    print("EXE: %s" % args.exe)
    print("PE:  ts=0x%08x size=0x%08x csum=0x%08x"
          % (fh.TimeDateStamp, oh.SizeOfImage, oh.CheckSum))

    out = {}
    for name, minimum in MIN_SIG_LENGTHS.items():
        rva = getattr(args, name)
        off = rva - tva
        if off < 0 or off + minimum + READ_AHEAD > len(data):
            raise SystemExit("RVA 0x%08x for %s is outside .text" % (rva, name))
        out[name] = masked_signature(data[off:off + minimum + READ_AHEAD], rva, minimum)
        print("  %-22s @ 0x%08x  %s" % (name, rva, out[name]))
    pe.close()

    with open(signatures.SIGNATURE_FILE, "w") as f:
        json.dump(out, f, indent=2, sort_keys=True)
        f.write("\n")
    print("\nWrote %s" % signatures.SIGNATURE_FILE)
    print("This file holds game code. It is gitignored - keep it that way.")


if __name__ == "__main__":
    sys.exit(main())
