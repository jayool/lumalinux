#include "sls_update_unblock.hpp"
#include "log.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace SlsUpdateUnblock {
namespace {

// The combined update-clear immediate: ~(0x2|0x8|0x10|0x100|0x200|0x400) = ~0x71A
// = 0xFFFFF8E5, stored little-endian in the instruction as E5 F8 FF FF. This
// constant is derived from Steam's APPSTATE_UPDATE_* enum, which is ABI-stable
// (games depend on the flag values), so it survives SLSsteam recompiles even as
// surrounding code shifts — the reason we anchor on it rather than a prologue.
constexpr uint8_t  kImm[4]      = { 0xE5, 0xF8, 0xFF, 0xFF };  // 0xFFFFF8E5
constexpr uint32_t kPatchedImm  = 0xFFFFFFFFu;                 // AND all-ones = no-op

struct Range { uintptr_t start, end; };

// Collect SLSsteam's individual executable (r-x) mappings. We scan each mapping
// separately (never an aggregated min..max span) so we can't read an unmapped
// hole between segments. Matches "SLSsteam.so" (generic release) and
// "libSLSsteam.so" (Arch) — the former is a substring of the latter.
bool FindSlsRx(std::vector<Range>& out) {
    std::ifstream maps("/proc/self/maps");
    if (!maps.is_open()) return false;
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("SLSsteam.so") == std::string::npos) continue;
        if (line.find("r-x") == std::string::npos) continue;
        auto dash = line.find('-');
        auto sp   = line.find(' ');
        if (dash == std::string::npos || sp == std::string::npos) continue;
        uintptr_t s = std::strtoul(line.substr(0, dash).c_str(), nullptr, 16);
        uintptr_t e = std::strtoul(line.substr(dash + 1, sp - dash - 1).c_str(),
                                    nullptr, 16);
        if (e > s) out.push_back({ s, e });
    }
    return !out.empty();
}

// If the imm32 beginning at `imm` is the operand of a `81 /4 r/m32, imm32` (AND)
// instruction, return the instruction's start address; else 0. The `81 /4` prefix
// is what tells the real clear apart from the (six, on the verified build)
// jump/call displacements that coincidentally equal 0xFFFFF8E5 — those are
// preceded by e9 / e8 / 0f 84, never by an 81 with reg field 4. We handle the
// three non-SIB addressing forms (info->state is a plain [reg+disp]):
//   mod=01 disp8 : 81 modrm disp8  imm   (the form observed & verified: 81 66 08)
//   mod=00 nodisp: 81 modrm        imm
//   mod=10 disp32: 81 modrm disp32 imm
uintptr_t AndInsnStart(uintptr_t imm, uintptr_t lo) {
    struct Form { unsigned dist; uint8_t modBits; };  // modBits = mod field << 6
    static const Form forms[] = { { 3, 0x40 }, { 2, 0x00 }, { 6, 0x80 } };
    for (const Form& f : forms) {
        if (imm < lo + f.dist) continue;
        uintptr_t op   = imm - f.dist;
        uint8_t   code = *reinterpret_cast<const uint8_t*>(op);
        uint8_t   modrm = *reinterpret_cast<const uint8_t*>(op + 1);
        if (code != 0x81) continue;
        if (((modrm >> 3) & 7) != 4) continue;            // reg field == /4 (AND)
        if ((modrm & 0xC0) != f.modBits) continue;        // mod matches this form
        if ((modrm & 7) == 4) continue;                   // rm==100 → SIB (unhandled)
        if (f.modBits == 0x00 && (modrm & 7) == 5) continue;  // mod=00 rm=101 → [disp32]
        return op;
    }
    return 0;
}

// Same atomic, executable-window-preserving write as the achievement patch's
// WriteRel32 (src/sls_achievement_unblock.cpp + RESEARCH §17.4). This clear runs
// on a COLD path (app-state queries, not a boot-hammered function), so the old
// byte-wise / non-exec-window write never collided in practice — but it was the
// exact same latent torn-write hazard that OOBE'd the achievement patch, so it
// gets the same fix: keep PROT_EXEC across the write, and store the 4-byte
// immediate atomically. A thread executing the AND concurrently then reads either
// the whole old immediate or the whole new one, never a torn mix.
bool WritePatch(uintptr_t imm) {
    constexpr uintptr_t kCacheLine = 64;
    if ((imm & (kCacheLine - 1)) > kCacheLine - 4) {
        Log::Warn("SLS-unblock: imm at 0x%lx straddles a cache line — leaving block "
                  "in place (no torn write)", (unsigned long)imm);
        return false;
    }
    const long pageSz = sysconf(_SC_PAGESIZE);
    const uintptr_t pageMask = ~static_cast<uintptr_t>(pageSz - 1);
    uintptr_t first = imm & pageMask;
    uintptr_t last  = (imm + 3) & pageMask;   // the 4 bytes may straddle a page
    void*  page = reinterpret_cast<void*>(first);
    size_t len  = (last - first) + static_cast<size_t>(pageSz);
    // Keep EXEC set — the page is never non-executable during the write.
    if (mprotect(page, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        Log::Warn("SLS-unblock: mprotect(RWX) failed at 0x%lx — leaving block in place",
                  (unsigned long)imm);
        return false;
    }
    __atomic_store_n(reinterpret_cast<uint32_t*>(imm), kPatchedImm, __ATOMIC_SEQ_CST);
    mprotect(page, len, PROT_READ | PROT_EXEC);   // best-effort restore to RX
    return true;
}

} // namespace

bool Apply() {
    if (const char* off = std::getenv("LUMA_NO_SLS_UNBLOCK");
        off && off[0] && off[0] != '0') {
        Log::Info("SLS-unblock: disabled via LUMA_NO_SLS_UNBLOCK");
        return false;
    }

    std::vector<Range> ranges;
    if (!FindSlsRx(ranges)) {
        // No SLSsteam in the process → no block to remove. Not an error: lumalinux
        // can run standalone (there's simply no update-block to undo then).
        Log::Info("SLS-unblock: SLSsteam.so not mapped — nothing to patch");
        return false;
    }

    // Scan each r-x mapping for the combined-clear immediate, keeping only those
    // that form a real `81 /4 …, imm32` AND. Require EXACTLY ONE (fail-safe).
    struct Hit { uintptr_t imm, insn; };
    std::vector<Hit> hits;
    for (const Range& r : ranges) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(r.start);
        const size_t   n = r.end - r.start;
        for (size_t i = 0; i + 4 <= n; ++i) {
            if (p[i] != kImm[0] || p[i + 1] != kImm[1] ||
                p[i + 2] != kImm[2] || p[i + 3] != kImm[3]) continue;
            uintptr_t imm  = r.start + i;
            uintptr_t insn = AndInsnStart(imm, r.start);
            if (insn != 0) hits.push_back({ imm, insn });
        }
    }

    if (hits.size() != 1) {
        Log::Warn("SLS-unblock: expected 1 update-clear AND, found %zu — leaving "
                  "SLSsteam's update-block in place (auto-update falls back to "
                  "manual). SLSsteam codegen may have shifted; refresh the anchor.",
                  hits.size());
        return false;
    }

    const uintptr_t imm  = hits[0].imm;
    const uintptr_t insn = hits[0].insn;
    if (!WritePatch(imm)) return false;

    Log::Info("SLS-unblock: patched SLSsteam update-clear at insn=0x%lx (imm=0x%lx) "
              "-> and reg,0xFFFFFFFF (no-op). AdditionalApps games auto-update again.",
              (unsigned long)insn, (unsigned long)imm);
    return true;
}

} // namespace SlsUpdateUnblock
