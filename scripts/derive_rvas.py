#!/usr/bin/env python3
"""Deterministic SN2 RVA derivation - no Ghidra, no full analysis.

Repeated full Ghidra analyses of the 225MB UE5 binary proved flaky (2G OOM,
non-persisting reference/string analysis), so this derives the load-bearing
RVAs straight off the PE file using pefile + capstone:

  - GetPlayerViewPoint  : the function containing the LEA that loads the
    "APlayerController::GetPlayerViewPoint:" checkf string.
  - render caller       : the .pdata function that issues `call [reg+0x7f8]`
    then `call [reg+0x828]` (the camera double-vfn sequence the renderer's
    FMinimalViewInfo builder runs); retRVA of the +0x828 call is what
    kKnownCallerRvas[1] needs.
  - GUObjectArray.ObjObjects + FNamePool : pinned by their decoder/allocator
    code signatures (the global each references in .data/.bss).

Function boundaries come from the PE exception table (.pdata
RUNTIME_FUNCTION[]), which is exact and needs no analysis. Validate against a
known build (RVAs already in steam_offsets.cpp) before trusting new output.

Usage: py -3 derive_rvas.py <path-to-exe>
"""
import sys
import struct
import bisect

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

EXE = sys.argv[1]

pe = pefile.PE(EXE, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase  # for RVA math we keep things RVA-relative

# ---- map sections ----
sections = []
for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode("latin1")
    sections.append({
        "name": name,
        "va": s.VirtualAddress,
        "vsize": s.Misc_VirtualSize,
        "raw": s.PointerToRawData,
        "rawsize": s.SizeOfRawData,
        "data": s.get_data(),
    })

def sec_for_rva(rva):
    for s in sections:
        if s["va"] <= rva < s["va"] + max(s["vsize"], s["rawsize"]):
            return s
    return None

def read_rva(rva, n):
    s = sec_for_rva(rva)
    if not s:
        return None
    off = rva - s["va"]
    return s["data"][off:off + n]

def u32_at(rva):
    b = read_rva(rva, 4)
    return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

text = next(s for s in sections if s["name"] == ".text")
TEXT_VA = text["va"]
TEXT_DATA = text["data"]
TEXT_END = TEXT_VA + len(TEXT_DATA)

# ---- .pdata RUNTIME_FUNCTION table: exact function boundaries ----
# Each entry: BeginAddress, EndAddress, UnwindData (3x uint32 RVA).
pdata = next((s for s in sections if s["name"] == ".pdata"), None)
func_starts = []
func_ranges = []  # (begin, end)
if pdata:
    d = pdata["data"]
    for i in range(0, len(d) - 11, 12):
        begin, end, unwind = struct.unpack_from("<III", d, i)
        if begin == 0 and end == 0:
            continue
        if begin >= end:
            continue
        func_ranges.append((begin, end))
    func_ranges.sort()
    func_starts = [b for (b, e) in func_ranges]

def func_containing(rva):
    """Return (begin, end) of the .pdata function containing rva, or None."""
    i = bisect.bisect_right(func_starts, rva) - 1
    if i < 0:
        return None
    b, e = func_ranges[i]
    if b <= rva < e:
        return (b, e)
    return None

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False

# ---------------------------------------------------------------------------
# 1. GetPlayerViewPoint via the checkf string xref.
# ---------------------------------------------------------------------------
def find_string_rva(needle):
    nb = needle.encode("latin1")
    for s in sections:
        if s["name"] not in (".rdata", ".data", ".rodata"):
            continue
        idx = s["data"].find(nb)
        if idx != -1:
            return s["va"] + idx
    return None

def find_lea_targets_to(target_rva):
    """Scan .text for `lea reg,[rip+disp32]` whose target == target_rva.
    Returns list of instruction RVAs. REX.W 8D /r with mod=00 rm=101."""
    hits = []
    data = TEXT_DATA
    # opcode forms: 48 8D <modrm> d d d d  and 4C 8D <modrm> ...
    # modrm low 3 bits == 101 (rip), reg field in bits 3-5: bytes 05,0D,15,1D,25,2D,35,3D
    rip_modrm = {0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D}
    i = 0
    n = len(data)
    while i < n - 6:
        if data[i] in (0x48, 0x4C) and data[i + 1] == 0x8D and data[i + 2] in rip_modrm:
            disp = struct.unpack_from("<i", data, i + 3)[0]
            instr_rva = TEXT_VA + i
            next_rva = instr_rva + 7
            if next_rva + disp == target_rva:
                hits.append(instr_rva)
            i += 7
            continue
        i += 1
    return hits

# GPV's first 32 relocation-free prologue bytes (frame setup + `mov edx,0x142`),
# captured from steam-win64-20260522/20260601. The verbose checkf string that
# used to anchor GPV is stripped in newer builds, so the prologue signature is
# the primary anchor. If a future patch reshapes the prologue, re-dump it from
# the prior known-good Ghidra project via scripts/ghidra/dump_old_signatures.py.
GPV_PROLOGUE_SIG = bytes.fromhex(
    "48895c24105556415648" "8d6c24b94881ec90000000" "488bf2488bd9ba42010000")

def scan_text_sig(sig):
    hits = []
    start = 0
    while True:
        i = TEXT_DATA.find(sig, start)
        if i == -1:
            break
        hits.append(TEXT_VA + i)
        start = i + 1
    return hits

def derive_gpv():
    # Primary: the checkf string is referenced from inside GPV (older builds).
    for needle in ("APlayerController::GetPlayerViewPoint: out_Location",
                   "APlayerController::GetPlayerViewPoint:"):
        srva = find_string_rva(needle)
        if srva is None:
            continue
        leas = find_lea_targets_to(srva)
        funcs = {}
        for l in leas:
            fc = func_containing(l)
            if fc:
                funcs.setdefault(fc[0], []).append(l)
        if funcs:
            return srva, funcs
    # Fallback: prologue signature (the string is stripped in newer builds).
    hits = scan_text_sig(GPV_PROLOGUE_SIG)
    if len(hits) == 1:
        return None, {hits[0]: ["prologue-sig"]}
    if len(hits) > 1:
        return None, {h: ["prologue-sig(ambiguous)"] for h in hits}
    return None, {}

# ---------------------------------------------------------------------------
# 2. Render caller: function with both call [reg+0x7f8] and call [reg+0x828].
#    call [reg+disp32] = FF /2 mem, modrm mod=10 rm=reg, disp32.
#    Encodings: FF 90+rm d d d d (rax..rdi), 41 FF 90+rm ... (r8..r15).
# ---------------------------------------------------------------------------
def find_vfn_call_sites(disp):
    """Return list of (instr_rva, ret_rva) for every `call [reg+disp]`."""
    sites = []
    data = TEXT_DATA
    n = len(data)
    target = struct.pack("<i", disp)
    i = 0
    while i < n - 6:
        # non-REX: FF /2 with modrm 0x90..0x97 (mod=10, reg=010, rm=0..7)
        if data[i] == 0xFF and 0x90 <= data[i + 1] <= 0x97:
            if data[i + 2:i + 6] == target:
                rva = TEXT_VA + i
                sites.append((rva, rva + 6))
                i += 6
                continue
        # REX.B: 41 FF /2 modrm 0x90..0x97 (r8..r15)
        if data[i] == 0x41 and data[i + 1] == 0xFF and 0x90 <= data[i + 2] <= 0x97:
            if data[i + 3:i + 7] == target:
                rva = TEXT_VA + i
                sites.append((rva, rva + 7))
                i += 7
                continue
        i += 1
    return sites

def derive_render_caller():
    sites_828 = find_vfn_call_sites(0x828)
    sites_7f8 = find_vfn_call_sites(0x7f8)
    fns_7f8 = set()
    for rva, _ in sites_7f8:
        fc = func_containing(rva)
        if fc:
            fns_7f8.add(fc[0])
    cands = []
    for rva, ret in sites_828:
        fc = func_containing(rva)
        if fc and fc[0] in fns_7f8:
            # confidence: does the fn deref [reg+0x368] (PCM) ?
            has_368 = func_refs_disp(fc, 0x368)
            cands.append((fc[0], rva, ret, has_368))
    return len(sites_828), len(sites_7f8), cands

def func_refs_disp(fc, disp):
    """True if any instruction in the function has a [reg+disp] memory operand."""
    b, e = fc
    data = read_rva(b, min(e - b, 0x4000))
    if not data:
        return False
    needle = struct.pack("<i", disp)
    return needle in data

# ---------------------------------------------------------------------------
# run + report
# ---------------------------------------------------------------------------
print("EXE:", EXE)
fh = pe.FILE_HEADER
oh = pe.OPTIONAL_HEADER
print("PE fingerprint: ts=0x%08x size=0x%08x csum=0x%08x" %
      (fh.TimeDateStamp, oh.SizeOfImage, oh.CheckSum))
print(".pdata functions: %d" % len(func_ranges))
print()

print("== GetPlayerViewPoint ==")
srva, funcs = derive_gpv()
if srva is not None:
    print("  checkf string @ RVA 0x%08x" % srva)
elif funcs:
    print("  checkf string stripped - using prologue signature fallback")
else:
    print("  NOT FOUND (string stripped AND prologue signature did not match)")
for fstart, sites in sorted(funcs.items()):
    # sites entries are either lea RVAs (string path) or marker strings (sig path)
    sites_str = ", ".join(("0x%08x" % s) if isinstance(s, int) else s for s in sites)
    print("  -> GPV fn RVA 0x%08x  (from: %s)" % (fstart, sites_str))
    prologue = read_rva(fstart, 16)
    if prologue:
        print("     prologue:", " ".join("%02x" % c for c in prologue))
print()

print("== render caller (call [reg+0x7f8] -> call [reg+0x828] in one fn) ==")
n828, n7f8, cands = derive_render_caller()
print("  call[+0x828] sites: %d   call[+0x7f8] sites: %d" % (n828, n7f8))
for fstart, csite, ret, has368 in cands:
    print("  fn 0x%08x  call@0x%08x  retRVA 0x%08x  PCM[+0x368]:%s" %
          (fstart, csite, ret, "YES" if has368 else "no"))
print()

pe.close()
