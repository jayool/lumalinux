# Ghidra postScript: PIC xref search for i386 PIC binaries.
# Resuelve manualmente las referencias `lea reg, [base + STRING_VA - GOT_BASE]`
# que Ghidra a veces no resuelve via su reference manager.
#
# Para cada anchor string, encuentra:
# 1. Su VA
# 2. Para cada funcion en .text con `call __i686.get_pc_thunk; add reg, imm32` prologue PIC
#    - Calcula GOT_BASE de esa funcion
#    - Busca `lea reg, [reg + (STRING_VA - GOT_BASE)]` en su body
# 3. Imprime funciones que la referencian + decompile

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import jpype

prog = currentProgram
mem = prog.getMemory()
fm = prog.getFunctionManager()
listing = prog.getListing()
af = prog.getAddressFactory()
ds = af.getDefaultAddressSpace()
mon = ConsoleTaskMonitor()
base = prog.getImageBase().getOffset()

needles = [
    "Package id 0",
    "CPackageInfoCache::ReadFromDisk",
    "%s/appcache/packageinfo.vdf",
    "No package info for packageID %u found",
    "PackageID : %u, change number : %u/%u",
]

# Bulk read .text
text_block = None
for blk in mem.getBlocks():
    if blk.getName() == ".text":
        text_block = blk
        break

text_size = text_block.getSize()
print("[*] Bulk reading .text ({} bytes) via chunked read...".format(text_size))
# Use 1MB chunks via Ghidra's getBytes() (returns Java byte array, convert per chunk)
chunks = []
CHUNK = 1024 * 1024
offset = 0
start_addr = text_block.getStart()
while offset < text_size:
    sz = min(CHUNK, text_size - offset)
    ba = jpype.JArray(jpype.JByte, 1)(sz)
    mem.getBytes(start_addr.add(offset), ba)
    chunks.append(bytes((b & 0xff) for b in ba))
    offset += sz
    if offset % (CHUNK * 10) == 0:
        print("   ...{}/{} bytes".format(offset, text_size))
text = b"".join(chunks)
text_start = text_block.getStart().getOffset()
print("[*] Got .text bytes")

def find_string(s):
    """Find string in initialized memory, return VA or None"""
    addr_set = mem.getAllInitializedAddressSet()
    found = mem.findBytes(addr_set.getMinAddress(), s.encode('ascii'), None, True, mon)
    return found.getOffset() if found else None

def find_function_starts():
    """Find all functions in .text via push ebp; mov ebp, esp prologue"""
    starts = []
    i = 0
    while i < len(text) - 3:
        if text[i] == 0x55 and text[i+1] == 0x89 and text[i+2] == 0xE5:
            # Check prev byte is c3 (RET), cc (INT3), 90 (NOP), or boundary
            if i == 0 or text[i-1] in (0xC3, 0xCC, 0x90, 0xC9, 0x5D):
                starts.append(i)
            i += 3
        else:
            i += 1
    return starts

def get_got_base(fn_offset):
    """Given a function start offset, find call __i686.get_pc_thunk + add reg, GOT_offset.
    Returns the GOT base VA computed by that prologue, or None if PIC prologue not found.
    PIC prologue is:
      55                 push ebp                ; 1 byte
      89 e5              mov ebp, esp            ; 2 bytes (sometimes after pushes)
      [push regs maybe]
      e8 XX XX XX XX     call get_pc_thunk       ; 5 bytes (return addr loaded into a reg)
      81 C? YY YY YY YY  add reg, GOT_offset     ; 6 bytes
    The reg gets _GLOBAL_OFFSET_TABLE_ address = call_target_va + GOT_offset
    But more simply: GOT_BASE = next_pc_after_call + GOT_offset
                              = (fn_va + call_offset + 5) + GOT_offset
    """
    # Scan first 60 bytes for call (e8) followed by add reg, imm32 (81 C? XX XX XX XX)
    for i in range(min(60, len(text) - fn_offset - 11)):
        idx = fn_offset + i
        if text[idx] == 0xE8:  # call rel32
            # Read 4-byte signed offset
            call_off = int.from_bytes(text[idx+1:idx+5], 'little', signed=True)
            call_target_offset = idx + 5 + call_off  # relative to text start
            # The PC after the call instruction is idx + 5
            ret_pc_va = text_start + idx + 5
            # Next instruction should be ADD reg, imm32 = 81 C? XX XX XX XX
            next_idx = idx + 5
            if next_idx + 6 <= len(text) and text[next_idx] == 0x81 and (text[next_idx+1] & 0xF8) == 0xC0:
                got_imm = int.from_bytes(text[next_idx+2:next_idx+6], 'little', signed=True)
                got_base = (ret_pc_va + got_imm) & 0xFFFFFFFF
                return got_base
    return None

def find_string_refs_in_fn(fn_offset, fn_end_offset, string_va, got_base):
    """Find LEA instructions in fn body that reference the string via got_base"""
    if got_base is None: return []
    target_offset = (string_va - got_base) & 0xFFFFFFFF
    # Could be positive or negative; check both
    target_offset_signed = string_va - got_base
    refs = []
    body = text[fn_offset:fn_end_offset]
    # Look for lea reg, [reg + imm32]: 8D modrm imm32
    # modrm: bits 7-6=10 (disp32 form), 5-3=dest reg, 2-0=src reg (base)
    for i in range(len(body) - 6):
        if body[i] == 0x8D:
            modrm = body[i+1]
            if (modrm & 0xC0) == 0x80:  # disp32 form
                disp = int.from_bytes(body[i+2:i+6], 'little', signed=True)
                if disp == target_offset_signed:
                    refs.append(fn_offset + i)
    return refs

print("[*] Finding function starts...")
fn_starts = find_function_starts()
print("[*] Found {} potential function starts".format(len(fn_starts)))

print("[*] Building GOT base map (slow, scanning prologues)...")
fn_got_map = {}
for fs in fn_starts:
    got = get_got_base(fs)
    if got is not None:
        fn_got_map[fs] = got
print("[*] {} functions have PIC prologue with GOT base".format(len(fn_got_map)))

# Determine fn end (next fn start)
fn_starts_sorted = sorted(fn_got_map.keys())

decomp = DecompInterface()
decomp.openProgram(prog)
seen = set()

for needle in needles:
    print()
    print("#### needle '{}' ####".format(needle))
    sva = find_string(needle)
    if sva is None:
        print("   not found in binary")
        continue
    print("   string@0x{:x}".format(sva))
    
    # For each function with known GOT base, check if it references this string
    matches = []
    for j, fs in enumerate(fn_starts_sorted):
        end = fn_starts_sorted[j+1] if j+1 < len(fn_starts_sorted) else len(text)
        got = fn_got_map[fs]
        refs = find_string_refs_in_fn(fs, end, sva, got)
        if refs:
            matches.append((fs, refs[0]))
    
    print("   referenced by {} function(s)".format(len(matches)))
    for fs, ref_offset in matches[:5]:
        fn_va = text_start + fs
        addr = ds.getAddress(fn_va)
        f = fm.getFunctionAt(addr)
        if f is None:
            # Create function
            from ghidra.app.cmd.function import CreateFunctionCmd
            CreateFunctionCmd(addr).applyTo(prog, mon)
            f = fm.getFunctionAt(addr)
        fn_name = f.getName() if f else "FUN_{:x}".format(fn_va)
        rva = (fn_va - base) & 0xFFFFFFFF
        print()
        print("   ================= FUNCTION {} entry=0x{:x} RVA=0x{:x} ref@0x{:x} =================".format(
            fn_name, fn_va, rva, text_start + ref_offset))
        # Skip if seen
        if fn_va in seen: continue
        seen.add(fn_va)
        # Decompile
        if f:
            try:
                res = decomp.decompileFunction(f, 60, mon)
                if res and res.getDecompiledFunction():
                    code = res.getDecompiledFunction().getC()
                    # Limit to 80 lines
                    print(code[:5000])
            except Exception as e:
                print("   decomp err: {}".format(e))

print()
print("#### DONE ####")
