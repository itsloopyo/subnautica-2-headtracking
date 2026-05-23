OUT=r"C:\tmp\sub2_pe_final.txt"; IMG=0x140000000
fact=currentProgram.getAddressFactory(); listing=currentProgram.getListing(); fm=currentProgram.getFunctionManager()
def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
ep=IMG+0x16b9e20; fn=fm.getFunctionContaining(addr(ep))
with open(OUT,"w") as f:
    f.write("vt[76] candidate ProcessEvent @ RVA 0x16b9e20 size=%d\n"%fn.getBody().getNumAddresses())
    n=0
    for ins in listing.getInstructions(fn.getBody(),True):
        n+=1
        if n>60: break
        f.write("  +0x%08x  %s\n"%(ins.getAddress().getOffset()-IMG, ins.toString()))
print("done")
