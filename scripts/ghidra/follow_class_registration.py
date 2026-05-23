# Subnautica 2 (UE 5.6.1): for each camera-related UClass name, follow the
# code xref into the Z_Construct_UClass_* function and extract:
#   - the static "Z_Registration_Info_UClass_X" slot that holds the UClass*
#     pointer at runtime (we can read this at runtime to pin the class).
#   - the ClassParams struct (also static, in .rdata) which contains the
#     parent class reference, function table, property table.
#   - any constructor/vftable-shaped pointer into .text.
#
# UE5 shipping pattern (UECodeGen_Private::ConstructUClass):
#
#   Z_Construct_UClass_AUWEPlayerCameraManager:
#       MOV  RAX, [Z_Registration_Info_UClass_AUWEPlayerCameraManager + 0x8]
#       TEST RAX, RAX
#       JNZ  done
#       LEA  RCX, [Z_Registration_Info_UClass_AUWEPlayerCameraManager + 0x8]
#       LEA  RDX, [&ClassParams]            ; <-- our prize
#       CALL UECodeGen_Private::ConstructUClass
#     done:
#       RET
#
# ClassParams (FClassParams) layout:
#       +0x00 ClassNoRegisterFunc (UClass*())
#       +0x08 ClassName ("UWEPlayerCameraManager")
#       +0x10 ...
#
# We don't need to fully decode FClassParams here; just dump the LEA
# operands inside the function so we can identify which RVAs to bake
# into the runtime DLL.

OUT = r"C:\tmp\sub2_class_registration.txt"

TARGETS = [
    "AUWEPlayerCameraManager",
    "UWEPlayerCameraManager",
    "UWEPlayerCameraManagerSettings",
    "APlayerCameraManager",
    "AGameplayCamerasPlayerCameraManager",
    "UGameplayCameraComponent",
    "UGameplayCameraComponentBase",
    "MinimalViewInfo",
    "PlayerCameraManager",
]

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()
sym_tab = currentProgram.getSymbolTable()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

# ---------- locate strings via Ghidra's defined-string analysis ----------
print("Locating target strings...")
target_set = set(TARGETS)
string_addrs = {name: [] for name in TARGETS}
for data in listing.getDefinedData(True):
    if not data.hasStringValue():
        continue
    try:
        s = str(data.getValue())
    except:
        continue
    if s in target_set:
        string_addrs[s].append(data.getAddress())

# ---------- for each target string, find code xref and walk function ------

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def dump_function_walk(f, func, str_addr):
    """Walk function instructions; print LEAs, CALLs, and any reference operands."""
    f.write("  Function %s @ 0x%x (size=%d)\n" % (
        func.getName(), func.getEntryPoint().getOffset(), func.getBody().getNumAddresses()))
    iter_addrs = func.getBody().getAddresses(True)
    lea_targets = []
    call_targets = []
    for a in iter_addrs:
        ins = listing.getInstructionAt(a)
        if ins is None:
            continue
        mnem = ins.getMnemonicString()
        if mnem in ("LEA", "MOV", "CALL"):
            for i in range(ins.getNumOperands()):
                refs = ins.getOperandReferences(i)
                for r in refs:
                    t = r.getToAddress()
                    if t is None:
                        continue
                    if t.getOffset() == str_addr.getOffset():
                        continue  # the class-name string itself - we already know
                    bn = block_name(t)
                    sym = sym_tab.getPrimarySymbol(t)
                    sname = sym.getName() if sym else "-"
                    line = "    %s %-4s -> 0x%016x  [%s]  %s\n" % (
                        ins.getAddress(), mnem, t.getOffset(), bn, sname)
                    f.write(line)
                    if mnem == "CALL":
                        call_targets.append(t.getOffset())
                    elif mnem == "LEA":
                        lea_targets.append(t.getOffset())
    return lea_targets, call_targets

with open(OUT, "w") as f:
    f.write("Subnautica 2 - UClass registration function dump\n")
    f.write("=" * 70 + "\n\n")
    for name in TARGETS:
        f.write("### %s\n" % name)
        for sloc in string_addrs.get(name, []):
            f.write("  string @ 0x%x  [%s]\n" % (sloc.getOffset(), block_name(sloc)))
            xrefs = ref_mgr.getReferencesTo(sloc)
            for r in xrefs:
                from_a = r.getFromAddress()
                if block_name(from_a) not in (".text",):
                    continue
                func = fm.getFunctionContaining(from_a)
                if func is None:
                    f.write("    xref from 0x%x (no containing function)\n" % from_a.getOffset())
                    continue
                f.write("    xref from 0x%x\n" % from_a.getOffset())
                lts, cts = dump_function_walk(f, func, sloc)
                if cts:
                    f.write("    CALLs: %s\n" % ", ".join("0x%x" % c for c in cts))
                # Heuristic: among the LEAs, ones into .data are likely the
                # Z_Registration_Info_UClass_X slot; ones into .rdata are
                # likely FClassParams; into .text are constructor/vftable.
                rdata = [x for x in lts if 0x149819000 <= x <  0x14c994000]
                data  = [x for x in lts if 0x14c994000 <= x <  0x14d1d06bc]
                text  = [x for x in lts if 0x140001000 <= x <  0x149819000]
                if data:  f.write("    likely Z_Registration_Info slots: %s\n" % ", ".join("0x%x" % x for x in data))
                if rdata: f.write("    likely FClassParams / metadata: %s\n" % ", ".join("0x%x" % x for x in rdata))
                if text:  f.write("    likely .text targets (ctor/vfunc): %s\n" % ", ".join("0x%x" % x for x in text))
        f.write("\n")

print("Wrote %s" % OUT)
