# Disassemble the FMinimalViewInfo builder (containing fn of the render caller
# at RVA 0x04171af7) to find where it reads FOV/AspectRatio. The builder derefs
# controller+0x368 (PlayerCameraManager) and copies loc/rot/FOV/aspect into a
# local FMinimalViewInfo. We want the [reg+disp] offset the FOV float is read
# from, relative to the PCM (or relative to whatever struct holds the cached
# POV), so the mod can read the live FOV at runtime instead of hardcoding 90.

OUT = r"C:\tmp\sub2_fov_builder.txt"

IMAGE_BASE = 0x140000000
CALL_SITE_RVA = 0x04171af7   # render GPV caller (mode 2)

fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()
sym_tab = currentProgram.getSymbolTable()


def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)


def block_name(a):
    blk = mem.getBlock(a)
    return blk.getName() if blk else "?"


call_site = addr(IMAGE_BASE + CALL_SITE_RVA)
func = fm.getFunctionContaining(call_site)

with open(OUT, "w") as f:
    if func is None:
        f.write("no function contains call site 0x%x\n" % (IMAGE_BASE + CALL_SITE_RVA))
    else:
        ep = func.getEntryPoint()
        f.write("function %s @ %s  rva=0x%x  size=%d\n\n" % (
            func.getName(), ep, ep.getOffset() - IMAGE_BASE,
            func.getBody().getNumAddresses()))

        ins = listing.getInstructionAt(ep)
        n = 0
        while ins is not None and func.getBody().contains(ins.getAddress()):
            a = ins.getAddress()
            mark = " <== GPV CALL SITE (ret)" if a.getOffset() == (IMAGE_BASE + CALL_SITE_RVA) else ""
            # Flag float-shaped moves and call targets.
            mn = ins.getMnemonicString().lower()
            extra = ""
            if mn in ("movss", "movsd", "cvtss2sd", "cvtsd2ss", "mulss", "addss"):
                extra += "   [FLOAT]"
            for i in range(ins.getNumOperands()):
                for r in ins.getOperandReferences(i):
                    t = r.getToAddress()
                    if t is None:
                        continue
                    sym = sym_tab.getPrimarySymbol(t)
                    sname = sym.getName() if sym else "-"
                    extra += "  ; ->0x%x[%s]%s" % (t.getOffset(), block_name(t), sname)
            f.write("  +0x%-6x %s   %s%s%s\n" % (
                a.getOffset() - IMAGE_BASE, a, ins.toString(), extra, mark))
            ins = ins.getNext()
            n += 1
            if n > 600:
                f.write("  ...truncated at 600 instrs\n")
                break

print("Wrote %s" % OUT)
