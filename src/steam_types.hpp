#pragma once

// Shared Steam-internal type layouts (Source SDK / steamclient.so, Linux i386).
//
// Single source of truth so individual hooks don't keep private, drifting
// copies of the same struct. When Valve changes a layout, or a defensive tweak
// is needed (volatile, bounds check, a new field), it's edited here once. The
// static_asserts pin the sizes — the build fails if a layout assumption breaks.
//
// See issue #10.

#include <cstdint>

// CUtlVector<T> — Valve's home-grown vector (Source SDK). On i386 the header is
// 16 bytes. We only ever read/patch these header fields; the element storage
// (m_pMemory) is Steam's own allocation.
template<typename T>
struct CUtlVector {
    T*       m_pMemory;
    int      m_nAllocationCount;
    int      m_nGrowSize;
    uint32_t m_Size;
};
static_assert(sizeof(CUtlVector<uint32_t>) == 16, "CUtlVector header must be 16 bytes");

// DepotEntry — one depot inside a BuildDepotDependency CUtlVector. 32 bytes on
// i386 (the BuildDep hook patches ManifestGid/ManifestSize in place).
struct DepotEntry {
    uint32_t DepotId;
    uint32_t AppId;
    uint64_t ManifestGid;
    uint64_t ManifestSize;
    uint32_t DlcAppId;
    uint8_t  LcsRequired;
    uint8_t  bNotNewTarget;
    uint8_t  SharedInstall;
    uint8_t  _pad;
};
static_assert(sizeof(DepotEntry) == 0x20, "DepotEntry must be 32 bytes");
static_assert(sizeof(CUtlVector<DepotEntry>) == 16, "CUtlVector<DepotEntry> header must be 16 bytes");
