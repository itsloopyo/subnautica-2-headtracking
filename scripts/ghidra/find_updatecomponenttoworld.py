# Find USceneComponent::UpdateComponentToWorld(WithParent) - the engine
# function that recomputes a component's cached world transform and writes it
# to ComponentToWorld (FTransform @ +0x1f0 in this SN2 build). We hook this,
# filter to our marked mask components, and re-apply mask compensation in its
# post so our write is the last one before render (kills the stomp race).
#
# Strategy: walk the known component vtables (SkeletalMesh, Capsule - both
# USceneComponent subclasses). For each slot's function, count memory operands
# whose displacement lands in the ComponentToWorld FTransform block
# (0x1f0..0x230) and whether it touches AttachParent (+0x120). The slot that
# scores high on BOTH, identically across both vtables (shared base impl), is
# UpdateComponentToWorld(WithParent).

from ghidra.program.model.scalar import Scalar

OUT = r"C:\tmp\sub2_updatecomponenttoworld.txt"
IMAGE_BASE = 0x140000000

# RVAs from ghidra_offsets.h VTables namespace.
VTABLES = [
    ("SkeletalMeshComponent", 0x0a2e2a48),
    ("CapsuleComponent",      0x0a2a2748),
    ("CameraMountComponent",  0x0aeedfa8),
]
SLOTS = 170

# ComponentToWorld FTransform block (Rotation+Translation+Scale, 96 bytes).
CTW_DISPS = set(range(0x1f0, 0x238, 8))   # 0x1f0,0x1f8,...0x230
ATTACH_PARENT = 0x120
MAX_INS = 600   # bound per function

mem = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"

def safe_qword(a):
    try: return mem.getLong(a) & 0xffffffffffffffff
    except: return None

def scan_fn(entry_va):
    """Return (ctw_write_hits, ctw_disps_seen, touches_attach, n_ins)."""
    fn = fm.getFunctionAt(addr(entry_va))
    if fn is None:
        return (0, set(), False, 0)
    ctw_hits = 0
    disps = set()
    attach = False
    n = 0
    for ins in listing.getInstructions(fn.getBody(), True):
        n += 1
        if n > MAX_INS:
            break
        for i in range(ins.getNumOperands()):
            for obj in ins.getOpObjects(i):
                if isinstance(obj, Scalar):
                    val = obj.getUnsignedValue()
                    if val in CTW_DISPS:
                        disps.add(val)
                        # count it as a write hit if this operand is a dest
                        # (operand 0 on a store-shaped mnemonic)
                        mnem = ins.getMnemonicString().lower()
                        if mnem.startswith("mov") and i == 0:
                            ctw_hits += 1
                        else:
                            ctw_hits += 1  # any touch counts; we rank later
                    elif val == ATTACH_PARENT:
                        attach = True
    return (ctw_hits, disps, attach, n)

results = {}  # slot -> per-vtable info

with open(OUT, "w") as f:
    f.write("Hunt for UpdateComponentToWorld(WithParent)\n")
    f.write("CTW FTransform block disps: %s\n" % sorted("0x%x" % d for d in CTW_DISPS))
    f.write("=" * 80 + "\n\n")

    for vt_name, vt_rva in VTABLES:
        vt_va = IMAGE_BASE + vt_rva
        f.write("### vtable %s @ 0x%x (RVA 0x%x)\n" % (vt_name, vt_va, vt_rva))
        for slot in range(SLOTS):
            ptr = safe_qword(addr(vt_va + slot * 8))
            if ptr is None or ptr == 0:
                f.write("  [slot %d] end (null)\n" % slot)
                break
            if block_name(addr(ptr)) != ".text":
                continue
            hits, disps, attach, n = scan_fn(ptr)
            if hits == 0 and not attach:
                continue
            score = hits + (5 if attach else 0) + len(disps)
            f.write("  [slot %3d] fn=0x%x  ctw_touch=%d  disps=%s  attachParent=%s  ins=%d  score=%d\n"
                    % (slot, ptr, hits,
                       ",".join("0x%x" % d for d in sorted(disps)) or "-",
                       "Y" if attach else "n", n, score))
            results.setdefault(slot, []).append((vt_name, ptr, score, sorted(disps), attach))
        f.write("\n")

    # Slots that score on BOTH SkeletalMesh and Capsule are the strongest:
    # they're the shared USceneComponent base implementation.
    f.write("=" * 80 + "\n")
    f.write("Cross-vtable candidates (touch CTW block in >=2 vtables):\n")
    for slot in sorted(results):
        rows = results[slot]
        if len(rows) < 2:
            continue
        f.write("  slot %3d:\n" % slot)
        for vt_name, ptr, score, disps, attach in rows:
            f.write("    %-22s fn=0x%x  score=%d  disps=%s  attach=%s\n"
                    % (vt_name, ptr, score,
                       ",".join("0x%x" % d for d in disps) or "-",
                       "Y" if attach else "n"))

print("Wrote %s" % OUT)
