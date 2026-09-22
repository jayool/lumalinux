// gmrc_xref_selftest.cpp — offline validation of the string-xref core against a
// real steamclient.so, WITHOUT loading Steam. It mmaps the file, reconstructs
// the binary's vaddr space from the ELF program headers, and runs the SAME
// GmrcXrefCore scanners the runtime locator uses. If it reproduces the getter
// entry the byte pattern / Python oracle found (0x12db210 on build 1c168d2f),
// the C++ port is proven byte-for-byte. Suitable as a CI gate (no Steam needed).
//
//   g++ -O2 -std=c++17 tools/gmrc_xref_selftest.cpp -o /tmp/gmrc_selftest
//   /tmp/gmrc_selftest <steamclient.so> [expected_entry_hex]
//
// Since 2026-09-22 the same core serves any string anchor (ShaderDepot,
// BuildDep — src/gmrc_xref.cpp FindFunctionByString). For those the entry
// comes from .eh_frame_hdr at runtime, which this offline tool does not parse,
// so the checkable quantity is the lea SITE (steps 1-3, the shared part):
//
//   /tmp/gmrc_selftest <steamclient.so> --needle shadercachedepot --expect-site <hex>
//
// searches the string WITH its NUL terminator (as the runtime does for these)
// and compares the unique lea site against the one check_patterns.py's
// gmrc_xref_derive reports (`sites`), which is the Python mirror that verified
// both strings on two builds. The walk-back entry is printed for information
// only: it is a few bytes off for these prologue shapes (see the core header).
//
// Exit 0 = derived (and == expected, if given); 1 = miss/mismatch; 2 = usage.
#include "../src/gmrc_xref_core.hpp"

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

static const char* kJobName =
    "ContentServerDirectory.GetManifestRequestCode#1";

static uint32_t rd32(const uint8_t* p) {
    uint32_t v; std::memcpy(&v, p, 4); return v;
}
static uint16_t rd16(const uint8_t* p) {
    uint16_t v; std::memcpy(&v, p, 2); return v;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <steamclient.so> [expected_entry_hex]\n"
                             "       %s <steamclient.so> --needle <string> [--expect-site <hex>]\n",
                     argv[0], argv[0]);
        return 2;
    }
    const char* path = argv[1];
    uintptr_t expected = 0x12db210;
    const char* needle = kJobName;
    bool needleMode = false;
    uintptr_t expectSite = 0;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--needle") == 0 && i + 1 < argc) {
            needle = argv[++i]; needleMode = true; expected = 0;
        } else if (std::strcmp(argv[i], "--expect-site") == 0 && i + 1 < argc) {
            expectSite = std::strtoul(argv[++i], nullptr, 16);
        } else if (!needleMode) {
            expected = std::strtoul(argv[i], nullptr, 16);
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return 2;
        }
    }
    // Needle mode searches WITH the NUL terminator, exactly like
    // GmrcXref::FindFunctionByString; the GMRC path keeps the bare job name.
    const std::size_t nlen = std::strlen(needle) + (needleMode ? 1 : 0);

    int fd = open(path, O_RDONLY);
    if (fd < 0) { std::perror("open"); return 2; }
    struct stat st;
    if (fstat(fd, &st) != 0) { std::perror("fstat"); close(fd); return 2; }
    const std::size_t fsize = static_cast<std::size_t>(st.st_size);
    const uint8_t* map =
        reinterpret_cast<const uint8_t*>(mmap(nullptr, fsize, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    if (map == MAP_FAILED) { std::perror("mmap"); return 2; }

    if (fsize < 0x34 || std::memcmp(map, "\x7f""ELF", 4) != 0 || map[4] != 1) {
        std::fprintf(stderr, "not an ELFCLASS32 file (i386 steamclient.so expected)\n");
        return 2;
    }

    const uint32_t e_phoff = rd32(map + 0x1C);
    const uint16_t e_phentsize = rd16(map + 0x2A);
    const uint16_t e_phnum = rd16(map + 0x2C);

    // Aggregate PT_LOAD segments: exec region (PF_X) for scanning, all readable
    // (PF_R) for the string search. addr = p_vaddr, so the core works in vaddr
    // space exactly as the runtime works in loaded space.
    uintptr_t exLo = ~uintptr_t(0), exHi = 0;
    const uint8_t* exPtr = nullptr;
    std::vector<Region> readable;
    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t* ph = map + e_phoff + static_cast<std::size_t>(i) * e_phentsize;
        if (rd32(ph) != 1) continue;                 // PT_LOAD
        const uint32_t off = rd32(ph + 4);
        const uint32_t vaddr = rd32(ph + 8);
        const uint32_t filesz = rd32(ph + 16);
        const uint32_t flags = rd32(ph + 24);
        if (static_cast<std::size_t>(off) + filesz > fsize) continue;
        const uint8_t* segPtr = map + off;
        if (flags & 0x4) {                           // PF_R
            readable.push_back(Region{ segPtr, filesz, vaddr });
        }
        if (flags & 0x1) {                           // PF_X
            if (vaddr < exLo) { exLo = vaddr; exPtr = segPtr; }
            if (vaddr + filesz > exHi) exHi = vaddr + filesz;
        }
    }
    if (!exPtr || exHi <= exLo) {
        std::fprintf(stderr, "no executable PT_LOAD segment found\n");
        return 2;
    }
    Region rx{ exPtr, static_cast<std::size_t>(exHi - exLo), exLo };

    uintptr_t strAddr = 0;
    for (const Region& r : readable) {
        strAddr = GmrcXrefCore::FindBytes(r, needle, nlen);
        if (strAddr) break;
    }
    std::printf("binary        : %s (%zu bytes)\n", path, fsize);
    std::printf("exec segment  : vaddr 0x%lx size 0x%zx\n",
                (unsigned long)rx.addr, rx.size);
    if (!strAddr) { std::fprintf(stderr, "FAIL: anchor string not found\n"); return 1; }
    std::printf("anchor        : %s\n", needle);
    std::printf("string @      : vaddr 0x%lx\n", (unsigned long)strAddr);

    uintptr_t got = 0, site = 0;
    uintptr_t entry = GmrcXrefCore::DeriveGmrcEntry(rx, strAddr, &got, &site);
    std::printf("GOT base      : 0x%lx\n", (unsigned long)got);
    std::printf("lea site      : 0x%lx\n", (unsigned long)site);
    if (needleMode) {
        std::printf("walk-back     : 0x%lx (informational; runtime uses .eh_frame_hdr)\n",
                    (unsigned long)entry);
        if (!site) { std::fprintf(stderr, "FAIL: no unique lea site\n"); return 1; }
        if (expectSite && site != expectSite) {
            std::printf("RESULT        : site 0x%lx != expected 0x%lx  -> MISMATCH\n",
                        (unsigned long)site, (unsigned long)expectSite);
            return 1;
        }
        std::printf("RESULT        : PASS — core finds the unique lea site 0x%lx\n",
                    (unsigned long)site);
        return 0;
    }
    std::printf("getter entry  : 0x%lx\n", (unsigned long)entry);

    if (!entry) { std::fprintf(stderr, "FAIL: no entry derived\n"); return 1; }
    if (expected && entry != expected) {
        std::printf("RESULT        : entry 0x%lx != expected 0x%lx  -> MISMATCH\n",
                    (unsigned long)entry, (unsigned long)expected);
        std::printf("  (a different build legitimately moves it; pass the build's RVA\n"
                    "   as arg 2 to compare, or cross-check with verify_gmrc_anchor.py)\n");
        return 1;
    }
    std::printf("RESULT        : PASS — core reproduces getter entry 0x%lx\n",
                (unsigned long)entry);
    return 0;
}
