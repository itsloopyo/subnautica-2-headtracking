# Subnautica 2: identify vtable slots in AUWEPlayerCameraManager by
# scanning for embedded method-name strings.
#
# UE shipping builds keep plenty of __FUNCTION__ / check() / ensure()
# format strings that contain fully-qualified method names like
# "APlayerCameraManager::DoUpdateCamera". Each such string is referenced
# from within the body of the method it names. Walk the xrefs back to
# their owning function, then cross-reference against the
# AUWEPlayerCameraManager vtable to learn which slot is which.
#
# Output: human-readable mapping of {method-name -> [vtable slot(s)]}.

OUT = r"C:\tmp\sub2_camera_method_slots.txt"

VTABLE_ADDR  = 0x14ac694d0
VTABLE_SLOTS = 220

# Anything containing one of these substrings is treated as interesting.
# We anchor on "::" so we only pick fully-qualified method names.
INTEREST_CLASSES = [
    "APlayerCameraManager::",
    "AUWEPlayerCameraManager::",
    "UCameraComponent::",
    "FMinimalViewInfo::",
    "APawn::",
    "APlayerController::",
]

mem      = currentProgram.getMemory()
fact     = currentProgram.getAddressFactory()
listing  = currentProgram.getListing()
ref_mgr  = currentProgram.getReferenceManager()
fm       = currentProgram.getFunctionManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

def read_u64(a):
    try: return mem.getLong(a) & 0xffffffffffffffff
    except: return None

# Step 1: read the vtable into {func_addr -> [slot,...]}
slot_of_func = {}
vtable_entries = []  # list of (slot, func_addr)
for i in range(VTABLE_SLOTS):
    p = read_u64(addr(VTABLE_ADDR + i * 8))
    if p is None or p == 0:
        break
    vtable_entries.append((i, p))
    slot_of_func.setdefault(p, []).append(i)

# Step 2: enumerate strings, filter for "<Class>::" substrings
hits = []  # (string_addr, string_value)
for data in listing.getDefinedData(True):
    if not data.hasStringValue():
        continue
    try:
        s = str(data.getValue())
    except:
        continue
    for needle in INTEREST_CLASSES:
        if needle in s:
            hits.append((data.getAddress(), s))
            break

# Step 3: walk xrefs to each string, attribute to the owning function
method_to_funcs = {}  # method-name-prefix -> {func_entry: count}
for saddr, sval in hits:
    for r in ref_mgr.getReferencesTo(saddr):
        from_a = r.getFromAddress()
        fn = fm.getFunctionContaining(from_a)
        if not fn:
            continue
        entry = fn.getEntryPoint().getOffset()
        bucket = method_to_funcs.setdefault(sval, {})
        bucket[entry] = bucket.get(entry, 0) + 1

# Step 4: write a report
with open(OUT, "w") as f:
    f.write("AUWEPlayerCameraManager vtable @ 0x%x  (%d slots)\n"
            % (VTABLE_ADDR, len(vtable_entries)))
    f.write("=" * 80 + "\n\n")

    f.write("Method-name strings found: %d\n\n" % len(hits))

    # Group by method name (the substring after "::" up to first space or '(')
    def method_key(s):
        for needle in INTEREST_CLASSES:
            if needle in s:
                tail = s.split(needle, 1)[1]
                for stop in (" ", "(", "\t", "\n", ":"):
                    i = tail.find(stop)
                    if i != -1:
                        tail = tail[:i]
                return needle + tail
        return s

    keyed = {}
    for sval, fn_counts in method_to_funcs.items():
        k = method_key(sval)
        agg = keyed.setdefault(k, {})
        for entry, c in fn_counts.items():
            agg[entry] = agg.get(entry, 0) + c

    f.write("Method name -> owning function(s) -> vtable slot(s):\n")
    f.write("-" * 80 + "\n")
    for method in sorted(keyed.keys()):
        fn_counts = keyed[method]
        # rank candidates by xref count (more refs => more likely the
        # actual implementation, not an external caller)
        ranked = sorted(fn_counts.items(), key=lambda kv: -kv[1])
        f.write("\n%s\n" % method)
        for entry, count in ranked[:5]:
            slots = slot_of_func.get(entry, [])
            slot_str = ", ".join("slot %d (+0x%x)" % (s, s * 8) for s in slots) or "not in vtable"
            f.write("  fn 0x%016x  xrefs=%d  %s\n" % (entry, count, slot_str))

print("Wrote %s" % OUT)
