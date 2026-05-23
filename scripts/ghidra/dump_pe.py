OUT=r"C:\tmp\sub2_pe_confirm.txt"; BASE=0x140000000
fact=currentProgram.getAddressFactory(); listing=currentProgram.getListing(); fm=currentProgram.getFunctionManager()
def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
ep=BASE+0x02313bf0; fn=fm.getFunctionContaining(addr(ep))
with open(OUT,"w") as f:
    f.write("ProcessEvent candidate @ RVA 0x2313bf0 size=%d\n"%fn.getBody().getNumAddresses())
    n=0
    for ins in listing.getInstructions(fn.getBody(),True):
        n+=1
        if n>80: break
        f.write("  +0x%08x  %s\n"%(ins.getAddress().getOffset()-BASE, ins.toString()))
print("done")
