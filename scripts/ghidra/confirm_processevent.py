OUT  = r"C:\tmp\sub2_processevent.txt"
BASE = 0x140000000
mem=currentProgram.getMemory(); fact=currentProgram.getAddressFactory()
listing=currentProgram.getListing(); fm=currentProgram.getFunctionManager()
def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
with open(OUT,"w") as f:
    for rva in [0x16b6510]:
        a=addr(BASE+rva); fn=fm.getFunctionContaining(a)
        f.write("ProcessEvent? @ RVA 0x%x size=%s\n" % (rva,
            "?" if fn is None else fn.getBody().getNumAddresses()))
        n=0
        for ins in listing.getInstructions(fn.getBody(),True):
            n+=1
            if n>40: break
            f.write("  +0x%08x  %s\n" % (ins.getAddress().getOffset()-BASE, ins.toString()))
print("Wrote %s" % OUT)
