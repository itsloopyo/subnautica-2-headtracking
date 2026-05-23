# Identify each of the 11 unique callers to APlayerController::GetPlayerViewPoint
# that we captured at runtime via _ReturnAddress() logging.
#
# Input: hardcoded list of return-RVAs (relative to module base = image base
# = 0x140000000). The instruction at (image_base + RVA) is the instruction
# AFTER the `call`, so we want to look at the few instructions before it.
#
# Output: for each caller, the containing function (name + entry) plus 20
# instructions of context centered on the call site, plus any nearby string
# references that might hint at what the function does.

OUT = r"C:\tmp\sub2_gpv_callers.txt"

IMAGE_BASE = 0x140000000

# Captured via runtime logging from a 113-second gameplay session.
# Format: (label, return-RVA, hits-per-frame estimate, notes)
CALLERS = [
    ("tier1_dominant",       0x06328548,  22, "per-actor: visibility/streaming/audio?"),
    ("tier2_high_a",         0x04170a87,   4, ""),
    ("tier2_high_b",         0x043e9e07,   2, ""),
    ("tier2_intermittent",   0x06a4ef05,   2, "appeared after entering area"),
    ("perframe_a",           0x03fc88cc,   1, ""),
    ("perframe_b",           0x04176dbd,   1, ""),
    ("perframe_c",           0x02b5ce18,   1, ""),
    ("perframe_d",           0x05113961,   1, ""),
    ("perframe_e",           0x063e6be9,   1, ""),
    ("rare_a",               0x05105e90,   0, "~11/sec - action-triggered?"),
    ("rare_b",               0x068d7a35,   0, "~9/sec - action-triggered?"),
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
    """Collect any cross-references from this instruction. Look up strings
    pointed at by data references so we can spot diagnostic strings inside
    the function body that hint at its identity."""
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
            # If the target is in .rdata, try to interpret as a C-string.
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

    # Walk back ~12 instructions before the return address so we see the
    # `call` itself and how the FRotator/FVector out-params were prepared.
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

    # Walk all of the function's instructions looking for nearby string refs
    # that might give us a clue (UE5 leaves some diagnostic strings).
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

    # Who calls the containing function? Helps put it in a chain.
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
    f.write("Subnautica 2: GetPlayerViewPoint caller identification\n")
    f.write("Image base = 0x%x\n" % IMAGE_BASE)
    f.write("=" * 78 + "\n\n")
    for label, ret_rva, est_hpf, notes in CALLERS:
        dump_caller(f, label, ret_rva, est_hpf, notes)

print("Wrote %s" % OUT)
