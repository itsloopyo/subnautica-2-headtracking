# Find the GPV render caller statically. GPV is virtual, so there are no
# static call references to it - the render call site is `call [reg+0x828]`
# (PCM vtable slot for GetPlayerViewPoint) preceded in the same function by
# `call [reg+0x7f8]` (the vfn that writes FOV into the FMinimalViewInfo being
# assembled). Both slot offsets are UE5.6.1 engine-ABI constants, stable
# across content patches; only the code addresses move. The retRVA reported
# for the +0x828 call site is what kKnownCallerRvas[1] (inject mode 2) needs.
#
# Confidence markers per candidate:
#   - [reg+0x368] deref in the same fn (controller -> PlayerCameraManager)
#   - movss [reg+0x30] write (FOV into the FMinimalViewInfo out-struct)
OUT  = r"C:\tmp\sub2_render_caller.txt"
BASE = 0x140000000

from ghidra.util.task import ConsoleTaskMonitor

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

monitor = ConsoleTaskMonitor()

# call qword ptr [reg+disp32]: FF /2 with ModRM 0x90..0x97 (rax..rdi),
# plus REX-prefixed forms for r8-r15 (41 FF 90..97).
def find_vfn_calls(disp_le_bytes):
    """Return RVAs of every `call [reg+disp32]` with the given displacement."""
    hits = []
    pattern = bytearray([0xFF, 0x90]) + bytearray(disp_le_bytes)
    mask    = bytearray([0xFF, 0xF8]) + bytearray([0xFF, 0xFF, 0xFF, 0xFF])
    a = currentProgram.getMinAddress()
    while True:
        a = mem.findBytes(a, pattern, mask, True, monitor)
        if a is None: break
        hits.append(a.getOffset() - BASE)
        a = a.add(1)
    return hits

disp_828 = [0x28, 0x08, 0x00, 0x00]
disp_7f8 = [0xf8, 0x07, 0x00, 0x00]

with open(OUT, "w") as f:
    f.write("Render-caller scan: call [reg+0x7f8] .. call [reg+0x828] in one fn\n")
    f.write("=" * 78 + "\n\n")

    sites_828 = find_vfn_calls(disp_828)
    sites_7f8 = find_vfn_calls(disp_7f8)
    f.write("call [reg+0x828] sites: %d\n" % len(sites_828))
    f.write("call [reg+0x7f8] sites: %d\n\n" % len(sites_7f8))

    fns_7f8 = {}
    for rva in sites_7f8:
        fn = fm.getFunctionContaining(addr(BASE + rva))
        if fn is not None:
            fns_7f8.setdefault(fn.getEntryPoint().getOffset() - BASE, []).append(rva)

    f.write("## Candidates (both vfn calls in the same function)\n")
    for rva in sites_828:
        site = addr(BASE + rva)
        fn = fm.getFunctionContaining(site)
        if fn is None: continue
        fn_rva = fn.getEntryPoint().getOffset() - BASE
        if fn_rva not in fns_7f8: continue

        # getInstructionContaining handles REX-prefixed encodings (41 FF 90 ..)
        # where the instruction starts one byte before the matched FF 90.
        ins = listing.getInstructionContaining(site)
        if ins is None:
            # Analysis did not disassemble this byte run; report with the
            # plain encoded length of the matched pattern (6 bytes).
            ret_rva = rva + 6
            note = " (no instruction at site - undisassembled)"
        else:
            ret_rva = ins.getAddress().getOffset() + ins.getLength() - BASE
            note = ""

        # Confidence markers inside the containing function.
        has_368 = False
        has_fov_write = False
        n = 0
        for i in listing.getInstructions(fn.getBody(), True):
            n += 1
            if n > 4000: break
            t = i.toString()
            if "0x368]" in t: has_368 = True
            if i.getMnemonicString().upper() == "MOVSS" and "+ 0x30]" in t:
                has_fov_write = True

        f.write("\n  fn 0x%08x  call@0x%08x  retRVA 0x%08x%s\n" %
                (fn_rva, rva, ret_rva, note))
        f.write("    [reg+0x7f8] calls in fn: %s\n" %
                ", ".join("0x%08x" % x for x in fns_7f8[fn_rva]))
        f.write("    PCM deref [reg+0x368]: %s   FOV write movss [reg+0x30]: %s\n" %
                ("YES" if has_368 else "no", "YES" if has_fov_write else "no"))

print("Wrote %s" % OUT)
