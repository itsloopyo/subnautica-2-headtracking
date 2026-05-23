# Subnautica 2: enumerate MSVC RTTI type descriptors for any class whose
# name contains "Camera", then walk TypeDescriptor -> CompleteObjectLocator
# (-> ClassHierarchyDescriptor) -> vtable[0..15]. Dumps the vtable contents.
#
# UE5 shipping builds emit MSVC RTTI for polymorphic C++ classes (UCLASS or
# otherwise) because /GR is on by default. The .?AV<name>@@ mangling holds.
#
# MSVC RTTI layout (x64):
#   TypeDescriptor:
#     +0x00  vftable ptr (type_info vtable)
#     +0x08  spare
#     +0x10  name string (.?AV<class>@@\0)
#   CompleteObjectLocator (lives at vtable[-1]):
#     +0x00  signature (1 on x64)
#     +0x04  offset
#     +0x08  cdOffset
#     +0x0C  pTypeDescriptor  (RVA from imageBase)
#     +0x10  pClassDescriptor (RVA)
#     +0x14  pSelf            (RVA)  -- only on x64; lets us reverse-RVA
#
# We find TypeDescriptors by scanning .data for the mangled name pattern,
# then back up 0x10 to land on the TD start. Then we sweep .rdata for a
# 4-byte RVA equal to (TD - imageBase) at +0xC of any candidate COL.

import struct

OUT = r"C:\tmp\sub2_camera_rtti.txt"
NAME_FILTER = "Camera"

mem  = currentProgram.getMemory()
fact = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
ref_mgr = currentProgram.getReferenceManager()
image_base = currentProgram.getImageBase().getOffset()

def addr(v):
    return fact.getDefaultAddressSpace().getAddress(v)

def read_u32(a):
    try: return mem.getInt(a) & 0xffffffff
    except: return None

def read_u64(a):
    try: return mem.getLong(a) & 0xffffffffffffffff
    except: return None

def read_cstring_at(a, max_len=256):
    out = []
    cur = a
    for _ in range(max_len):
        try:
            b = mem.getByte(cur) & 0xff
        except:
            return None
        if b == 0:
            return "".join(chr(c) for c in out)
        if not (0x20 <= b <= 0x7e):
            return None
        out.append(b)
        cur = cur.add(1)
    return None

# ---------- Step 1: locate TypeDescriptors by their mangled name ----------
print("Scanning data sections for .?AV*Camera*@@ name strings...")
data_blocks = [b for b in mem.getBlocks()
               if b.isInitialized() and not b.isExecute()
               and b.getName() in (".data", ".rdata")]

td_candidates = []  # (td_address, mangled_name, demangled_hint)
for blk in data_blocks:
    start = blk.getStart().getOffset()
    end = blk.getEnd().getOffset()
    cur = start
    while cur < end - 4:
        try:
            b0 = mem.getByte(addr(cur)) & 0xff
            b1 = mem.getByte(addr(cur + 1)) & 0xff
            b2 = mem.getByte(addr(cur + 2)) & 0xff
            b3 = mem.getByte(addr(cur + 3)) & 0xff
        except:
            cur += 1
            continue
        if b0 == 0x2e and b1 == 0x3f and b2 == 0x41 and b3 == 0x56:
            s = read_cstring_at(addr(cur), 200)
            if s and s.endswith("@@") and NAME_FILTER in s:
                td_addr = cur - 0x10
                td_candidates.append((td_addr, s))
                cur += len(s) + 1
                continue
        cur += 1

print("  TD candidates: %d" % len(td_candidates))
if not td_candidates:
    with open(OUT, "w") as f:
        f.write("No TypeDescriptors matching '%s' found.\n" % NAME_FILTER)
    raise SystemExit

td_by_rva = {(td - image_base) & 0xffffffff: (td, name) for td, name in td_candidates}
td_rvas = set(td_by_rva.keys())

# ---------- Step 2: locate CompleteObjectLocators ----------
print("Scanning .rdata for CompleteObjectLocators...")
rdata = [b for b in mem.getBlocks()
         if b.isInitialized() and not b.isExecute() and b.getName() == ".rdata"]

# COL layout: signature(4) + offset(4) + cdOffset(4) + pTypeDesc(4) + ...
# Search for 32-bit RVA hits at align-4 positions; if +0x0C points to a
# known TD-RVA and the dword 0x0C bytes before that hit is 0 or 1
# (signature), accept it.
col_for_td = {}  # td_addr -> [col_addr,...]
for blk in rdata:
    start = blk.getStart().getOffset()
    end = blk.getEnd().getOffset()
    a = (start + 3) & ~3
    while a + 4 <= end:
        v = read_u32(addr(a))
        if v is not None and v in td_rvas:
            col_base = a - 0xC
            sig = read_u32(addr(col_base))
            if sig in (0, 1):
                td_addr, _ = td_by_rva[v]
                col_for_td.setdefault(td_addr, []).append(col_base)
        a += 4

# ---------- Step 3: vtable[-1] points at COL; find vtables ----------
print("Scanning data for vtables (8-byte ptr to a known COL)...")
col_addrs = {col: td for td, cols in col_for_td.items() for col in cols}
col_addr_set = set(col_addrs.keys())

vtable_for_td = {}  # td_addr -> [vt_addr,...]
scan_blocks = rdata + [b for b in mem.getBlocks()
                       if b.isInitialized() and not b.isExecute() and b.getName() == ".data"]
for blk in scan_blocks:
    start = blk.getStart().getOffset()
    end = blk.getEnd().getOffset()
    a = (start + 7) & ~7
    while a + 8 <= end:
        v = read_u64(addr(a))
        if v in col_addr_set:
            td = col_addrs[v]
            vt = a + 8
            vtable_for_td.setdefault(td, []).append(vt)
        a += 8

# ---------- Step 4: dump ----------
with open(OUT, "w") as f:
    f.write("Subnautica 2 - RTTI scan for classes matching %r\n" % NAME_FILTER)
    f.write("=" * 70 + "\n\n")
    for td_addr, name in sorted(td_candidates):
        f.write("TypeDescriptor 0x%x : %s\n" % (td_addr, name))
        cols = col_for_td.get(td_addr, [])
        if not cols:
            f.write("  (no COL found)\n\n")
            continue
        for col in cols:
            f.write("  COL 0x%x\n" % col)
            vts = vtable_for_td.get(td_addr, [])
            vts_for_col = []
            for vt in vts:
                col_ref = read_u64(addr(vt - 8))
                if col_ref == col:
                    vts_for_col.append(vt)
            for vt in vts_for_col:
                f.write("    vtable 0x%x\n" % vt)
                for i in range(16):
                    p = read_u64(addr(vt + i * 8))
                    if p is None:
                        break
                    note = ""
                    if p and (image_base <= p < image_base + 0x10000000):
                        sym = currentProgram.getSymbolTable().getPrimarySymbol(addr(p))
                        if sym:
                            note = "  -> %s" % sym.getName()
                    f.write("      [%2d] 0x%016x%s\n" % (i, p or 0, note))
        f.write("\n")

print("Wrote %s" % OUT)
