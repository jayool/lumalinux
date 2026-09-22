#include "reconcile_anchor.hpp"

#include "eh_frame.hpp"
#include "gmrc_xref.hpp"            // SteamclientExecRegion
#include "log.hpp"
#include "reconcile_anchor_core.hpp"

#include <cstdint>

namespace ReconcileAnchor {

uintptr_t FindNotifyLicensesUpdated() {
    using GmrcXrefCore::Region;
    const Region rx = GmrcXref::SteamclientExecRegion();
    if (!rx) {
        Log::Debug("Reconcile anchor: steamclient.so r-x mapping not found");
        return 0;
    }

    // Function boundaries from .eh_frame_hdr, exact. A site the table cannot
    // place resolves to 0 and drops out of the candidate set (fail-closed).
    const uintptr_t execEnd = rx.addr + rx.size;
    bool tableOk = true;
    auto fnOf = [&](uintptr_t site, uintptr_t& s, uintptr_t& e) -> bool {
        if (EhFrame::FindFunction(site, execEnd, s, e)) return true;
        tableOk = false;
        return false;
    };

    ReconcileAnchorCore::Info info;
    const uintptr_t entry = ReconcileAnchorCore::LocateNotifyLicensesUpdated(rx, fnOf, &info);

    if (info.sites == 0) {
        Log::Debug("Reconcile anchor: no `push 0x7d; push eax; call` site in steamclient.so");
        return 0;
    }
    if (info.candidates == 0) {
        Log::Warn("Reconcile anchor: %lu callback site(s) but .eh_frame_hdr placed none "
                  "of them in a function — refusing (no walk-back for this anchor)",
                  (unsigned long)info.sites);
        return 0;
    }
    if (!entry) {
        if (info.kept == 0)
            Log::Warn("Reconcile anchor: %lu site(s) in %lu function(s), none reads `this` "
                      "then bails — NOT_FOUND, refusing",
                      (unsigned long)info.sites, (unsigned long)info.candidates);
        else
            Log::Warn("Reconcile anchor: %lu function(s) qualify (0x%lx, 0x%lx, …) — "
                      "AMBIGUOUS, refusing",
                      (unsigned long)info.kept, (unsigned long)info.first,
                      (unsigned long)info.second);
        return 0;
    }
    Log::Info("Reconcile anchor: %lu callback-125 site(s) in %lu function(s)%s; this-read + "
              "jle keeps 1 -> 0x%lx (RVA 0x%lx, member disp 0x%x informational)",
              (unsigned long)info.sites, (unsigned long)info.candidates,
              tableOk ? "" : " (some sites outside the table)",
              (unsigned long)entry, (unsigned long)(entry - rx.addr), info.memberDisp);
    return entry;
}

} // namespace ReconcileAnchor
