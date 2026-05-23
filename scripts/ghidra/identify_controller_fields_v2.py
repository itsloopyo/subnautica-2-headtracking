# Pass 2 at identifying APlayerController fields. For each runtime-captured
# vtable RVA (the value at *(self + offset) for a UObject field), find the
# xrefs that write that vtable into objects. Those xrefs land in the class
# constructor and Z_Construct_UClass_X_Statics. Z_Construct functions
# embed the class name as a string, so grep the nearby instructions for
# any 4+ char ASCII string in .rdata and report it.

OUT = r"C:\tmp\sub2_controller_fields_v2.txt"

IMAGE_BASE = 0x140000000

# (label, slot offset in APlayerController, VTABLE rva captured at runtime,
#  candidate obj address - heap pointer for sanity check)
CANDIDATES = [
    # Pawn-internal subobjects (offsets relative to APawn, captured via deep probe):
    ("Pawn_off_0x338_charmove?", 0x338, 0x0a3544e0, 0x18769c6d7c0),
    ("Pawn_off_0x340_capsule?",  0x340, 0x0a3033d0, 0x1845e604c90),
    ("Pawn_off_0x348_mesh?",     0x348, 0x0a301c78, 0x184a2e5e5c0),
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

def strings_in_function(fn, limit=20):
    if fn is None:
        return []
    seen_addrs = set()
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
                if t.getOffset() in seen_addrs:
                    continue
                s = read_cstring(t)
                if s and len(s) >= 4:
                    seen_addrs.add(t.getOffset())
                    seen.append((t.getOffset(), s))
                    if len(seen) >= limit:
                        return seen
        ins = ins.getNext()
    return seen

def identify_vtable(vtbl_rva):
    """Return (writer_function, strings_in_that_function) lists."""
    vtbl_abs = IMAGE_BASE + vtbl_rva
    refs = ref_mgr.getReferencesTo(addr(vtbl_abs))
    writers = []
    for r in refs:
        from_a = r.getFromAddress()
        fn = fm.getFunctionContaining(from_a)
        writers.append((from_a, fn))
    return vtbl_abs, writers

with open(OUT, "w") as f:
    f.write("Subnautica 2: APlayerController field identification (pass 2)\n")
    f.write("=" * 78 + "\n\n")

    for label, off, vtbl_rva, obj_addr in CANDIDATES:
        f.write("=" * 78 + "\n")
        f.write("off=0x%03x  %s  vtbl_RVA=0x%x  vtbl_abs=0x%x  obj=0x%x\n"
                % (off, label, vtbl_rva, IMAGE_BASE + vtbl_rva, obj_addr))

        # First check: is the vtable itself in our binary? (For external
        # modules like CoreUObject.dll the abs is outside our image.)
        vtbl_abs = IMAGE_BASE + vtbl_rva
        bn = block_name(addr(vtbl_abs))
        f.write("  vtbl is in block: %s\n" % bn)

        # Look up xrefs to this vtable address.
        _, writers = identify_vtable(vtbl_rva)
        f.write("  %d xref(s) to this vtable\n" % len(writers))

        # Dedup by function entry.
        by_fn = {}
        for from_a, fn in writers:
            if fn is None:
                key = ("?", from_a.getOffset())
            else:
                key = (fn.getName(), fn.getEntryPoint().getOffset())
            by_fn.setdefault(key, []).append(from_a)

        # Print each unique function, with the strings it references.
        for (fname, fentry), sites in by_fn.items():
            f.write("    fn %s @ 0x%x  (%d xref sites)\n"
                    % (fname, fentry, len(sites)))
            if fname == "?":
                continue
            fn = fm.getFunctionAt(addr(fentry))
            strs = strings_in_function(fn, limit=10)
            if strs:
                for sa, sv in strs:
                    snippet = sv if len(sv) <= 100 else (sv[:97] + "...")
                    f.write("      str 0x%x  \"%s\"\n" % (sa, snippet))

        # Also: scan a small window around the vtable in .rdata for any
        # adjacent strings (UE often emits the class name string in a
        # nearby data block).
        f.write("  --- adjacent .rdata strings within ~0x100 of vtable ---\n")
        adj_seen = set()
        scan_start = vtbl_abs - 0x100
        scan_end   = vtbl_abs + 0x300
        cur = scan_start
        while cur < scan_end:
            s = read_cstring(addr(cur))
            if s and len(s) >= 4 and 'Z_Construct' not in s:
                if cur not in adj_seen:
                    adj_seen.add(cur)
                    snippet = s if len(s) <= 80 else (s[:77] + "...")
                    f.write("    @ 0x%x  \"%s\"\n" % (cur, snippet))
                cur += len(s) + 1
            else:
                cur += 1
        f.write("\n")

print("Wrote %s" % OUT)
