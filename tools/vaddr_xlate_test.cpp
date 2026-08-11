// vaddr_xlate_test.cpp — mirror test for src/vaddr_xlate.cpp. Host-compilable
// (the logic is arch-independent; the ELF fields are read as 32-bit regardless):
//
//   g++ -O2 -std=c++20 -I src tools/vaddr_xlate_test.cpp src/vaddr_xlate.cpp -o /tmp/t
//   /tmp/t
//
// Covers the same cases as tools/xlate_vaddr.py's self-test, plus the exact
// numbers observed live in the codespace (rva 0x118c1f0 -> 0xcfa101f0).
#include "vaddr_xlate.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace VaddrXlate;

static int g_fail = 0;
static void check(const char* name, uintptr_t got, uintptr_t want) {
    bool ok = (got == want);
    std::printf("  %-28s got=0x%lx want=0x%lx  %s\n", name,
                (unsigned long)got, (unsigned long)want, ok ? "PASS" : "FAIL");
    if (!ok) g_fail = 1;
}

static void put32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    std::memcpy(&b[off], &v, 4);
}
static void put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    std::memcpy(&b[off], &v, 2);
}

int main() {
    // ── Translate: REAL codespace numbers ────────────────────────────────────
    // exec PT_LOAD p_vaddr=0xe7d3b0 p_offset=0xe7c3b0; r-x mapping start=0xcf701000
    // file offset 0xe7c000; rva 0x118c1f0 -> runtime 0xcfa101f0 (byte-verified live).
    {
        std::vector<Load> loads = {{0xe7c3b0, 0xe7d3b0, 0x2000000}};
        std::vector<Mapping> maps = {{0xcf701000, 0xd16da000, 0xe7c000}};
        std::printf("REAL (live-verified):\n");
        check("DepotKey 0x118c1f0", Translate(0x118c1f0, loads, maps), 0xcfa101f0);
    }

    // ── Translate: multi-bias (two segments at unrelated biases) ─────────────
    {
        std::vector<Load> loads = {
            {0x1000,    0x1000,    0x400000},   // exec
            {0x2a00000, 0x2e6c000, 0x200000},   // data: p_offset != p_vaddr
        };
        std::vector<Mapping> maps = {
            {0xAAAA0000, 0xAAAA0000 + 0x400000, 0x1000},     // exec mapped here
            {0x50000000, 0x50000000 + 0x200000, 0x2a00000},  // data mapped far away
        };
        std::printf("MULTI-BIAS:\n");
        // exec vaddr 0x50000 -> 0xAAAA0000 + (0x50000 - 0x1000)
        check("exec 0x50000", Translate(0x50000, loads, maps), 0xAAAA0000 + (0x50000 - 0x1000));
        // data vaddr 0x2e6cc2c -> foff 0x2a00c2c -> 0x50000000 + (0x2a00c2c-0x2a00000)
        check("data 0x2e6cc2c", Translate(0x2e6cc2c, loads, maps),
              0x50000000 + (0x2a00000 + (0x2e6cc2c - 0x2e6c000) - 0x2a00000));
        // vaddr outside any load -> 0
        check("miss 0x99999999", Translate(0x99999999, loads, maps), 0);
    }

    // ── ParseMaps ────────────────────────────────────────────────────────────
    {
        std::string maps =
            "cf701000-d16da000 r-xp 00e7c000 00:23 42  /home/u/.local/share/Steam/ubuntu12_32/steamclient.so\n"
            "d16da000-d1700000 r--p 02e55000 00:23 42  /home/u/.local/share/Steam/ubuntu12_32/steamclient.so\n"
            "7f000000-7f001000 rw-p 00000000 00:00 0 \n"          // unrelated, ignored
            "c4000000-c4001000 r-xp 00000000 00:23 99  /usr/lib/libc.so\n"; // other lib, ignored
        std::vector<Mapping> out;
        ParseMaps(maps, out);
        std::printf("ParseMaps:\n");
        check("count", (uintptr_t)out.size(), 2);
        if (out.size() == 2) {
            check("m0.start", out[0].start, 0xcf701000);
            check("m0.offset", out[0].offset, 0xe7c000);
            check("m1.offset", out[1].offset, 0x2e55000);
        } else {
            g_fail = 1;
        }
    }

    // ── ParseLoads on a synthetic ELF32 ──────────────────────────────────────
    {
        std::vector<uint8_t> elf(0x34 + 2 * 0x20, 0);
        std::memcpy(&elf[0], "\x7f" "ELF", 4);
        elf[4] = 1;                          // ELFCLASS32
        put32(elf, 0x1C, 0x34);              // e_phoff
        put16(elf, 0x2A, 0x20);              // e_phentsize
        put16(elf, 0x2C, 2);                 // e_phnum
        auto ph = [&](size_t o, uint32_t typ, uint32_t off, uint32_t va, uint32_t fsz) {
            put32(elf, o + 0,  typ);
            put32(elf, o + 4,  off);
            put32(elf, o + 8,  va);
            put32(elf, o + 16, fsz);
        };
        ph(0x34,        1, 0xe7c3b0, 0xe7d3b0, 0x2000000);  // PT_LOAD exec
        ph(0x34 + 0x20, 1, 0x2a00000, 0x2e6c000, 0x200000); // PT_LOAD data
        char path[] = "/tmp/vaddr_xlate_synth.soXXXXXX";
        int fd = mkstemp(path);
        if (fd >= 0) {
            FILE* f = fdopen(fd, "wb");
            std::fwrite(elf.data(), 1, elf.size(), f);
            std::fclose(f);
            std::vector<Load> loads;
            bool ok = ParseLoads(path, loads);
            std::remove(path);
            std::printf("ParseLoads:\n");
            check("ok", ok ? 1 : 0, 1);
            check("count", (uintptr_t)loads.size(), 2);
            if (loads.size() == 2) {
                check("L0.p_vaddr", loads[0].p_vaddr, 0xe7d3b0);
                check("L0.p_offset", loads[0].p_offset, 0xe7c3b0);
                check("L1.p_filesz", loads[1].p_filesz, 0x200000);
            } else {
                g_fail = 1;
            }
        } else {
            std::printf("ParseLoads: SKIP (mkstemp failed)\n");
        }
    }

    std::printf("\n%s\n", g_fail ? "RESULT: FAIL" : "RESULT: ALL PASS");
    return g_fail;
}
