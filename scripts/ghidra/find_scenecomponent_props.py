# Find USceneComponent property offsets via UE5 reflection metadata.
#
# UE5 reflection: every UCLASS-tagged class gets a generated
# Z_Construct_UClass_<Name>_Statics function that registers the UClass*
# plus all of its UPROPERTYs. Each property registration carries:
#   - field name (FName, as a c-string somewhere in .rdata)
#   - offset within the class instance (uint32)
#   - type metadata
#
# By finding the SceneComponent's class registration we can extract field
# offsets analytically instead of guessing. Specifically we want:
#   - RelativeLocation, RelativeRotation, RelativeScale3D (UPROPERTYs)
#   - AttachParent, AttachChildren, AttachSocketName (UPROPERTYs)
# ComponentToWorld is NOT a UPROPERTY (transient), so for that we still
# need a different vector (vtable function disassembly).
#
# Approach:
#   1. String-search for "/Script/Engine" SceneComponent identifiers
#   2. Find xrefs to the SceneComponent class-name string from inside a
#      function that looks like Z_Construct (large body, many string refs)
#   3. Dump strings from inside that function so we can spot property names
#   4. Print every 4-byte immediate operand near each property-name ref
#      (those are likely the offsets we want)

OUT = r"C:\tmp\sub2_scenecomp_props.txt"

# Strings we'd expect to find in SceneComponent UClass registration.
NEEDLE_STRINGS = [
    "SceneComponent",
    "USceneComponent",
    "/Script/Engine.SceneComponent",
    "RelativeLocation",
    "RelativeRotation",
    "RelativeScale3D",
    "AttachParent",
    "AttachChildren",
    "AttachSocketName",
    "ComponentToWorld",
    "ComponentVelocity",
    "bAbsoluteLocation",
    "bAbsoluteRotation",
    "bAbsoluteScale",
    "bVisible",
    "bHiddenInGame",
]

mem      = currentProgram.getMemory()
fact     = currentProgram.getAddressFactory()
listing  = currentProgram.getListing()
fm       = currentProgram.getFunctionManager()
sym_tab  = currentProgram.getSymbolTable()
ref_mgr  = currentProgram.getReferenceManager()

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

# Step 1: enumerate every defined string in .rdata. Note which ones match
# our needles.
print("Scanning defined strings...")
string_addrs = {n: [] for n in NEEDLE_STRINGS}
needle_set = set(NEEDLE_STRINGS)
for data in listing.getDefinedData(True):
    if not data.hasStringValue():
        continue
    try:
        s = str(data.getValue())
    except:
        continue
    if s in needle_set:
        string_addrs[s].append(data.getAddress().getOffset())

print("\nNeedle string locations:")
for name in NEEDLE_STRINGS:
    locs = string_addrs.get(name, [])
    print("  %-40s %d location(s)" % (name, len(locs)))

# Step 2: for each property-name string, walk xrefs and group by
# containing function. The Z_Construct function will be the one that
# references MANY property-name strings.
print("\nGrouping xrefs by containing function...")
prop_names_focus = [
    "RelativeLocation", "RelativeRotation", "RelativeScale3D",
    "AttachParent", "AttachChildren", "AttachSocketName",
    "ComponentVelocity", "bVisible", "bHiddenInGame",
]
fn_to_props = {}  # fn_entry -> set of property names it references
fn_objects = {}   # fn_entry -> Ghidra Function object
for name in prop_names_focus:
    for sloc in string_addrs.get(name, []):
        for r in ref_mgr.getReferencesTo(addr(sloc)):
            fn = fm.getFunctionContaining(r.getFromAddress())
            if not fn:
                continue
            entry = fn.getEntryPoint().getOffset()
            fn_to_props.setdefault(entry, set()).add(name)
            fn_objects[entry] = fn

ranked = sorted(fn_to_props.items(),
                key=lambda kv: -len(kv[1]))[:8]

print("Top candidate Z_Construct functions:")
for entry, props in ranked:
    print("  fn 0x%016x  refs %d property names: %s"
          % (entry, len(props), ", ".join(sorted(props))))

# Step 3: for the top candidate, walk its instructions and report every
# 4-byte immediate (lea reg,[mem]; mov reg,imm32) along with any nearby
# string reference. The pattern is typically:
#     lea rax, ["RelativeLocation"]   ; FName for property
#     mov dword ptr [...], 0x140       ; offset of property
# So a 4-byte int near a property string ref is likely the offset.

def imm32_operands(ins):
    """Return list of immediate operand values that look like small uints."""
    out = []
    for i in range(ins.getNumOperands()):
        for op in ins.getOpObjects(i):
            try:
                v = op.getValue()
            except:
                continue
            if isinstance(v, (int, long)) if 'long' in dir(__builtins__) else isinstance(v, int):
                if 0 <= v <= 0x10000:  # plausible struct offset
                    out.append(int(v))
    return out

with open(OUT, "w") as f:
    f.write("Subnautica 2: USceneComponent UPROPERTY offset extraction\n")
    f.write("=" * 78 + "\n\n")

    f.write("Needle string presence:\n")
    for name in NEEDLE_STRINGS:
        locs = string_addrs.get(name, [])
        f.write("  %-40s %d at: %s\n" % (
            name, len(locs),
            ", ".join("0x%x" % a for a in locs[:4])))
    f.write("\n")

    f.write("Top Z_Construct-like functions (count of property-name refs):\n")
    for entry, props in ranked:
        f.write("  fn 0x%016x  %d refs: %s\n"
                % (entry, len(props), ", ".join(sorted(props))))
    f.write("\n")

    # For the #1 candidate, dump instructions and property-name refs +
    # nearby 4-byte immediates.
    if ranked:
        top_entry, top_props = ranked[0]
        top_fn = fn_objects[top_entry]
        f.write("=" * 78 + "\n")
        f.write("DEEP DUMP: fn 0x%x  (size %d)\n"
                % (top_entry, top_fn.getBody().getNumAddresses()))
        f.write("=" * 78 + "\n")

        ins = listing.getInstructionAt(top_fn.getEntryPoint())
        body = top_fn.getBody()
        last_string_ref = None  # (addr, value)
        while ins is not None and body.contains(ins.getAddress()):
            # Look for any operand that points at a string we recognise.
            for i in range(ins.getNumOperands()):
                for r in ins.getOperandReferences(i):
                    t = r.getToAddress()
                    if t is None:
                        continue
                    if block_name(t) != ".rdata":
                        continue
                    s = read_cstring(t)
                    if s and len(s) >= 3:
                        last_string_ref = (ins.getAddress(), s)
                        f.write("  STR @ %s  ref 0x%x  \"%s\"\n"
                                % (ins.getAddress(), t.getOffset(),
                                   s if len(s) <= 80 else s[:77] + "..."))

            # Look for immediates that look like small uints (struct offsets).
            opstr = ins.toString()
            # crude: split on commas/spaces and try to parse hex
            for tok in opstr.replace(",", " ").replace("[", " ").replace("]", " ").split():
                if tok.startswith("0x"):
                    try:
                        v = int(tok, 16)
                    except:
                        continue
                    if 0x40 <= v <= 0x1000 and last_string_ref is not None:
                        f.write("    IMM @ %s  0x%x  (last str: \"%s\")\n"
                                % (ins.getAddress(), v, last_string_ref[1]))
            ins = ins.getNext()

print("Wrote %s" % OUT)
