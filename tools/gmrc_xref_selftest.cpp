// gmrc_xref_selftest.cpp — offline validation of the GMRC xref core against a
// real steamclient.so, WITHOUT loading Steam. It mmaps the file, reconstructs
// the binary's vaddr space from the ELF program headers, and runs the SAME
// GmrcXrefCore scanners the runtime locator uses. If it reproduces the getter
// entry the byte pattern / Python oracle found (0x12db210 on build 1c168d2f),
// the C++ port is proven byte-for-byte. Suitable as a CI gate (no Steam needed).
//
//   g++ -O2 -std=c++17 tools/gmrc_xref_selftest.cpp -o /tmp/gmrc_selftest
//   /tmp/gmrc_selftest .steamcmd/linux32/steamclient.so [expected_rva_hex]
//
// Exit 0 = entry derived (and == expected, if given); 1 = miss/mismatch.
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
        std::fprintf(stderr, "usage: %s <steamclient.so> [expected_rva_hex]\n", argv[0]);
        return 2;
    }
    const char* path = argv[1];
    uintptr_t expected = (argc >= 3) ? std::strtoul(argv[2], nullptr, 16) : 0x12db210;

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
        strAddr = GmrcXrefCore::FindBytes(r, kJobName, std::strlen(kJobName));
        if (strAddr) break;
    }
    std::printf("binary        : %s (%zu bytes)\n", path, fsize);
    std::printf("exec segment  : vaddr 0x%lx size 0x%zx\n",
                (unsigned long)rx.addr, rx.size);
    if (!strAddr) { std::fprintf(stderr, "FAIL: job-name string not found\n"); return 1; }
    std::printf("job-name @    : vaddr 0x%lx\n", (unsigned long)strAddr);

    uintptr_t got = 0, site = 0;
    uintptr_t entry = GmrcXrefCore::DeriveGmrcEntry(rx, strAddr, &got, &site);
    std::printf("GOT base      : 0x%lx\n", (unsigned long)got);
    std::printf("lea site      : 0x%lx\n", (unsigned long)site);
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
