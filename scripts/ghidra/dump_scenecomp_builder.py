# Dump the SceneComponent property-builder function and pair every property
# name string reference with the nearest preceding 4-byte immediate move,
# which in UE5's generated UClass builder code is the property's struct
# offset.
#
# Builder pattern in UE5 looks like:
#   lea rcx, [RelativeLocation_name_string]   ; FName
#   mov dword ptr [rdx + 0x10], 0x140         ; offset within instance
# We capture the (string -> offset) pairing.

OUT = r"C:\tmp\sub2_scenecomp_builder.txt"

# Top candidate from find_scenecomponent_props.py
TARGET_FN = 0x143e4c3c0

mem      = currentProgram.getMemory()
fact     = currentProgram.getAddressFactory()
listing  = currentProgram.getListing()
fm       = currentProgram.getFunctionManager()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def read_cstring(a, max_len=200):
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

target_addr = addr(TARGET_FN)
fn = fm.getFunctionAt(target_addr)
if fn is None:
    print("ERROR: no function at 0x%x" % TARGET_FN)
else:
    print("Function: %s @ 0x%x  size=%d" %
          (fn.getName(), TARGET_FN, fn.getBody().getNumAddresses()))

with open(OUT, "w") as f:
    if fn is None:
        f.write("ERROR: no function at 0x%x\n" % TARGET_FN)
    else:
        f.write("Dump of %s @ 0x%x  (size %d bytes)\n" %
                (fn.getName(), TARGET_FN, fn.getBody().getNumAddresses()))
        f.write("=" * 78 + "\n\n")

        ins = listing.getInstructionAt(target_addr)
        body = fn.getBody()
        if ins is None:
            f.write("(no instruction at function entry; trying first body addr)\n")
            ins = listing.getInstructionAt(body.getMinAddress())

        # Maintain a "recent immediates" sliding window so when we see a
        # string ref we can report the immediates that came before it.
        recent_imms = []  # list of (instr_addr, imm_value)
        # And vice versa: when we see an immediate, what string did we
        # last reference?
        last_string_ref = None  # (instr_addr, string_value)

        seen_pairs = set()
        pairs = []  # (string_value, offset_value, near_instr_addr)
        line_count = 0

        while ins is not None and body.contains(ins.getAddress()):
            ins_addr = ins.getAddress()
            opstr = ins.toString()

            # Detect string refs to .rdata.
            string_refs_here = []
            for i in range(ins.getNumOperands()):
                for r in ins.getOperandReferences(i):
                    t = r.getToAddress()
                    if t is None:
                        continue
                    if block_name(t) != ".rdata":
                        continue
                    s = read_cstring(t)
                    if s and 3 <= len(s) <= 60:
                        string_refs_here.append((t.getOffset(), s))

            # Detect numeric immediates that look like struct offsets
            # (0x40 .. 0x800).
            imms_here = []
            for i in range(ins.getNumOperands()):
                for op in ins.getOpObjects(i):
                    try:
                        v = op.getValue()
                    except:
                        continue
                    try:
                        iv = int(v) & 0xffffffffffffffff
                    except:
                        continue
                    if 0x40 <= iv <= 0x800:
                        imms_here.append(iv)

            # Also use the instruction-level references-from (not just
            # operand-level), which catches LEA-target refs that
            # getOperandReferences misses.
            for r in ins.getReferencesFrom():
                t = r.getToAddress()
                if t is None:
                    continue
                if block_name(t) != ".rdata":
                    continue
                s = read_cstring(t)
                if s and 3 <= len(s) <= 60:
                    if (t.getOffset(), s) not in [(x[0], x[1]) for x in string_refs_here]:
                        string_refs_here.append((t.getOffset(), s))

            # Dump every instruction so we can see the structure.
            f.write("  %s  %s\n" % (ins_addr, opstr))
            for (a_off, s_val) in string_refs_here:
                f.write("    STR 0x%x  \"%s\"\n" % (a_off, s_val))
                last_string_ref = (ins_addr, s_val)
            for iv in imms_here:
                f.write("    IMM 0x%x\n" % iv)
                # Pair with the most recent string ref.
                if last_string_ref is not None:
                    pair = (last_string_ref[1], iv)
                    if pair not in seen_pairs:
                        seen_pairs.add(pair)
                        pairs.append((last_string_ref[1], iv,
                                      ins_addr.getOffset()))

            line_count += 1
            ins = ins.getNext()

        f.write("\n" + "=" * 78 + "\n")
        f.write("Inferred (property -> offset) pairs (string-imm pairings):\n")
        f.write("=" * 78 + "\n")
        # Group by string, report each unique offset that appeared near it.
        per_string = {}
        for s, iv, ia in pairs:
            per_string.setdefault(s, []).append((iv, ia))
        for s in sorted(per_string.keys()):
            offsets = per_string[s]
            f.write("  %-40s : %s\n" %
                    (s, ", ".join("0x%x (@%x)" % (iv, ia)
                                  for iv, ia in offsets[:6])))
        f.write("\nProcessed %d instructions.\n" % line_count)

print("Wrote %s" % OUT)
