# Dump disassembly + immediate operands for a list of suspect functions.
# Edit FUNCS or pass them via a sidecar mechanism. Designed for the
# "what is FUN_XXXXXX actually doing" iteration loop after
# follow_class_registration narrows the candidate set.

OUT = r"C:\tmp\sub2_function_dump.txt"

FUNCS = [
    ("Z_Construct_AUWEPlayerCameraManager_full_140 bytes", 0x146303f40),
    ("Z_Construct_AUWEPlayerCameraManager_inner_FClassParams_candidate", 0x1463040d0),
    ("Z_Construct_UWEPlayerCameraManagerSettings_outer", 0x146304020),
    ("Z_Construct_canonical_Settings_check", 0x146303c90),
    ("UECodeGen_ConstructUClass_real", 0x1416d82f0),
    ("UECodeGen_ConstructUClass_thunk", 0x14157a7d0),
    ("GetCameraView_xref_site_A", 0x14a27d610),
    ("GetCameraView_xref_site_B", 0x14ca0a148),
    ("APlayerCameraManager_string_xref", 0x14a175018),
]

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
sym_tab = currentProgram.getSymbolTable()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

with open(OUT, "w") as f:
    for label, ep in FUNCS:
        a = addr(ep)
        func = fm.getFunctionContaining(a)
        f.write("=" * 70 + "\n")
        f.write("%s  @ 0x%x\n" % (label, ep))
        if func is None:
            f.write("  (no function at this address)\n\n")
            continue
        f.write("  function: %s  size=%d\n" % (func.getName(), func.getBody().getNumAddresses()))

        count = 0
        ins = listing.getInstructionAt(func.getEntryPoint())
        while ins is not None and count < 80:
            ops_text = ins.toString()
            extra = ""
            for i in range(ins.getNumOperands()):
                for r in ins.getOperandReferences(i):
                    t = r.getToAddress()
                    if t is None: continue
                    bn = block_name(t)
                    sym = sym_tab.getPrimarySymbol(t)
                    sname = sym.getName() if sym else "-"
                    extra += "  ; -> 0x%x [%s] %s" % (t.getOffset(), bn, sname)
            f.write("  %s  %s%s\n" % (ins.getAddress(), ops_text, extra))
            ins = ins.getNext()
            count += 1
            if ins is not None and not func.getBody().contains(ins.getAddress()):
                break
        f.write("\n")

print("Wrote %s" % OUT)
