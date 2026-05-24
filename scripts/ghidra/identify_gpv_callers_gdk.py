# GDK-specific copy of identify_gpv_callers.py. Same logic, different
# caller-RVA table - captured from the Xbox/Game Pass build via the F2
# inject-mode-0 caller-summary on 2026-05-24. Run via:
#
#   pixi run ghidra-script identify_gpv_callers_gdk \
#     -ProgramExe scratch/Subnautica2-WinGDK-Shipping-dumped.exe \
#     -ProjectName "Subnautica2-GDK"
#
# Output identifies which containing function each caller sits inside,
# with string refs / callers-of-the-fn that hint at its role. The render
# caller is the one whose containing function builds FMinimalViewInfo
# (the Steam equivalent was caller [2] at RVA 0x04171af7).

OUT = r"C:\tmp\sub2_gpv_callers_gdk.txt"

IMAGE_BASE = 0x140000000

# Captured via F2 (inject-mode 0) + caller-summary dump from a ~60s
# Xbox gameplay session on 2026-05-24. Ordered by count descending so
# the rank loosely lines up with Steam's tier1/tier2/perframe/rare buckets.
CALLERS = [
    ("rank01_dominant",      0x06131bf8,  93, "168k - dominant per-actor"),
    ("rank02_high",          0x03f074a7,  17, "31k - 2nd highest"),
    ("rank03_mid",           0x041809d7,   8, "14k - candidate render caller"),
    ("rank04_mid",           0x068555b5,   8, "14k"),
    ("rank05_low",           0x028f81d8,   4, "7k"),
    ("rank06_low",           0x061f02b9,   4, "7k"),
    ("rank07_low",           0x04e99361,   4, "7k"),
    ("rank08_low",           0x03f0d7dd,   4, "7k"),
    ("rank09_low",           0x03d5f36c,   4, "7k"),
    ("rank10_rare",          0x066ddfe5,   1, "1k"),
    ("rank11_very_rare",     0x04e8b890,   0, "154 - one-shot or near-one-shot"),
]

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
sym_tab = currentProgram.getSymbolTable()
ref_mgr = currentProgram.getReferenceManager()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def read_cstring(a, max_len=128):
    out = []
    cur = a
    for _ in range(max_len):
        try:
            b = mem.getByte(cur) & 0xff
        except:
            return None
        if b == 0:
            return "".join(chr(c) for c in out) if out else None
        if not (0x20 <= b <= 0x7e):
            return None
        out.append(b)
        cur = cur.add(1)
    return None

def operand_extras(ins):
    pieces = []
    for i in range(ins.getNumOperands()):
        for r in ins.getOperandReferences(i):
            t = r.getToAddress()
            if t is None:
                continue
            bn = block_name(t)
            sym = sym_tab.getPrimarySymbol(t)
            sname = sym.getName() if sym else "-"
            tail = "  -> 0x%x [%s] %s" % (t.getOffset(), bn, sname)
            if bn in (".rdata",):
                s = read_cstring(t)
                if s and len(s) >= 3:
                    snippet = s if len(s) <= 80 else (s[:77] + "...")
                    tail += "  STR=\"%s\"" % snippet
            pieces.append(tail)
    return pieces

def dump_caller(f, label, ret_rva, est_hpf, notes):
    abs_ret = IMAGE_BASE + ret_rva
    a = addr(abs_ret)
    func = fm.getFunctionContaining(a)
    f.write("=" * 78 + "\n")
    f.write("%s  ret-RVA=0x%x  abs=0x%x  hits/frame~%d  %s\n"
            % (label, ret_rva, abs_ret, est_hpf, notes))

    if func is None:
        f.write("  (no Ghidra function contains this address)\n\n")
        return

    func_entry = func.getEntryPoint().getOffset()
    func_size = func.getBody().getNumAddresses()
    f.write("  containing function: %s  entry=0x%x  size=%d bytes\n"
            % (func.getName(), func_entry, func_size))

    instrs_before = []
    cur = listing.getInstructionBefore(a)
    while cur is not None and len(instrs_before) < 12 and func.getBody().contains(cur.getAddress()):
        instrs_before.insert(0, cur)
        cur = listing.getInstructionBefore(cur.getAddress())

    f.write("  --- instructions immediately before ret RVA ---\n")
    for ins in instrs_before:
        extras = operand_extras(ins)
        f.write("    %s  %s\n" % (ins.getAddress(), ins.toString()))
        for ex in extras:
            f.write("      %s\n" % ex)

    f.write("  --- ret-RVA instruction (post-call) ---\n")
    ret_ins = listing.getInstructionAt(a)
    if ret_ins is not None:
        f.write("    %s  %s\n" % (ret_ins.getAddress(), ret_ins.toString()))

    f.write("  --- string refs anywhere in containing function ---\n")
    seen = set()
    ins = listing.getInstructionAt(func.getEntryPoint())
    while ins is not None and func.getBody().contains(ins.getAddress()):
        for i in range(ins.getNumOperands()):
            for r in ins.getOperandReferences(i):
                t = r.getToAddress()
                if t is None:
                    continue
                if block_name(t) not in (".rdata",):
                    continue
                s = read_cstring(t)
                if not s or len(s) < 6:
                    continue
                key = (t.getOffset(), s)
                if key in seen:
                    continue
                seen.add(key)
                snippet = s if len(s) <= 100 else (s[:97] + "...")
                f.write("    @ %s  ref 0x%x  \"%s\"\n"
                        % (ins.getAddress(), t.getOffset(), snippet))
        ins = ins.getNext()

    f.write("  --- callers of containing function (up to 8) ---\n")
    callers_seen = 0
    for r in ref_mgr.getReferencesTo(func.getEntryPoint()):
        from_a = r.getFromAddress()
        cfn = fm.getFunctionContaining(from_a)
        cname = cfn.getName() if cfn else "?"
        centry = ("0x%x" % cfn.getEntryPoint().getOffset()) if cfn else "-"
        f.write("    from %s  in fn=%s @ %s\n" % (from_a, cname, centry))
        callers_seen += 1
        if callers_seen >= 8:
            f.write("    ... (more callers)\n")
            break

    f.write("\n")

with open(OUT, "w") as f:
    f.write("Subnautica 2 (GDK): GetPlayerViewPoint caller identification\n")
    f.write("Image base = 0x%x\n" % IMAGE_BASE)
    f.write("=" * 78 + "\n\n")
    for label, ret_rva, est_hpf, notes in CALLERS:
        dump_caller(f, label, ret_rva, est_hpf, notes)

print("Wrote %s" % OUT)
