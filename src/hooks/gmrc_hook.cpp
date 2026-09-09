#include "gmrc_hook.hpp"
#include "../gmrc_xref.hpp"
#include "../patterns.hpp"
#include "../rva_feed.hpp"
#include "../key_store.hpp"
#include "../gmrc_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <cstdint>

namespace {

// int32 GetManifestRequestCode(void* this_, uint32 app_id, uint32 depot_id,
//                              uint32 manifest_lo, uint32 manifest_hi,
//                              char* branch, uint64* out_code)
// On success: writes the request code to *out_code and returns 1.
// For unowned content the Steam server denies it -> returns 0 (failure).
using GmrcFn = int32_t (*)(void*, uint32_t, uint32_t, uint32_t, uint32_t,
                           char*, uint64_t*);
GmrcFn g_origFn = nullptr;

int32_t HookFn(void* this_, uint32_t app_id, uint32_t depot_id,
               uint32_t manifest_lo, uint32_t manifest_hi,
               char* branch, uint64_t* out_code) {
    uint64_t gid = (uint64_t)manifest_lo | ((uint64_t)manifest_hi << 32);

    Log::Debug("GMRC: GetManifestRequestCode app=%u depot=%u manifest=%llu out=%p",
               app_id, depot_id, (unsigned long long)gid, (void*)out_code);

    // Intervene for manifests of our forced depots. Two ways a depot qualifies
    // as "ours":
    //   (a) its manifest_gid is pre-registered in keys.txt (Extended format) —
    //       the common case for content depots seeded by steamidra_lite.
    //   (b) its depot_id is in the KeyStore but the specific manifest_gid was
    //       not pre-registered. Happens for the app/shader depot when Steam
    //       requests its own manifest (e.g. shader pre-cache: depot==appid)
    //       using a manifest_gid that came from PICS appinfo, not from the
    //       Hubcap .lua. Without (b) the hook falls through and the CDN
    //       denies the download with Access Denied, which Steam surfaces as
    //       "No connection" in the UI.
    // Everything else still goes to Steam's normal (owned) path.
    // EXPERIMENT (2026-09-09), LUMA_GMRC_PASSTHROUGH=1: do not inject for our
    // depots either — let Steam ask Valve itself and log what comes back.
    //
    // This path has never been observed. The hook has short-circuited our depots
    // since it was written, so nobody knows what Valve's CM answers when the
    // asking client has SLSsteam's ownership spoof and the package-0 injection in
    // place. If it answers with a usable code, the provider cascade is not needed
    // at all; if it denies, that is the measurement that closes the question.
    // =====================================================================
    // GMRC_PROBE (2026-09-09) — map WHAT Valve checks before issuing a code.
    // =====================================================================
    // Armed by ~/.config/lumalinux/gmrc_probe. Runs ONCE per process, on the
    // first call that concerns one of our depots, then gets out of the way.
    //
    // Why a probe and not reasoning: what reaches Valve is the RPC
    // "ContentServerDirectory.GetManifestRequestCode#1" over the CM connection
    // (RESEARCH §GMRC), whose protobuf carries exactly four things we control —
    // app_id, depot_id, manifest_id, branch. The verdict is that RPC's own
    // return value: 0 = Access Denied, 1 = issued (code at response+0x10). So
    // the entire policy surface is those four fields, and the only way to learn
    // the rule is to vary them and read Valve's answer.
    //
    // Measured before this existed, which is why the obvious rules are already
    // ruled out:
    //   Proton / redistributables (real licence)  -> issued
    //   Brotato shader depot (depot == appid)     -> issued, and unowned
    //   Brotato content depot                     -> Access Denied
    //   Abyssal Terrors DLC (depot == appid too)  -> Access Denied
    // So neither "depot == appid" nor "the account owns the app" is the rule.
    //
    // Optional reference values for the cases that need an app the account
    // really owns, read from the marker file:
    //   owned_app=262060
    //   owned_depot=262065
    // Leave them out and those cases are skipped.
    //
    // Every case is a real RPC to Valve. The matrix is small and runs once.
    static std::atomic<bool> g_probeDone{false};
    if (out_code && g_origFn && !g_probeDone.load(std::memory_order_relaxed)
        && (KeyStore::HasManifestGid(gid) || KeyStore::HasDepot(depot_id))) {
        const char* home = std::getenv("HOME");
        std::string mk = home ? std::string(home) + "/.config/lumalinux/gmrc_probe"
                              : std::string();
        std::ifstream mf(mk);
        if (mf.good() && !g_probeDone.exchange(true)) {
            uint32_t ownedApp = 0, ownedDepot = 0;
            for (std::string line; std::getline(mf, line); ) {
                if (line.rfind("owned_app=", 0) == 0)
                    ownedApp = (uint32_t)std::strtoul(line.c_str() + 10, nullptr, 10);
                else if (line.rfind("owned_depot=", 0) == 0)
                    ownedDepot = (uint32_t)std::strtoul(line.c_str() + 12, nullptr, 10);
            }
            Log::Info("GMRC_PROBE: start — real call app=%u depot=%u manifest=%llu "
                      "branch='%s' (owned_app=%u owned_depot=%u)",
                      app_id, depot_id, (unsigned long long)gid,
                      branch ? branch : "(null)", ownedApp, ownedDepot);

            char brPublic[] = "public";
            char brEmpty[]  = "";
            char brBeta[]   = "beta";

            struct Case { const char* name; uint32_t a; uint32_t d; bool needsOwned; char* b; };
            const Case cases[] = {
                { "baseline",             app_id,   depot_id,   false, branch   },
                { "branch=public",        app_id,   depot_id,   false, brPublic },
                { "branch=empty",         app_id,   depot_id,   false, brEmpty  },
                { "branch=beta",          app_id,   depot_id,   false, brBeta   },
                { "app=0",                0,        depot_id,   false, branch   },
                { "depot=0",              app_id,   0,          false, branch   },
                { "depot=app",            app_id,   app_id,     false, branch   },
                { "app=depot",            depot_id, depot_id,   false, branch   },
                { "ownedapp+ourdepot",    ownedApp, depot_id,   true,  branch   },
                { "ownedapp+owneddepot",  ownedApp, ownedDepot, true,  branch   },
            };
            for (const auto& c : cases) {
                if (c.needsOwned && (ownedApp == 0 || ownedDepot == 0)) {
                    Log::Info("GMRC_PROBE: %-22s SKIPPED (no owned_app/owned_depot "
                              "in the marker file)", c.name);
                    continue;
                }
                uint64_t probeOut = 0;
                const int32_t prc = g_origFn(this_, c.a, c.d, manifest_lo, manifest_hi,
                                             c.b, &probeOut);
                Log::Info("GMRC_PROBE: %-22s app=%-9u depot=%-9u branch='%s' -> rc=%d "
                          "code=%llu %s",
                          c.name, c.a, c.d, c.b ? c.b : "(null)", (int)prc,
                          (unsigned long long)probeOut,
                          (prc && probeOut) ? "<<<<< ISSUED" : "(denied)");
            }
            Log::Info("GMRC_PROBE: end — manifest under test was %llu",
                      (unsigned long long)gid);
        }
    }

    // Two ways to arm it, because ONE OF THEM DOES NOT SURVIVE THE STEAM RUNTIME:
    // steam-runtime-tool re-execs the client through a filtered environment and
    // drops variables outside its allow-list, so LUMA_* set in the wrapper never
    // reaches the process that loads us (measured 2026-09-09). The marker FILE is
    // the reliable one; the env var is kept for launches that bypass the runtime.
    static const bool kPassthrough = [] {
        const char* e = std::getenv("LUMA_GMRC_PASSTHROUGH");
        if (e && *e && e[0] != '0') return true;
        const char* home = std::getenv("HOME");
        if (!home || !*home) return false;
        std::string marker = std::string(home) + "/.config/lumalinux/gmrc_passthrough";
        return std::ifstream(marker).good();
    }();

    if (kPassthrough) {
        Log::Info("GMRC: LUMA_GMRC_PASSTHROUGH=1 — NOT injecting for app=%u depot=%u "
                  "manifest=%llu; letting Steam ask Valve", app_id, depot_id,
                  (unsigned long long)gid);
    } else if (out_code && (KeyStore::HasManifestGid(gid) || KeyStore::HasDepot(depot_id))) {
        auto code = Gmrc::GetCode(gid);
        if (code) {
            *out_code = *code;
            Log::Info("GMRC: INJECTED request code %llu for manifest %llu (depot %u)",
                      (unsigned long long)*code, (unsigned long long)gid, depot_id);
            return 1;  // success — Steam proceeds to download the manifest
        }
        Log::Warn("GMRC: no code available for manifest %llu — falling through "
                  "(download will fail with Access Denied)", (unsigned long long)gid);
    }

    if (!g_origFn) return 0;
    const int32_t rc = g_origFn(this_, app_id, depot_id, manifest_lo, manifest_hi,
                                branch, out_code);

    // DIAGNOSTIC (2026-09-09). Logs the code VALVE's own path returns, for the
    // content we deliberately do not touch — i.e. games the account owns.
    //
    // Why it is here: on 2026-09-09 Valve began returning 401 for every manifest
    // fetched with a provider-minted code, and the ecosystem produced three
    // incompatible explanations (ownership is now required / the (app, depot,
    // manifest) triple is now validated and providers send a filler depot /
    // provider-side maintenance). All three yield the same 401, so the 401 alone
    // cannot separate them.
    //
    // This line can. Ask a provider for a code for a manifest of a game the
    // account OWNS, and compare it against what Valve hands Steam for the exact
    // same manifest, within the same rotation window:
    //   - codes IDENTICAL -> the provider mints correctly; the rejection is about
    //     who is asking, not how.
    //   - codes DIFFERENT -> the provider is minting for a different tuple, which
    //     is a bug with a fix on our side: this hook already knows app_id and
    //     depot_id, and Gmrc::GetCode() is currently given only the gid.
    //
    // Debug severity, so it costs nothing at the default level. Remove it once
    // the question is settled.
    Log::Debug("GMRC: passthrough app=%u depot=%u manifest=%llu -> rc=%d code=%llu",
               app_id, depot_id, (unsigned long long)gid, (int)rc,
               (unsigned long long)(out_code ? *out_code : 0));
    return rc;
}

} // namespace

namespace Hooks::Gmrc {

bool Install() {
    // Resolver order, same shape as depot_key_hook.cpp: each step runs ONLY if
    // the previous came up empty, because a resolver that cannot change the
    // outcome must not run while Steam is starting. Cross-checking the chosen
    // address belongs in CI (check_patterns.py derives the xref nightly and
    // blocks on a disagreement), which has the binary open and can open a PR.
    const char* method = "none";
    uintptr_t target = 0;

    if (uintptr_t feed = RvaFeed::Resolve("GMRC")) {
        target = feed; method = "rva";
    }

    if (!target) {
        if (uintptr_t pat = Patterns::FindGmrcFunction()) {
            target = pat; method = "pattern";
        }
    }

    // Last resort: the job-name string xref. Survives a rebuild that reshuffles
    // the prologue — the anchor is a string, and since 2026-09-07 the entry comes
    // from .eh_frame_hdr's function table rather than a backward scan.
    if (!target) {
        if (uintptr_t xref = GmrcXref::FindGmrcFunction()) {
            target = xref; method = "xref(rescue)";
            Log::Warn("GMRC: feed and pattern both MISSED or AMBIGUOUS; job-name "
                      "xref resolved 0x%lx — Steam likely reshuffled the prologue",
                      (unsigned long)xref);
        }
    }

    if (!target) {
        Log::Error("GMRC hook: target not found (feed, pattern and job-name xref "
                   "all failed)");
        Log::Warn("Hook install: name=GMRC method=none outcome=miss");
        return false;
    }
    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("GMRC hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        Log::Warn("Hook install: name=GMRC method=%s target=0x%lx outcome=hook_install_failed",
                  method, (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<GmrcFn>(tramp);
    Log::Info("GMRC hook: INSTALLED (target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    uintptr_t base = Patterns::FindSteamclientBase();
    Log::Info("Hook install: name=GMRC method=%s target=0x%lx rva=0x%lx outcome=installed",
              method, (unsigned long)target, (unsigned long)(base ? target - base : 0));
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::Gmrc
