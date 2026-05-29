# Ghidra headless postScript: find functions referencing key GMRC strings and
# decompile them. Output to stdout (analyzeHeadless console).
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

prog   = currentProgram
fm     = prog.getFunctionManager()
mem    = prog.getMemory()
refmgr = prog.getReferenceManager()
mon    = ConsoleTaskMonitor()
base   = prog.getImageBase().getOffset()

needles = [
    "CDepotDownloadMgr::BYldRequestDepotManifest(App",   # the GMRC consumer (fails Access Denied)
    "ContentServerDirectory.GetManifestRequestCode#1",   # job name
    "bClientTryRequestManifestWithoutCode",              # the convar (its read = the gate)
]

def find_all(s):
    out = []
    b = bytearray(s, "ascii")
    start = prog.getMinAddress()
    while True:
        a = mem.findBytes(start, bytes(b), None, True, mon)
        if a is None: break
        out.append(a); start = a.add(1)
    return out

decomp = DecompInterface(); decomp.openProgram(prog)
seen = set()

for needle in needles:
    addrs = find_all(needle)
    print("\n#### needle '%s' -> %d hit(s) ####" % (needle, len(addrs)))
    for sa in addrs:
        refs = refmgr.getReferencesTo(sa)
        cnt = 0
        for r in refs:
            cnt += 1
            f = fm.getFunctionContaining(r.getFromAddress())
            if f is None:
                print("   ref @%s (no fcn)" % r.getFromAddress()); continue
            ep = f.getEntryPoint(); key = ep.getOffset()
            if key in seen: continue
            seen.add(key)
            print("\n================= FUNCTION %s  entry=%s  RVA=0x%x  (ref@%s) ================="
                  % (f.getName(), ep, key-base, r.getFromAddress()))
            try:
                res = decomp.decompileFunction(f, 200, mon)
                if res and res.getDecompiledFunction():
                    print(res.getDecompiledFunction().getC())
                else:
                    print("[decompile empty]")
            except Exception as e:
                print("[decompile err: %s]" % e)
        if cnt == 0:
            print("   string@%s : 0 refs" % sa)

print("\n#### ghidra_find.py DONE ####")
