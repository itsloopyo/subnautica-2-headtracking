#Requires -Version 5.1
# Scan a rebased PE dump for likely FUObjectArray and FNamePool bases.
#
# Strategy:
#   * Find each section's (RVA, on-disk offset, size) by walking PE headers.
#   * For each candidate RVA (passed via -Candidates), read the 8 bytes at
#     [base+RVA] and the 4 bytes at [base+RVA+0x14]. The FUObjectArray base
#     has a heap pointer at +0 (high u64, 8-byte aligned, >> 0x10000 and
#     < 0x7fff_ffff_ffff) and a uint32 count at +0x14 in a sane range
#     (~10000 to ~10_000_000 for a UE5 title).
#   * For FNamePool: heap pointer at +0x10 (Blocks[0]), and Blocks[1+]
#     pointers monotonically valid.
#
# Outputs ranked candidates for each. Pick the highest-confidence and
# paste into builds/gdk_offsets.cpp.

param(
    [string]$DumpPath = '.\scratch\Subnautica2-WinGDK-Shipping-dumped.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $DumpPath)) { throw "Dump not found: $DumpPath" }
$bytes = [System.IO.File]::ReadAllBytes($DumpPath)
Write-Host "Loaded $($bytes.Length) bytes from $DumpPath" -ForegroundColor DarkGray

# Walk PE headers to find each section's disk range and its RVA mapping.
$e_lfanew = [BitConverter]::ToUInt32($bytes, 0x3c)
$opt = [int]($e_lfanew + 4 + 20)
$numSections = [BitConverter]::ToUInt16($bytes, $e_lfanew + 4 + 2)
$sizeOptHdr  = [BitConverter]::ToUInt16($bytes, $e_lfanew + 4 + 16)
$secBase = $opt + $sizeOptHdr

$sections = @()
for ($i = 0; $i -lt $numSections; ++$i) {
    $h = $secBase + ($i * 40)
    $name = [System.Text.Encoding]::ASCII.GetString($bytes[$h..($h+7)]).TrimEnd([char]0)
    $sections += [pscustomobject]@{
        Name     = $name
        VSize    = [BitConverter]::ToUInt32($bytes, $h + 8)
        VAddr    = [BitConverter]::ToUInt32($bytes, $h + 12)
        RSize    = [BitConverter]::ToUInt32($bytes, $h + 16)
        ROff     = [BitConverter]::ToUInt32($bytes, $h + 20)
    }
}

function Rva-To-Disk {
    param([uint32]$rva)
    foreach ($s in $script:sections) {
        if ($rva -ge $s.VAddr -and $rva -lt ($s.VAddr + $s.VSize)) {
            return ($s.ROff + ($rva - $s.VAddr))
        }
    }
    return -1
}

function Read-U64 { param([int]$off) [BitConverter]::ToUInt64($bytes, $off) }
function Read-U32 { param([int]$off) [BitConverter]::ToUInt32($bytes, $off) }

function Looks-Like-Heap-Pointer {
    param([uint64]$v)
    if ($v -lt 0x10000) { return $false }
    if ($v -gt 0x7fffffffffff) { return $false }
    if (($v -band 0x7) -ne 0) { return $false }
    return $true
}

# Scan .data section for FUObjectArray shape:
#   +0x00 heap pointer (chunk-array)
#   +0x14 uint32 count, range 10000..16000000
# Print all matches with their RVA, the pointer value, and the count.
$dataSec = $sections | Where-Object { $_.Name -eq '.data' } | Select-Object -First 1
if (-not $dataSec) { throw "No .data section in dump" }

Write-Host "`n=== FUObjectArray candidates (.data scan, shape: ptr@+0, num@+0x14) ===" -ForegroundColor Cyan
$matches = @()
$disk = [int]$dataSec.ROff
$end  = [int]($disk + $dataSec.RSize) - 0x20
for ($p = $disk; $p -lt $end; $p += 8) {
    $ptr = Read-U64 $p
    if (-not (Looks-Like-Heap-Pointer $ptr)) { continue }
    $num = Read-U32 ($p + 0x14)
    if ($num -lt 10000 -or $num -gt 16000000) { continue }
    # Also sanity-check: max should be >= num (typically equal or slightly larger), at +0x18
    $max = Read-U32 ($p + 0x18)
    if ($max -lt $num) { continue }
    if ($max -gt 32000000) { continue }
    $rva = $dataSec.VAddr + ($p - $dataSec.ROff)
    $matches += [pscustomobject]@{
        Rva       = '0x{0:x8}' -f $rva
        ChunkPtr  = '0x{0:x}' -f $ptr
        Num       = $num
        Max       = $max
    }
}
$matches | Format-Table -AutoSize

# Scan for FNamePool: layout per memory.md is `Blocks` array at pool+0x10,
# stride 2 (16-bit per slot? actually 64-bit pointers - the "stride 2" in
# memory refers to the ENTRY layout, not the Blocks array stride). Each
# Blocks[i] is a uint64 heap pointer. Validate by checking 2+ adjacent
# entries are heap pointers (Block 0 always populated; Blocks 1..N too as
# more names get interned).
Write-Host "`n=== FNamePool candidates (.data scan, shape: Blocks[] at +0x10) ===" -ForegroundColor Cyan
$poolMatches = @()
$end2 = [int]($disk + $dataSec.RSize) - 0x40
for ($p = $disk; $p -lt $end2; $p += 8) {
    $b0 = Read-U64 ($p + 0x10)
    $b1 = Read-U64 ($p + 0x18)
    if (-not (Looks-Like-Heap-Pointer $b0)) { continue }
    if (-not (Looks-Like-Heap-Pointer $b1)) { continue }
    # The blocks are typically far apart (each block is megabytes).
    $delta = if ($b1 -gt $b0) { $b1 - $b0 } else { $b0 - $b1 }
    if ($delta -lt 0x10000 -or $delta -gt 0x100000000) { continue }
    $rva = $dataSec.VAddr + ($p - $dataSec.ROff)
    # Count how many adjacent blocks look valid - a real pool has several.
    $validBlocks = 2
    for ($k = 2; $k -lt 8; ++$k) {
        $bx = Read-U64 ($p + 0x10 + ($k * 8))
        if (Looks-Like-Heap-Pointer $bx) { $validBlocks++ } else { break }
    }
    if ($validBlocks -lt 3) { continue }
    $poolMatches += [pscustomobject]@{
        Rva           = '0x{0:x8}' -f $rva
        Block0        = '0x{0:x}' -f $b0
        Block1        = '0x{0:x}' -f $b1
        AdjValid      = $validBlocks
    }
}
$poolMatches | Format-Table -AutoSize
