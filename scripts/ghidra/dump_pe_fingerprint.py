# Read PE TimeDateStamp / SizeOfImage / CheckSum from the analyzed program's
# in-memory Headers block. Used to capture the build fingerprint the current
# offsets were derived from, before re-importing a newer patched binary.
OUT  = r"C:\tmp\sub2_pe_fingerprint.txt"
BASE = 0x140000000

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()

def addr(v): return fact.getDefaultAddressSpace().getAddress(v)

def u16(a): return mem.getShort(addr(a)) & 0xFFFF
def u32(a): return mem.getInt(addr(a)) & 0xFFFFFFFF

e_lfanew = u32(BASE + 0x3c)
coff     = BASE + e_lfanew + 4
timestamp = u32(coff + 4)
size_opt  = u16(coff + 16)
opt       = coff + 20
magic     = u16(opt)              # 0x20b = PE32+
size_img  = u32(opt + 56)
checksum  = u32(opt + 64)

lines = [
    "image base    = 0x%x" % BASE,
    "e_lfanew      = 0x%x" % e_lfanew,
    "OptMagic      = 0x%x (0x20b=PE32+)" % magic,
    "TimeDateStamp = 0x%08x" % timestamp,
    "SizeOfImage   = 0x%08x" % size_img,
    "CheckSum      = 0x%08x" % checksum,
]
with open(OUT, "w") as f:
    f.write("\n".join(lines) + "\n")
for ln in lines:
    print(ln)
print("Wrote %s" % OUT)
