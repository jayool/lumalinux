// SPDX-License-Identifier: GPL-3.0-only
// x64_rescues_selftest.cpp — offline validation of the x86-64 runtime rescues
// (src/gmrc_xref_core.hpp ScanRipLeaUnique, src/reconcile_anchor_core.hpp
// LocateNotifyLicensesUpdated64) against a real steamrt64/steamclient.so,
// WITHOUT loading Steam. mmaps the file, rebuilds the vaddr space from the
// ELF64 program headers, parses .eh_frame_hdr's function table from the file
// (the runtime parses the same table from the mapped module, src/eh_frame.cpp)
// and runs the SAME cores the runtime rescues run. The by-name DepotKey rescue
// is not covered: it walks vtables, which in the file are zeros until the
// loader applies .rela.dyn (tools/check_patterns_x64.py applies them and
// checks that one).
//
//   g++ -O2 -std=c++17 tools/x64_rescues_selftest.cpp -o /tmp/x64rs
//   /tmp/x64rs <steamclient.so> [GMRC=0x… ShaderDepot=0x… BuildDep=0x… Reconcile=0x…]
//
// Measured on 947e5a19… (2026-10-20): GMRC 0x13a4960, ShaderDepot 0x10a0d20,
// BuildDep 0x106f840, Reconcile 0x18d9e00 — the values check_patterns_x64.py
// derives. Exit 0 = every rescue resolved (and == expected where given);
// 1 = a miss or mismatch; 2 = usage/IO.
#include "../src/gmrc_xref_core.hpp"
#include "../src/reconcile_anchor_core.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using GmrcXrefCore::Region;

struct Load { uint64_t off, vaddr, filesz, memsz; uint32_t flags; };

static std::vector<Load> ParseLoads64(const uint8_t* f, size_t n, uint64_t* ehHdr) {
    std::vector<Load> out;
    if (n < 64 || std::memcmp(f, "\x7f" "ELF", 4) != 0 || f[4] != 2) return out;
    uint64_t phoff; uint16_t phentsize, phnum;
    std::memcpy(&phoff, f + 0x20, 8); std::memcpy(&phentsize, f + 0x36, 2); std::memcpy(&phnum, f + 0x38, 2);
    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t* p = f + phoff + (uint64_t)i * phentsize;
        if (p + 56 > f + n) break;
        uint32_t type, flags; uint64_t off, vaddr, filesz, memsz;
        std::memcpy(&type, p, 4); std::memcpy(&flags, p + 4, 4);
        std::memcpy(&off, p + 8, 8); std::memcpy(&vaddr, p + 16, 8);
        std::memcpy(&filesz, p + 32, 8); std::memcpy(&memsz, p + 40, 8);
        if (type == 1) out.push_back({off, vaddr, filesz, memsz, flags});
        if (type == 0x6474E550 && ehHdr) *ehHdr = vaddr;
    }
    return out;
}

// The file's bytes for a vaddr (file-backed part only).
static const uint8_t* AtVaddr(const uint8_t* f, const std::vector<Load>& L, uint64_t va, size_t need = 1) {
    for (const auto& l : L)
        if (va >= l.vaddr && va + need <= l.vaddr + l.filesz) return f + l.off + (va - l.vaddr);
    return nullptr;
}

static std::vector<uint64_t> EhStarts(const uint8_t* f, const std::vector<Load>& L, uint64_t hdrVa) {
    std::vector<uint64_t> starts;
    const uint8_t* h = AtVaddr(f, L, hdrVa, 16);
    if (!h || h[0] != 1) return starts;
    auto rd = [&](size_t off, uint8_t enc, uint64_t* v) -> size_t {
        int32_t raw; std::memcpy(&raw, h + off, 4);
        const uint64_t at = hdrVa + off;
        switch (enc & 0x70) {
            case 0x00: *v = (uint32_t)raw; break;
            case 0x10: *v = at + (int64_t)raw; break;
            case 0x30: *v = hdrVa + (int64_t)raw; break;
            default: *v = 0;
        }
        return off + 4;
    };
    uint64_t tmp = 0, count = 0;
    size_t off = rd(4, h[1], &tmp);
    off = rd(off, h[2], &count);
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t loc; off = rd(off, h[3], &loc); off = rd(off, h[3], &tmp);
        starts.push_back(loc);
    }
    return starts;
}

static uint64_t FindString(const uint8_t* f, const std::vector<Load>& L, const char* s, size_t nlen) {
    for (const auto& l : L) {
        Region r{f + l.off, (size_t)l.filesz, l.vaddr};
        if (l.flags & 1) continue;                     // not in code
        if (uint64_t hit = GmrcXrefCore::FindBytes(r, s, nlen)) return hit;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s steamclient.so [Name=0xhex ...]\n", argv[0]); return 2; }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { std::perror("open"); return 2; }
    struct stat st; fstat(fd, &st);
    const uint8_t* f = (const uint8_t*)mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (f == MAP_FAILED) { std::perror("mmap"); return 2; }
    uint64_t ehHdr = 0;
    auto L = ParseLoads64(f, st.st_size, &ehHdr);
    if (L.empty()) { std::puts("FAIL: not an ELF64 with PT_LOAD"); return 1; }
    const Load* ex = nullptr;
    for (const auto& l : L) if (l.flags & 1) { ex = &l; break; }
    if (!ex || !ehHdr) { std::puts("FAIL: no exec segment / no PT_GNU_EH_FRAME"); return 1; }
    Region rx{f + ex->off, (size_t)ex->filesz, ex->vaddr};
    auto starts = EhStarts(f, L, ehHdr);
    if (starts.empty()) { std::puts("FAIL: .eh_frame_hdr unreadable"); return 1; }
    const uint64_t execEnd = ex->vaddr + ex->filesz;
    auto fnOf = [&](uintptr_t site, uintptr_t& s, uintptr_t& e) -> bool {
        auto it = std::upper_bound(starts.begin(), starts.end(), (uint64_t)site);
        if (it == starts.begin()) return false;
        const uint64_t st2 = *(it - 1);
        const uint64_t en = (it != starts.end()) ? *it : execEnd;
        if (site >= en) return false;
        s = st2; e = en; return true;
    };
    std::printf("exec 0x%lx+0x%lx, %zu function starts\n", (unsigned long)ex->vaddr, (unsigned long)ex->filesz, starts.size());

    struct Want { const char* name; uint64_t rva; };
    std::vector<Want> want;
    for (int i = 2; i < argc; ++i) {
        const char* eq = std::strchr(argv[i], '=');
        if (eq) want.push_back({strndup(argv[i], eq - argv[i]), std::strtoull(eq + 1, nullptr, 16)});
    }
    auto expect = [&](const char* name) -> uint64_t {
        for (auto& w : want) if (!std::strcmp(w.name, name)) return w.rva;
        return 0;
    };
    int fails = 0;
    auto report = [&](const char* name, uint64_t got) {
        const uint64_t e = expect(name);
        const bool ok = got && (!e || got == e);
        std::printf("  %-12s -> 0x%lx%s%s\n", name, (unsigned long)got,
                    e ? (got == e ? "  (== expected)" : "  MISMATCH") : "",
                    got ? "" : "  MISS");
        if (!ok) ++fails;
    };
    // String-anchored rescues: the runtime's Locate() on x86-64 = unique rip lea + .eh_frame_hdr.
    struct Anchor { const char* name; const char* needle; bool nul; };
    const Anchor anchors[] = {
        {"GMRC", "ContentServerDirectory.GetManifestRequestCode#1", false},
        {"ShaderDepot", "shadercachedepot", true},
        {"BuildDep", "BuildDepotDependency", true},
    };
    for (const auto& a : anchors) {
        const uint64_t s = FindString(f, L, a.needle, std::strlen(a.needle) + (a.nul ? 1 : 0));
        uint64_t entry = 0;
        if (s) {
            const uintptr_t site = GmrcXrefCore::ScanRipLeaUnique(rx, s);
            uintptr_t st2 = 0, en = 0;
            if (site && fnOf(site, st2, en)) entry = st2;
        }
        report(a.name, entry);
    }
    ReconcileAnchorCore::Info info;
    const uintptr_t rec = ReconcileAnchorCore::LocateNotifyLicensesUpdated64(rx, fnOf, &info);
    std::printf("  Reconcile: %zu callback-125 sites in %zu functions, %zu kept (member disp 0x%x)\n",
                info.sites, info.candidates, info.kept, info.memberDisp);
    report("Reconcile", rec);
    std::puts(fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
