# Subnautica 2 (UE 5.6.1): UE classes don't show in MSVC RTTI (UE has its own
# reflection system). Instead we follow xrefs from the class-name strings.
#
# For every interesting class name we know about, find the C-string in .rdata
# and dump:
#   - all xrefs to it (with the calling function name if Ghidra has one)
#   - a short disassembly window at the call site so we can spot the
#     Z_Construct_UClass_X / GetPrivateStaticClass / FName pattern
#
# Once we identify the Z_Construct or StaticClass getter we can pull the
# UClass* CDO pointer, which gives us the vtable layout the engine actually
# uses (independent of MSVC RTTI).

OUT = r"C:\tmp\sub2_camera_uclass.txt"

TARGETS = [
    "AUWEPlayerCameraManager",
    "UWEPlayerCameraManager",
    "UWEPlayerCameraManagerSettings",
    "APlayerCameraManager",
    "AGameplayCamerasPlayerCameraManager",
    "UGameplayCameraComponent",
    "UGameplayCameraComponentBase",
    "UCameraComponentCameraNode",
    "MinimalViewInfo",
    "PlayerCameraManager",
    "GetCameraView",
    "UpdateViewTarget",
    "DoUpdateCamera",
    "ProcessViewRotation",
    "CalcCamera",
]

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def read_cstring(a, max_len=128):
    out = []
    cur = a
    for _ in range(max_len):
        try:
            b = mem.getByte(cur) & 0xff
        except:
            return None
        if b == 0:
            return "".join(chr(c) for c in out)
        if not (0x20 <= b <= 0x7e):
            return None
        out.append(b)
        cur = cur.add(1)
    return None

print("Iterating defined strings...")
string_addrs = {name: [] for name in TARGETS}
target_set = set(TARGETS)
defined = listing.getDefinedData(True)
for data in defined:
    if not data.hasStringValue():
        continue
    try:
        val = data.getValue()
    except:
        continue
    if val is None:
        continue
    s = str(val)
    if s in target_set:
        string_addrs[s].append(data.getAddress().getOffset())

for name in TARGETS:
    locs = string_addrs.get(name, [])
    print("  %-40s %d location(s)" % (name, len(locs)))

def dump_instructions(f, start_addr, count):
    instr = listing.getInstructionAt(start_addr)
    n = 0
    while instr is not None and n < count:
        f.write("    %s  %s\n" % (instr.getAddress(), instr))
        instr = instr.getNext()
        n += 1

with open(OUT, "w") as f:
    f.write("Subnautica 2 - UClass camera-name string xrefs\n")
    f.write("=" * 70 + "\n\n")
    for name in TARGETS:
        locs = string_addrs.get(name, [])
        f.write("### %s\n" % name)
        if not locs:
            f.write("  (no string match)\n\n")
            continue
        for sloc in locs:
            f.write("  string @ 0x%x\n" % sloc)
            xrefs = ref_mgr.getReferencesTo(addr(sloc))
            shown = 0
            for r in xrefs:
                if shown >= 8:
                    f.write("    ... (more refs)\n")
                    break
                from_a = r.getFromAddress()
                fn = fm.getFunctionContaining(from_a)
                fname = fn.getName() if fn else "?"
                fentry = ("0x%x" % fn.getEntryPoint().getOffset()) if fn else "-"
                f.write("    xref from %s  fn=%s @ %s\n" % (from_a, fname, fentry))
                dump_instructions(f, from_a, 6)
                shown += 1
        f.write("\n")

print("Wrote %s" % OUT)
