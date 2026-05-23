# Identify which UE5 class each candidate APlayerController-field UObject
# belongs to. Input: a list of "first virtual function" RVAs captured
# at runtime via ProbeControllerObject(). For each, locate the vtable
# (the .rdata address where this RVA appears as the first qword) and
# the class registration that built the vtable. Output the class
# fingerprint so we can map field offsets -> Pawn / PlayerState /
# PlayerCameraManager / HUD / etc.

OUT = r"C:\tmp\sub2_controller_fields.txt"

IMAGE_BASE = 0x140000000

# (label, slot offset in APlayerController, first-virt RVA, candidate addr)
CANDIDATES = [
    ("self_vtbl_first_virt",       0x000, 0x06844a30, 0x7ff7326b3e20),
    ("class_private?",             0x010, 0x098eaf78, 0x7ff497de97c0),
    ("strange_off30",              0x030, 0x03a876b0, 0x7ff7318e9c08),
    ("self_ref?",                  0x058, 0x0ae83e20, 0x2111e758b00),
    ("uobj_180",                   0x180, 0x0a8011a8, 0x21112805f80),
    ("uobj_1c0_dup_308",           0x1c0, 0x0a1859a8, 0x21474e8f640),
    ("static_2b0",                 0x2b0, 0x068448c8, 0x7ff7326b4da0),
    ("uobj_2b8",                   0x2b8, 0x0a4c4cb0, 0x21483c9dff0),
    ("uobj_2f0_dup_358",           0x2f0, 0x0a32a668, 0x21483c906a0),
    ("static_348",                 0x348, 0x012924a0, 0x7ff7323de7b8),
    ("uobj_350",                   0x350, 0x0af8b368, 0x21431924ed0),
    ("uobj_360",                   0x360, 0x0ae835d8, 0x2142dea0b50),
    ("uobj_368",                   0x368, 0x0a178418, 0x2144f36d560),
    ("uobj_428",                   0x428, 0x0a801d38, 0x2143df8b0b0),
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

def read_u64(a):
    try:
        return mem.getLong(a) & 0xffffffffffffffff
    except:
        return None

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

# Scan .rdata for any 8-byte qword == target_abs. Each hit is a vtable
# whose first entry is the target function. There can be more than one
# (RTTI completes vtables get emitted next to each base vtable).
def find_vtable_for_first_virt(first_virt_abs):
    matches = []
    for block in mem.getBlocks():
        if block.getName() != ".rdata":
            continue
        start = block.getStart().getOffset()
        end   = block.getEnd().getOffset()
        # Scan aligned to 8 bytes for speed.
        cur = start
        while cur + 8 <= end:
            try:
                v = mem.getLong(addr(cur)) & 0xffffffffffffffff
            except:
                v = 0
            if v == first_virt_abs:
                matches.append(cur)
                if len(matches) >= 5:
                    return matches
            cur += 8
    return matches

# For a vtable address, walk xrefs to find the function that writes it
# (i.e., the class constructor / Z_Construct). That function often has
# class-identifying strings nearby in shipping builds, even when the
# vtable itself is unlabeled.
def find_writers_of_address(target_addr):
    writers = []
    refs = ref_mgr.getReferencesTo(addr(target_addr))
    for r in refs:
        from_a = r.getFromAddress()
        fn = fm.getFunctionContaining(from_a)
        if fn:
            writers.append((from_a, fn))
        else:
            writers.append((from_a, None))
    return writers

def strings_in_function(fn, limit=10):
    if fn is None:
        return []
    seen = []
    ins = listing.getInstructionAt(fn.getEntryPoint())
    body = fn.getBody()
    while ins is not None and body.contains(ins.getAddress()):
        for i in range(ins.getNumOperands()):
            for r in ins.getOperandReferences(i):
                t = r.getToAddress()
                if t is None:
                    continue
                if block_name(t) != ".rdata":
                    continue
                s = read_cstring(t)
                if s and len(s) >= 4:
                    if s not in [x[1] for x in seen]:
                        seen.append((t.getOffset(), s))
                        if len(seen) >= limit:
                            return seen
        ins = ins.getNext()
    return seen

with open(OUT, "w") as f:
    f.write("Subnautica 2: APlayerController field identification\n")
    f.write("=" * 78 + "\n\n")

    for label, off, virt_rva, obj_addr in CANDIDATES:
        f.write("=" * 78 + "\n")
        f.write("off=0x%03x  %s  first_virt_RVA=0x%x  candidate_obj=0x%x\n"
                % (off, label, virt_rva, obj_addr))

        virt_abs = IMAGE_BASE + virt_rva
        fn = fm.getFunctionAt(addr(virt_abs))
        if fn:
            f.write("  first_virt: %s @ 0x%x\n" % (fn.getName(), virt_abs))
        else:
            f.write("  first_virt: (no Ghidra fn at 0x%x)\n" % virt_abs)

        # Find vtables containing this address as the first slot.
        vtbls = find_vtable_for_first_virt(virt_abs)
        f.write("  vtable candidates (.rdata addrs where first qword == 0x%x): %d found\n"
                % (virt_abs, len(vtbls)))
        for vt in vtbls:
            f.write("    vtable @ 0x%x (RVA 0x%x)\n" % (vt, vt - IMAGE_BASE))

            # Who writes this vtable address? That's the constructor.
            writers = find_writers_of_address(vt)
            f.write("      %d xref(s) to this vtable\n" % len(writers))
            for from_a, wfn in writers[:8]:
                if wfn:
                    f.write("        from %s in fn %s @ 0x%x\n"
                            % (from_a, wfn.getName(), wfn.getEntryPoint().getOffset()))
                    strs = strings_in_function(wfn, limit=6)
                    for sa, sv in strs:
                        snippet = sv if len(sv) <= 90 else (sv[:87] + "...")
                        f.write("          str 0x%x  \"%s\"\n" % (sa, snippet))
                else:
                    f.write("        from %s (no fn)\n" % from_a)
        f.write("\n")

print("Wrote %s" % OUT)
