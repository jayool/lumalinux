# -*- coding: utf-8 -*-
# Ghidra headless postScript: locate the UNTESTED content-authorization RPCs and
# dump their decompiled signatures, so a hook probe can call them SAFELY.
#
# Why this exists (2026-09-09): Valve added a server-side ownership check to
# GetManifestRequestCode - measured, our fully-spoofed session is denied for
# paid unowned content, and the sole provider backend (wudrm/opensteamtool) is
# a relay whose upstream now denies it too. Reading the current steamclient
# protobufs (OpenSteam001/steam-monitor, build 1788652215) shows the
# ContentServerDirectory service has OTHER content-auth RPCs we never touched:
#
#   GetCDNAuthToken{depot_id,host_name,app_id} -> {token,expiration_time}
#       The legacy per-depot CDN auth token. SteamKit's changelog says it is now
#       only for "country specific servers that still require it" - so the main
#       CDN likely wants the request code - BUT it is a SEPARATE RPC and may be
#       gated differently (or not at all) on ownership. Untested.
#   RequestPeerContentServer / GetPeerContentInfo
#       Steam's own peer-to-peer content path (download from another owning PC).
#       A different transport, native to Steam. Untested.
#
# We CANNOT hook + call these blind: the function's C++ signature (arg count,
# order, calling convention, the out-struct shape) is not in the .proto. Calling
# with a wrong signature corrupts the stack and crashes Steam. So the required
# FIRST step is decompiling each function to read its real signature - exactly
# what this script prints. Only then is the probe hook (mirroring gmrc_hook)
# safe to write.
#
# Anchor is the RPC job-name string, the same reliable anchor gmrc_xref uses.
#
# Usage (on a box with the current steamclient.so):
#   run_ghidra_derive-style headless invocation, e.g.
#   analyzeHeadless <proj> tmp -import steamclient.so -postScript ghidra_find_cdnauth.py
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

prog   = currentProgram
fm     = prog.getFunctionManager()
mem    = prog.getMemory()
refmgr = prog.getReferenceManager()
mon    = ConsoleTaskMonitor()
base   = prog.getImageBase().getOffset()

# The job-name strings dispatched over the CM connection (see the decompile of
# GMRC in RESEARCH: `(**(vtable+0x10))(this, "<job name>", &req, &resp, 0)`).
# The function that references each string is the BYielding* getter we'd hook.
needles = [
    "ContentServerDirectory.GetCDNAuthToken#1",          # legacy CDN auth token
    "ContentServerDirectory.RequestPeerContentServer#1", # peer content transport
    "ContentServerDirectory.GetPeerContentInfo#1",       # peer content discovery
    "ContentServerDirectory.GetManifestRequestCode#1",   # known one, for signature comparison
]

def find_all(s):
    out = []
    b = bytearray(s, "ascii")
    start = prog.getMinAddress()
    while True:
        a = mem.findBytes(start, bytes(b), None, True, mon)
        if a is None:
            break
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
            if key in seen:
                continue
            seen.add(key)
            print("\n================= FUNCTION %s  entry=%s  RVA=0x%x  (ref@%s) ================="
                  % (f.getName(), ep, key - base, r.getFromAddress()))
            # Signature line first - this is the whole point of the script.
            try:
                proto = f.getSignature().getPrototypeString()
                print("SIGNATURE: %s" % proto)
                print("PARAM COUNT: %d  CALLING CONV: %s"
                      % (f.getParameterCount(), f.getCallingConventionName()))
            except Exception as e:
                print("[signature err: %s]" % e)
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

print("\n#### ghidra_find_cdnauth.py DONE ####")
