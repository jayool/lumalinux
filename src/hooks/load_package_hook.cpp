#include "load_package_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using LoadPackageFn = bool (*)(void* pInfo, uint8_t* sha1, int32_t cn, void* p4);
LoadPackageFn g_origFn = nullptr;

// This hook NO LONGER injects. The package-0 finder is the sole injector: it
// covers both the fresh-load case this hook would catch AND the regression case
// where Steam keeps PackageId=0 cached and never calls LoadPackage (which this
// hook misses entirely). Keeping the hook out of the inject path means only the
// finder's single thread ever mutates AppIdVec, so there's no cross-thread race
// on the vector and no lock is needed. The hook stays installed purely for the
// LUMA_LOADPKG_DEBUG diagnostic below.
bool HookFn(void* pInfo, uint8_t* sha1, int32_t cn, void* p4) {
    bool result = g_origFn ? g_origFn(pInfo, sha1, cn, p4) : false;
    if (!pInfo) return result;

    uint32_t packageId = Hooks::LoadPackage::PkgId(pInfo);

    // Discovery diagnostic (opt-in, LUMA_LOADPKG_DEBUG=1). Logs EVERY call's
    // PackageId + AppIdVec triple for the first kDiagCap calls, regardless of
    // PackageId value. This is what tells us, in one session, whether the
    // hooked function is the real LoadPackage: a real LoadPackage is called
    // once per package during PICS parsing and MUST be seen with PackageId==0
    // (the free-apps package) at least once. LoadPackage is not called in a
    // tight loop, so logging every call is safe (unlike the v0.10.6 probes,
    // which hooked tight-loop functions and saturated log I/O).
    static const bool kDiag = []{
        const char* e = std::getenv("LUMA_LOADPKG_DEBUG");
        return e && e[0] && e[0] != '0';
    }();
    if (kDiag) {
        static std::atomic<int> diagCount{0};
        constexpr int kDiagCap = 400;
        int n = diagCount.fetch_add(1, std::memory_order_relaxed);
        if (n < kDiagCap) {
            auto* dv = Hooks::LoadPackage::AppIdVec(pInfo);
            Log::Info("LoadPackage[DIAG #%d]: PackageId=%u AppIdVec{mem=%p size=%u alloc=%d}",
                      n, packageId, (void*)dv->m_pMemory, dv->m_Size, dv->m_nAllocationCount);
        }
    }

    if (packageId != 0) {
        return result;
    }

    // PackageId=0 seen on a real LoadPackage call. We deliberately do NOT inject
    // here — the package-0 finder owns injection now (see the note at the top of
    // this file). Just record the sighting; the finder seeds the depots from its
    // own thread, covering both this case and the cached-package case the hook
    // can't see.
    Log::Debug("LoadPackage[hook]: PackageId=0 seen — injection handled by the "
               "package-0 finder, not here");
    return result;
}

} // namespace

namespace Hooks::LoadPackage {

bool InjectDepots(void* pInfo, const char* source) {
    if (!pInfo) return false;

    auto depotIds = KeyStore::GetAllDepotIds();
    if (depotIds.empty()) return false;

    // Drop presence-only (keyless) entries — the app-id / shader pre-cache
    // depot. Surfacing it into package-0 makes Steam treat the shader depot as
    // licensed and run the shader pre-cache, which can never succeed (we have
    // no key for it): it loops with "Missing decryption key", and faking a key
    // only makes Steam fail to decrypt the shader manifest ("Invalid content
    // configuration") and suspend the install. By keeping it OUT of package-0,
    // Steam sees the shader depot as unlicensed and skips the pre-cache. The
    // CONTENT depots (real keys) stay injected, so installs are unaffected.
    // See RESEARCH §13.8.
    {
        const size_t before = depotIds.size();
        depotIds.erase(std::remove_if(depotIds.begin(), depotIds.end(),
                           [](uint32_t d) { return KeyStore::IsPresenceOnly(d); }),
                       depotIds.end());
        const size_t dropped = before - depotIds.size();
        if (dropped) {
            Log::Info("LoadPackage[%s]: excluded %zu presence-only (keyless "
                      "shader/app-id) depot(s) from package-0 — skips the broken "
                      "shader pre-cache", source, dropped);
        }
        if (depotIds.empty()) return false;
    }

    CUtlVector<uint32_t>* vec = AppIdVec(pInfo);
    uint32_t oldSize = vec->m_Size;
    if (oldSize > 4096) {
        Log::Warn("LoadPackage[%s]: PackageId=0 AppIdVec->m_Size=%u looks "
                  "implausible (offset wrong?) — skipping injection",
                  source, oldSize);
        return false;
    }

    Log::Debug("LoadPackage[%s]: PackageId=0 hit — vec mem=%p size=%u alloc=%d grow=%d",
               source, (void*)vec->m_pMemory, vec->m_Size,
               vec->m_nAllocationCount, vec->m_nGrowSize);

    // Sanity: verify the offset we deduced for AppIdVec (+0x38) is sane.
    // The free package on real accounts contains real AppIds — small positive
    // integers, typically all distinct. If we're reading garbage we'd see
    // values like 0, 0xFFFFFFFF, repeating bytes, or huge clusters. Sample
    // the first 8 entries and abort if any look bogus.
    if (vec->m_pMemory && oldSize > 0) {
        uint32_t sampleN = oldSize < 8 ? oldSize : 8;
        for (uint32_t i = 0; i < sampleN; ++i) {
            uint32_t a = vec->m_pMemory[i];
            if (a == 0 || a > 50'000'000) {
                Log::Warn("LoadPackage[%s]: SANITY FAIL — AppIdVec[%u]=%u looks bogus "
                          "(offset 0x38 may be wrong on this build). Skipping injection.",
                          source, i, a);
                return false;
            }
        }
        Log::Debug("LoadPackage[%s]: sanity OK — vec[0..%u]={%u,%u,%u,%u,...}",
                   source, sampleN,
                   vec->m_pMemory[0],
                   sampleN > 1 ? vec->m_pMemory[1] : 0,
                   sampleN > 2 ? vec->m_pMemory[2] : 0,
                   sampleN > 3 ? vec->m_pMemory[3] : 0);
    }

    // Content-based idempotency: filter out depot ids already present. This
    // makes the function safe to call multiple times (e.g. both from the
    // LoadPackage hook and from the package-0 finder) without doubling
    // entries. No external flag needed — the state of the vector itself is
    // the source of truth.
    std::vector<uint32_t> toAdd;
    toAdd.reserve(depotIds.size());
    for (uint32_t depotId : depotIds) {
        bool already = false;
        for (uint32_t i = 0; i < oldSize; ++i) {
            if (vec->m_pMemory && vec->m_pMemory[i] == depotId) { already = true; break; }
        }
        if (!already) toAdd.push_back(depotId);
    }
    if (toAdd.empty()) {
        Log::Debug("LoadPackage[%s]: all %zu depot ids already in PackageId=0",
                   source, depotIds.size());
        return false;
    }

    uint32_t total = oldSize + static_cast<uint32_t>(toAdd.size());

    // Grow if needed. Steam Linux i386 uses raw libc realloc for CUtlMemory
    // (verified by disassembling the only 5 callers of realloc@plt in
    // steamclient.so — the leaf helper at +0x5c79d30 is `realloc(ptr, count *
    // elem_size)` with no tier0 allocator wrapper, confirming that m_pMemory
    // was malloc-allocated and is safe to realloc from outside. This is the
    // Linux equivalent of LumaCore's oCUtlMemoryGrow on Windows.
    //
    // Growth policy mirrors Source SDK CUtlMemory::Grow: double capacity if
    // possible, else snap to the requested size; minimum first allocation is
    // 32 entries to avoid a ladder of small reallocs.
    if (!vec->m_pMemory || vec->m_nAllocationCount < static_cast<int>(total)) {
        int new_alloc;
        if (vec->m_nAllocationCount <= 0) {
            new_alloc = static_cast<int>(total) > 32 ? static_cast<int>(total) : 32;
        } else {
            int doubled = vec->m_nAllocationCount * 2;
            new_alloc = doubled > static_cast<int>(total) ? doubled : static_cast<int>(total);
        }
        void* new_mem = std::realloc(vec->m_pMemory,
                                      static_cast<size_t>(new_alloc) * sizeof(uint32_t));
        if (!new_mem) {
            Log::Warn("LoadPackage[%s]: realloc failed for %d entries — skipping injection",
                      source, new_alloc);
            return false;
        }
        Log::Info("LoadPackage[%s]: grew AppIdVec capacity %d -> %d via realloc "
                  "(m_pMemory %p -> %p)",
                  source, vec->m_nAllocationCount, new_alloc,
                  (void*)vec->m_pMemory, new_mem);
        vec->m_pMemory = static_cast<uint32_t*>(new_mem);
        vec->m_nAllocationCount = new_alloc;
    }

    // Unreachable safeguard — kept so static analysis sees a path that returns.
    if (!vec->m_pMemory) {
        Log::Error("LoadPackage[%s]: vec->m_pMemory is null after grow attempt",
                   source);
        return false;
    }

    for (size_t i = 0; i < toAdd.size(); ++i) {
        vec->m_pMemory[oldSize + i] = toAdd[i];
    }
    vec->m_Size = total;
    Log::Info("LoadPackage[%s]: APPENDED %zu depot id(s) to PackageId=0 "
              "(size %u -> %u, capacity now %d)",
              source, toAdd.size(), oldSize, total, vec->m_nAllocationCount);
    for (uint32_t a : toAdd) {
        Log::Info("LoadPackage[%s]:   + depot %u", source, a);
    }
    return true;
}

bool Install() {
    uintptr_t target = Patterns::FindLoadPackageFunction();
    if (!target) {
        Log::Error("LoadPackage hook: cannot install — target not found");
        Log::Warn("Hook install: name=LoadPackage method=pattern outcome=pattern_miss");
        return false;
    }

    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("LoadPackage hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        Log::Warn("Hook install: name=LoadPackage method=pattern target=0x%lx outcome=hook_install_failed",
                  (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<LoadPackageFn>(tramp);

    Log::Info("LoadPackage hook: INSTALLED (target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    uintptr_t base = Patterns::FindSteamclientBase();
    Log::Info("Hook install: name=LoadPackage method=pattern target=0x%lx rva=0x%lx outcome=installed",
              (unsigned long)target, (unsigned long)(base ? target - base : 0));
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::LoadPackage
