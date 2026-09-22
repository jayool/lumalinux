// reconcile_anchor_selftest.cpp — offline validation of the Reconcile anchor
// locator (src/reconcile_anchor_core.hpp) against a real steamclient.so,
// WITHOUT loading Steam. mmaps the file, rebuilds the vaddr space from the ELF
// program headers, parses .eh_frame_hdr's function table from the file (the
// runtime parses the same table from the mapped module, src/eh_frame.cpp), and
// runs the SAME core the runtime rescue runs. If it lands on the function the
// Python deriver found (0x188c950 on bc54101b, 0x1a0d010 on 9cf4720f), the C++
// port is proven on that build.
//
//   g++ -O2 -std=c++17 tools/reconcile_anchor_selftest.cpp -o /tmp/reconcile_selftest
//   /tmp/reconcile_selftest <steamclient.so> [expected_rva_hex]
//
// Exit 0 = derived (and == expected, if given); 1 = miss/mismatch; 2 = usage.
#include "../src/reconcile_anchor_core.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using GmrcXrefCore::Region;

static uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

// .eh_frame_hdr (LSB §10.6.2): version, eh_frame_ptr_enc, fde_count_enc,
// table_enc, then eh_frame_ptr, fde_count, and fde_count pairs (initial_loc,
// fde_addr) sorted by initial_loc. Only the fixed-size encodings gcc emits here
// (udata4 / sdata4, absolute / pcrel / datarel) — like the runtime, anything
// else fails closed.
static bool readEncoded(const uint8_t* p, std::size_t size, std::size_t& off, uint8_t enc,
                        uintptr_t hdrVaddr, uintptr_t& out) {
    if (off + 4 > size) return false;
    const uint8_t fmt = enc & 0x0F, app = enc & 0x70;
    int32_t raw = 0;
    if (fmt == 0x03) raw = static_cast<int32_t>(rd32(p + off));          // udata4
    else if (fmt == 0x0B) std::memcpy(&raw, p + off, 4);                // sdata4
    else return false;
    const uintptr_t at = hdrVaddr + off;
    off += 4;
    if (app == 0x00) out = static_cast<uint32_t>(raw);
    else if (app == 0x10) out = at + static_cast<intptr_t>(raw);        // pcrel
    else if (app == 0x30) out = hdrVaddr + static_cast<intptr_t>(raw);  // datarel
    else return false;
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <steamclient.so> [expected_rva_hex]\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    const uintptr_t expected = (argc >= 3) ? std::strtoul(argv[2], nullptr, 16) : 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) { std::perror("open"); return 2; }
    struct stat st;
    if (fstat(fd, &st) != 0) { std::perror("fstat"); close(fd); return 2; }
    const std::size_t fsize = static_cast<std::size_t>(st.st_size);
    const uint8_t* map = reinterpret_cast<const uint8_t*>(mmap(nullptr, fsize, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    if (map == MAP_FAILED) { std::perror("mmap"); return 2; }
    if (fsize < 0x34 || std::memcmp(map, "\x7f" "ELF", 4) != 0 || map[4] != 1) {
        std::fprintf(stderr, "not an ELFCLASS32 file (i386 steamclient.so expected)\n");
        return 2;
    }

    const uint32_t e_phoff = rd32(map + 0x1C);
    const uint16_t e_phentsize = rd16(map + 0x2A);
    const uint16_t e_phnum = rd16(map + 0x2C);

    uintptr_t exLo = ~uintptr_t(0), exHi = 0;
    const uint8_t* exPtr = nullptr;
    uint32_t ehOff = 0, ehVaddr = 0, ehSize = 0;
    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t* ph = map + e_phoff + static_cast<std::size_t>(i) * e_phentsize;
        const uint32_t type = rd32(ph), off = rd32(ph + 4), vaddr = rd32(ph + 8);
        const uint32_t filesz = rd32(ph + 16), flags = rd32(ph + 24);
        if (static_cast<std::size_t>(off) + filesz > fsize) continue;
        if (type == 1 && (flags & 0x1)) {                    // PT_LOAD, PF_X
            if (vaddr < exLo) { exLo = vaddr; exPtr = map + off; }
            if (vaddr + filesz > exHi) exHi = vaddr + filesz;
        }
        if (type == 0x6474E550) { ehOff = off; ehVaddr = vaddr; ehSize = filesz; }   // PT_GNU_EH_FRAME
    }
    if (!exPtr || exHi <= exLo) { std::fprintf(stderr, "no executable PT_LOAD\n"); return 2; }
    const Region rx{ exPtr, static_cast<std::size_t>(exHi - exLo), exLo };
    std::printf("binary        : %s (%zu bytes)\n", path, fsize);
    std::printf("exec segment  : vaddr 0x%lx size 0x%zx\n", (unsigned long)rx.addr, rx.size);

    // Function-start table from .eh_frame_hdr.
    std::vector<uintptr_t> starts;
    if (!ehSize || ehSize < 8) { std::fprintf(stderr, "FAIL: no PT_GNU_EH_FRAME\n"); return 1; }
    {
        const uint8_t* p = map + ehOff;
        std::size_t off = 4;
        if (p[0] != 1) { std::fprintf(stderr, "FAIL: .eh_frame_hdr version %u\n", p[0]); return 1; }
        uintptr_t tmp = 0, count = 0;
        if (!readEncoded(p, ehSize, off, p[1], ehVaddr, tmp) ||
            !readEncoded(p, ehSize, off, p[2], ehVaddr, count)) {
            std::fprintf(stderr, "FAIL: .eh_frame_hdr header encoding unsupported\n"); return 1;
        }
        starts.reserve(count);
        for (uintptr_t i = 0; i < count; ++i) {
            uintptr_t loc = 0, fde = 0;
            if (!readEncoded(p, ehSize, off, p[3], ehVaddr, loc) ||
                !readEncoded(p, ehSize, off, p[3], ehVaddr, fde)) {
                std::fprintf(stderr, "FAIL: .eh_frame_hdr table truncated at %lu/%lu\n",
                             (unsigned long)i, (unsigned long)count); return 1;
            }
            if (!starts.empty() && loc < starts.back()) {
                std::fprintf(stderr, "FAIL: .eh_frame_hdr table not sorted\n"); return 1;
            }
            starts.push_back(loc);
        }
    }
    std::printf("eh_frame_hdr  : %zu function starts (0x%lx..0x%lx)\n", starts.size(),
                (unsigned long)starts.front(), (unsigned long)starts.back());

    auto fnOf = [&](uintptr_t site, uintptr_t& s, uintptr_t& e) -> bool {
        auto it = std::upper_bound(starts.begin(), starts.end(), site);
        if (it == starts.begin()) return false;
        s = *(it - 1);
        e = (it != starts.end()) ? *it : rx.addr + rx.size;
        return site < e;
    };

    ReconcileAnchorCore::Info info;
    const uintptr_t entry = ReconcileAnchorCore::LocateNotifyLicensesUpdated(rx, fnOf, &info);
    std::printf("callback sites: %zu in %zu function(s); this-read + jle keeps %zu\n",
                info.sites, info.candidates, info.kept);
    if (!entry) {
        std::fprintf(stderr, "FAIL: %s\n", info.kept ? "AMBIGUOUS" : "NOT_FOUND");
        return 1;
    }
    std::printf("entry         : 0x%lx (member disp 0x%x, informational)\n",
                (unsigned long)entry, info.memberDisp);
    if (expected && entry != expected) {
        std::printf("RESULT        : 0x%lx != expected 0x%lx  -> MISMATCH\n",
                    (unsigned long)entry, (unsigned long)expected);
        return 1;
    }
    std::printf("RESULT        : PASS — core lands on 0x%lx\n", (unsigned long)entry);
    return 0;
}
