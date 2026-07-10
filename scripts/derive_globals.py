#!/usr/bin/env python3
"""Relocate GUObjectArray.ObjObjects and FNamePool for a patched SN2 EXE.

Locates the FUObjectArray allocator and the FName decoder by masked byte
signatures (taken from the old build's relocation-free prologue bytes), then
capstone-decodes each to read the rip-relative global it references:

  ObjObjects : the unique `mov qword [rip+x], rax` in the allocator
               (old build: fn+0x18e, target 0x0cd16500).
  FNamePool  : the first `lea r8, [rip+x]` in the decoder
               (old build: fn+0x18, target 0x0cc32300); cross-checked
               against `cmp byte [rip+y], 0` where pool == y + 0x267.

Usage: py -3 derive_globals.py <path-to-exe>
"""
import sys
import struct

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

EXE = sys.argv[1]
pe = pefile.PE(EXE, fast_load=True)
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
TVA = text.VirtualAddress
D = text.get_data()

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

def masked_scan(sig):
    """sig: list of ints or None (wildcard). Returns list of RVAs."""
    hits = []
    n = len(sig)
    fixed = [(i, b) for i, b in enumerate(sig) if b is not None]
    first = sig[0]
    start = 0
    while True:
        i = D.find(bytes([first]), start)
        if i == -1 or i + n > len(D):
            break
        if all(D[i + k] == b for k, b in fixed):
            hits.append(TVA + i)
        start = i + 1
    return hits

def h(s):
    return [None if x == "??" else int(x, 16) for x in s.split()]

ALLOC_SIG = h("48 89 5c 24 20 55 56 57 48 83 ec 50 48 8d 15 ?? ?? ?? ?? "
              "48 8d 4c 24 30 e8 ?? ?? ?? ?? 33 db c7 84 24 80 00 00 00 00 00 20 00")
DECODER_SIG = h("48 89 5c 24 10 57 48 83 ec 20 80 3d ?? ?? ?? ?? 00 48 8b fa "
                "8b 19 74 09 4c 8d 05 ?? ?? ?? ?? eb 16")

def disasm(fn_rva, length=0x300):
    code = D[fn_rva - TVA: fn_rva - TVA + length]
    return list(md.disasm(code, fn_rva))

def rip_target(ins):
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            return ins.address + ins.size + op.mem.disp
    return None

print("EXE:", EXE)
fh = pe.FILE_HEADER; oh = pe.OPTIONAL_HEADER
print("PE: ts=0x%08x size=0x%08x csum=0x%08x" % (fh.TimeDateStamp, oh.SizeOfImage, oh.CheckSum))
print()

# --- ObjObjects ---
alloc = masked_scan(ALLOC_SIG)
print("allocator sig: %d hit(s): %s" % (len(alloc), ", ".join("0x%08x" % x for x in alloc)))
objobjects = None
if len(alloc) == 1:
    for ins in disasm(alloc[0]):
        if ins.mnemonic == "mov" and len(ins.operands) == 2:
            d, s = ins.operands
            if (d.type == X86_OP_MEM and d.mem.base == X86_REG_RIP
                    and s.type != X86_OP_MEM and ins.reg_name(s.reg) == "rax"):
                objobjects = rip_target(ins)
                print("  ObjObjects write @ fn+0x%x: %s %s -> 0x%08x"
                      % (ins.address - alloc[0], ins.mnemonic, ins.op_str, objobjects))
                break

# --- FNamePool ---
dec = masked_scan(DECODER_SIG)
print("decoder sig:   %d hit(s): %s" % (len(dec), ", ".join("0x%08x" % x for x in dec)))
fnamepool = None
flag = None


def decode_pool(fn):
    """Return (pool, init_flag) for one decoder, or (None, None)."""
    pool = init_flag = None
    for ins in disasm(fn, 0x80):
        if pool is None and ins.mnemonic == "lea" \
           and ins.reg_name(ins.operands[0].reg) == "r8":
            pool = rip_target(ins)
        if init_flag is None and ins.mnemonic == "cmp" \
           and ins.operands[0].type == X86_OP_MEM \
           and ins.operands[0].mem.base == X86_REG_RIP:
            init_flag = rip_target(ins)
        if pool is not None and init_flag is not None:
            break
    return pool, init_flag


# The signature matches every FName decoder variant the build emits (narrow /
# wide), so multiple hits are expected. Each must agree on the pool and satisfy
# pool - init_flag == 0x267; a disagreement means the signature has drifted onto
# unrelated code and the result must not be trusted.
pools = set()
for fn in dec:
    pool, init_flag = decode_pool(fn)
    ok = pool is not None and init_flag is not None and pool - init_flag == 0x267
    print("  fn 0x%08x: pool=%s flag=%s %s"
          % (fn,
             ("0x%08x" % pool) if pool else "?",
             ("0x%08x" % init_flag) if init_flag else "?",
             "(pool-flag=0x267 OK)" if ok else "(INVARIANT FAILED - ignored)"))
    if ok:
        pools.add(pool)
        flag = init_flag

if len(pools) == 1:
    fnamepool = pools.pop()
elif len(pools) > 1:
    raise SystemExit("decoders disagree on FNamePool: %s - signature has drifted"
                     % ", ".join("0x%08x" % p for p in sorted(pools)))

print()
print("=== RESULT ===")
print("kObjObjects = 0x%08x" % objobjects if objobjects else "kObjObjects = NOT FOUND")
print("kFNamePool  = 0x%08x" % fnamepool if fnamepool else "kFNamePool  = NOT FOUND")
if fnamepool and flag:
    print("flag check: pool - flag = 0x%x (expect 0x267)" % (fnamepool - flag))
pe.close()
