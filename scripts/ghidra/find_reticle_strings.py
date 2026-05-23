# Subnautica 2: hunt for the on-screen reticle / crosshair.
#
# UE5 UMG builds keep blueprint asset paths, widget class names, and
# __FUNCTION__ strings in .rdata. Find every string that smells like a
# crosshair / reticle / HUD widget, then attribute each to the function
# that references it so we have concrete code addresses to disassemble.
#
# Output: a ranked report of candidate strings + owning functions.

OUT = r"C:\tmp\sub2_reticle_strings.txt"

# Case-insensitive substrings of interest. Broad on purpose - we'd rather
# over-collect and eyeball the report than miss the asset.
NEEDLES = [
    "reticle", "reticule", "crosshair", "cross_hair",
    "aimdot", "aim_dot", "aimpoint",
    "wbp_", "w_hud", "wbp_hud", "hudwidget", "playerhud",
    "/game/", ".widget", "userwidget",
    "setvisibility", "eslatevisibility",
]

mem     = currentProgram.getMemory()
listing = currentProgram.getListing()
ref_mgr = currentProgram.getReferenceManager()
fm      = currentProgram.getFunctionManager()
base    = currentProgram.getImageBase().getOffset()

hits = []  # (addr, value)
for data in listing.getDefinedData(True):
    if not data.hasStringValue():
        continue
    try:
        s = str(data.getValue())
    except:
        continue
    low = s.lower()
    for n in NEEDLES:
        if n in low:
            hits.append((data.getAddress(), s))
            break

# Attribute each string to referencing functions.
rows = []  # (string, [(func_rva, count)...])
for saddr, sval in hits:
    fn_counts = {}
    for r in ref_mgr.getReferencesTo(saddr):
        fn = fm.getFunctionContaining(r.getFromAddress())
        if not fn:
            continue
        rva = fn.getEntryPoint().getOffset() - base
        fn_counts[rva] = fn_counts.get(rva, 0) + 1
    rows.append((sval, saddr.getOffset() - base, fn_counts))

with open(OUT, "w") as f:
    f.write("Subnautica2 reticle/crosshair/HUD string recon\n")
    f.write("image base 0x%x\n" % base)
    f.write("=" * 80 + "\n")
    f.write("matched strings: %d\n\n" % len(rows))
    # Strings with referencing code first (more actionable), then orphans.
    rows.sort(key=lambda r: (len(r[2]) == 0, r[0].lower()))
    for sval, srva, fn_counts in rows:
        disp = sval if len(sval) <= 100 else sval[:100] + "..."
        f.write('"%s"  (str RVA 0x%x)\n' % (disp, srva))
        if fn_counts:
            for rva, c in sorted(fn_counts.items(), key=lambda kv: -kv[1])[:6]:
                f.write("    <- fn RVA 0x%08x  xrefs=%d\n" % (rva, c))
        else:
            f.write("    <- (no code xref)\n")
        f.write("\n")

print("Wrote %s  (%d strings)" % (OUT, len(rows)))
