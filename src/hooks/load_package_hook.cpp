#include "load_package_hook.hpp"
#include "../patterns.hpp"
#include "../key_store.hpp"
#include "../lmhook.hpp"
#include "../log.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// CUtlVector<T> header (Source SDK, 16 bytes on i386):
//   T* m_pMemory;            int m_nAllocationCount;
//   int m_nGrowSize;         uint32_t m_Size;
template<typename T>
struct CUtlVector {
    T*       m_pMemory;
    int      m_nAllocationCount;
    int      m_nGrowSize;
    uint32_t m_Size;
};
static_assert(sizeof(CUtlVector<uint32_t>) == 16, "CUtlVector header must be 16 bytes");

// PackageInfo (Linux i386, derived from LumaCore's Structs.h with i386 sizes):
//   +0x00  uint32_t  PackageId
//   +0x04  int32_t   ChangeNumber
//   +0x08  uint64_t  PICS_token        (4-byte aligned on i386 SysV)
//   +0x10  int32_t   BillingType       (enum class : int)
//   +0x14  int32_t   LicenseType
//   +0x18  int32_t   Status
//   +0x1C  uint8_t   SHA_1_Hash[20]
//   +0x30  void*     pPackageInfoNodeBegin
//   +0x34  void*     pExtendNodeBegin
//   +0x38  CUtlVector<uint32_t>  AppIdVec     (16 bytes)
//   +0x48  CUtlVector<uint32_t>  DepotIdVec   (16 bytes)
constexpr size_t kOffPackageId = 0x00;
constexpr size_t kOffAppIdVec  = 0x38;

using LoadPackageFn = bool (*)(void* pInfo, uint8_t* sha1, int32_t cn, void* p4);
LoadPackageFn g_origFn = nullptr;

inline uint32_t PkgId(void* pInfo) {
    return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pInfo) + kOffPackageId);
}
inline CUtlVector<uint32_t>* AppIdVec(void* pInfo) {
    return reinterpret_cast<CUtlVector<uint32_t>*>(
        static_cast<uint8_t*>(pInfo) + kOffAppIdVec);
}

bool HookFn(void* pInfo, uint8_t* sha1, int32_t cn, void* p4) {
    bool result = g_origFn ? g_origFn(pInfo, sha1, cn, p4) : false;
    if (!pInfo) return result;

    uint32_t packageId = PkgId(pInfo);
    if (packageId != 0) {
        return result;
    }

    // LumaCore injects GetAllDepotIds() (the DEPOT ids) here, NOT the parent
    // app id. Steam's per-depot license filter in BuildDepotDependency checks
    // each depot id against PackageId=0; listing the depot ids here is what
    // keeps them in pDepotInfo instead of being dropped.
    auto depotIds = KeyStore::GetAllDepotIds();
    if (depotIds.empty()) return result;

    CUtlVector<uint32_t>* vec = AppIdVec(pInfo);
    uint32_t oldSize = vec->m_Size;
    if (oldSize > 4096) {
        Log::Warn("LoadPackage: PackageId=0 AppIdVec->m_Size=%u looks implausible "
                  "(offset wrong?) — skipping injection", oldSize);
        return result;
    }

    Log::Debug("LoadPackage: PackageId=0 hit — vec mem=%p size=%u alloc=%d grow=%d",
               (void*)vec->m_pMemory, vec->m_Size, vec->m_nAllocationCount,
               vec->m_nGrowSize);

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
                Log::Warn("LoadPackage: SANITY FAIL — AppIdVec[%u]=%u looks bogus "
                          "(offset 0x38 may be wrong on this build). Skipping injection.",
                          i, a);
                return result;
            }
        }
        Log::Debug("LoadPackage: sanity OK — vec[0..%u]={%u,%u,%u,%u,...}",
                   sampleN,
                   vec->m_pMemory[0],
                   sampleN > 1 ? vec->m_pMemory[1] : 0,
                   sampleN > 2 ? vec->m_pMemory[2] : 0,
                   sampleN > 3 ? vec->m_pMemory[3] : 0);
    }

    // Filter out depot ids already present.
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
        Log::Debug("LoadPackage: all %zu depot ids already in PackageId=0",
                   depotIds.size());
        return result;
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
            Log::Warn("LoadPackage: realloc failed for %d entries — skipping injection",
                      new_alloc);
            return result;
        }
        Log::Info("LoadPackage: grew AppIdVec capacity %d -> %d via realloc "
                  "(m_pMemory %p -> %p)",
                  vec->m_nAllocationCount, new_alloc, (void*)vec->m_pMemory, new_mem);
        vec->m_pMemory = static_cast<uint32_t*>(new_mem);
        vec->m_nAllocationCount = new_alloc;
    }

    // Unreachable safeguard — kept so static analysis sees a path that returns.
    if (!vec->m_pMemory) {
        Log::Error("LoadPackage: vec->m_pMemory is null after grow attempt");
        return result;
    }

    for (size_t i = 0; i < toAdd.size(); ++i) {
        vec->m_pMemory[oldSize + i] = toAdd[i];
    }
    vec->m_Size = total;
    Log::Info("LoadPackage: APPENDED %zu depot id(s) to PackageId=0 "
              "(size %u -> %u, capacity now %d)",
              toAdd.size(), oldSize, total, vec->m_nAllocationCount);
    for (uint32_t a : toAdd) {
        Log::Info("LoadPackage:   + depot %u", a);
    }
    return result;
}

} // namespace

namespace Hooks::LoadPackage {

bool Install() {
    uintptr_t target = Patterns::FindLoadPackageFunction();
    if (!target) {
        Log::Error("LoadPackage hook: cannot install — target not found");
        return false;
    }

    void* tramp = nullptr;
    if (!LmHook::Install(target, reinterpret_cast<void*>(&HookFn), &tramp)) {
        Log::Error("LoadPackage hook: LmHook::Install failed (target=0x%lx)",
                   (unsigned long)target);
        return false;
    }
    g_origFn = reinterpret_cast<LoadPackageFn>(tramp);

    Log::Info("LoadPackage hook: INSTALLED (target=0x%lx, trampoline=%p)",
              (unsigned long)target, (void*)g_origFn);
    return true;
}

void Uninstall() {
    g_origFn = nullptr;
}

} // namespace Hooks::LoadPackage
