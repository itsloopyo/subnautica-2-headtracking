# ProcessEvent allocas Function->PropertiesSize: a uint16 read from the
# UFunction (2nd arg, RDX) feeds the stack probe. Find probe call-sites whose
# size register was set by MOVZX <reg>, word ptr [<reg>+off], and dump those
# functions' prologues for eyeballing.

OUT   = r"C:\tmp\sub2_pe_v3.txt"
BASE  = 0x149667b90
IMG   = 0x140000000
PROBE = BASE

mem     = currentProgram.getMemory()
fact    = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm      = currentProgram.getFunctionManager()
ref_mgr = currentProgram.getReferenceManager()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

hits = []
for r in ref_mgr.getReferencesTo(addr(PROBE)):
    if not r.getReferenceType().isCall():
        continue
    site = r.getFromAddress()
    fn = fm.getFunctionContaining(site)
    if not fn:
        continue
    # walk back up to 16 instrs to see what set the size (EAX/RAX/ECX/RCX)
    ins = listing.getInstructionBefore(site)
    movzx_word = None
    steps = 0
    while ins and steps < 16:
        s = ins.toString()
        if s.startswith("MOVZX") and "word ptr [" in s and (
                "EAX" in s.split(",")[0] or "ECX" in s.split(",")[0]
                or "RAX" in s.split(",")[0] or "RCX" in s.split(",")[0]):
            movzx_word = s
            break
        # stop if we hit a clear immediate size set
        if (s.startswith("MOV EAX,0x") or s.startswith("MOV ECX,0x")
                or s.startswith("MOV RAX,0x")):
            break
        ins = listing.getInstructionBefore(ins.getAddress())
        steps += 1
    if movzx_word:
        hits.append((fn.getEntryPoint().getOffset(), movzx_word,
                     fn.getBody().getNumAddresses()))

# dedupe by function
seen = {}
for ep, mz, size in hits:
    seen.setdefault(ep, (mz, size))

def prologue(ep, n=14):
    out = []
    fn = fm.getFunctionContaining(addr(ep))
    c = 0
    for i in listing.getInstructions(fn.getBody(), True):
        out.append("    +0x%08x %s" % (i.getAddress().getOffset()-IMG, i.toString()))
        c += 1
        if c >= n: break
    return out

with open(OUT, "w") as f:
    f.write("variable-alloca (MOVZX word) probe callers: %d\n" % len(seen))
    f.write("=" * 70 + "\n")
    for ep, (mz, size) in sorted(seen.items(), key=lambda kv: -kv[1][1])[:12]:
        f.write("\nfn RVA 0x%08x size=%d  alloca-size: %s\n" % (ep-IMG, size, mz))
        for line in prologue(ep):
            f.write(line + "\n")

print("Wrote %s  (%d candidate fns)" % (OUT, len(seen)))
