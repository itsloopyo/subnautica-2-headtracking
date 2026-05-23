# Find USceneComponent::RelativeRotation offset (FRotator). Same technique as
# AttachParent: locate the property registration (FObjectPropertyParams-style
# struct whose first qword points at the "RelativeRotation" C string); the
# Offset field sits at struct+0x32 (calibrated: RelativeLocation reads 0x148).
OUT = r"C:\tmp\sub2_relrot.txt"
NAMES = ["RelativeRotation", "RelativeLocation", "RelativeScale3D"]
mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
def addr(v): return fact.getDefaultAddressSpace().getAddress(v)
def bn(a):
    b = mem.getBlock(a); return b.getName() if b else "?"
def qw(a):
    try: return mem.getLong(a) & 0xffffffffffffffff
    except: return None
def u16(a):
    try: return mem.getShort(a) & 0xffff
    except: return None
def find_string(s):
    t = bytearray(s + "\x00","ascii"); m = bytearray([0xff]*len(t)); hits=[]
    start = currentProgram.getMinAddress()
    while True:
        f = mem.findBytes(start, bytes(t), bytes(m), True, monitor)
        if f is None: break
        hits.append(f.getOffset()); start = f.add(1)
        if len(hits) >= 24: break
    return hits
def refs(val):
    out=[]
    for b in mem.getBlocks():
        if b.getName() not in (".rdata",".data"): continue
        cur=b.getStart().getOffset(); end=b.getEnd().getOffset()
        while cur+8<=end:
            if qw(addr(cur))==val: out.append(cur)
            cur+=8
            if len(out)>=24: return out
    return out
with open(OUT,"w") as f:
    for nm in NAMES:
        f.write("="*70+"\n%s\n"%nm)
        for sa in find_string(nm):
            for st in refs(sa):
                off = u16(addr(st+0x32))
                f.write("  struct@0x%x [%s]  Offset(+0x32)=0x%x\n"%(st,bn(addr(st)), off if off else 0))
print("Wrote %s"%OUT)
