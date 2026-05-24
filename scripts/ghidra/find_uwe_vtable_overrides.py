# Compare AUWEPlayerCameraManager's vtable to its parent (APlayerCameraManager)
# vtable and identify the slots where UWE overrode something. UWE overrides
# are the high-value hook targets - those are the per-frame customizations
# Subnautica 2's camera code adds on top of stock UE5.
#
# We don't have a confirmed parent-vtable address yet. The constructor at
# 0x146304170 invokes 0x1443ceee0 (APlayerCameraManager destructor). Its
# vtable is the same one we'd find by scanning xrefs to APlayerCameraManager's
# constructor. We'll fall back to flagging "any slot pointing at a function
# in the UWE plugin module (0x14630_____ range)" as a UWE override.

OUT = r"C:\tmp\sub2_uwe_overrides.txt"

VTABLE_ADDR = 0x14ac694d0
VTABLE_SLOTS = 220  # generous bound; we'll print stop-at-non-text

# Module ranges (from .text block start 0x140001000, .rdata 0x149819000).
# The Subnautica2 EXE has been compiled module-by-module; the UWE camera
# plugin lives roughly in the 0x14630____ neighbourhood of .text.
UWE_PLUGIN_LO = 0x146000000
UWE_PLUGIN_HI = 0x147000000

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
sym_tab = currentProgram.getSymbolTable()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
ref_mgr = currentProgram.getReferenceManager()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_qword(a):
    try: return mem.getLong(a) & 0xffffffffffffffff
    except: return None

# --- Step 1: dump the vtable, annotate UWE plugin members ---
overrides = []   # (slot_index, slot_offset, ptr, fname)
all_slots  = []
for i in range(VTABLE_SLOTS):
    off = i * 8
    a = addr(VTABLE_ADDR + off)
    v = safe_qword(a)
    if v is None or v == 0:
        break
    fn = fm.getFunctionContaining(addr(v))
    fname = fn.getName() if fn else "?"
    bn = block_name(addr(v)) if v else "?"
    all_slots.append((i, off, v, fname, bn))
    if UWE_PLUGIN_LO <= v < UWE_PLUGIN_HI:
        overrides.append((i, off, v, fname))

# --- Step 2: locate UFunction registrations for GetCameraView and
#     other camera-relevant FNames so we can correlate to vtable indices ---
UFUNC_TARGETS = [
    "GetCameraView",
    "UpdateViewTarget",
    "DoUpdateCamera",
    "ProcessViewRotation",
    "CalcCamera",
    "ApplyWorldOffset",
    "BlueprintUpdateCamera",
    "OnPhotographyMultiPartCaptureStart",
    "GetCameraLocation",
    "GetCameraRotation",
]

string_locs = {n: [] for n in UFUNC_TARGETS}
for data in listing.getDefinedData(True):
    if not data.hasStringValue():
        continue
    try:
        s = str(data.getValue())
    except:
        continue
    if s in string_locs:
        string_locs[s].append(data.getAddress())

ufunc_code_refs = {}   # name -> [(from_addr, fn_entry)]
for name, locs in string_locs.items():
    for sloc in locs:
        for r in ref_mgr.getReferencesTo(sloc):
            from_a = r.getFromAddress()
            if block_name(from_a) != ".text":
                continue
            fn = fm.getFunctionContaining(from_a)
            ufunc_code_refs.setdefault(name, []).append(
                (from_a.getOffset(), fn.getEntryPoint().getOffset() if fn else None,
                 fn.getName() if fn else "?"))

with open(OUT, "w") as f:
    f.write("AUWEPlayerCameraManager vtable @ 0x%x  (%d slots dumped)\n" % (VTABLE_ADDR, len(all_slots)))
    f.write("=" * 80 + "\n\n")

    f.write("UWE-plugin (0x14630____) overrides:\n")
    if not overrides:
        f.write("  (none found in plugin range; UWE may not override past destructor)\n")
    for i, off, v, fname in overrides:
        f.write("  [%-3d] +0x%03x  0x%016x  %s\n" % (i, off, v, fname))

    f.write("\nFull vtable dump:\n")
    for i, off, v, fname, bn in all_slots:
        marker = "  ** UWE **" if (UWE_PLUGIN_LO <= v < UWE_PLUGIN_HI) else ""
        f.write("  [%-3d] +0x%03x  0x%016x  [%s]  %s%s\n" % (i, off, v, bn, fname, marker))

    f.write("\n\nUFunction name xrefs (for vtable index correlation):\n")
    for name in UFUNC_TARGETS:
        refs = ufunc_code_refs.get(name, [])
        f.write("  %s : %d code xref(s)\n" % (name, len(refs)))
        for from_a, fn_entry, fn_name in refs:
            f.write("    from 0x%x  fn=%s @ 0x%x\n" % (
                from_a, fn_name, fn_entry if fn_entry else 0))

print("Wrote %s" % OUT)
