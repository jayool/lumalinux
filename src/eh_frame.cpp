#include "eh_frame.hpp"

#include "log.hpp"
#include "vaddr_xlate.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// See eh_frame.hpp. Parsing scope is deliberately narrow: only the fixed-size
// DWARF exception-header encodings gcc emits here. Anything else fails closed
// rather than being guessed at — a misparsed table would hand the caller a
// confident wrong address, which is the exact failure this module exists to end.
namespace {

// DWARF exception-header encodings (LSB Core Spec §10.5): low nibble = format,
// high nibble = what the value is relative to.
constexpr uint8_t kFmtMask   = 0x0F;
constexpr uint8_t kApplyMask = 0x70;
constexpr uint8_t kUData4    = 0x03;
constexpr uint8_t kSData4    = 0x0B;
constexpr uint8_t kApplyAbs     = 0x00;
constexpr uint8_t kApplyPcRel   = 0x10;
constexpr uint8_t kApplyDataRel = 0x30;

constexpr uint32_t kPtGnuEhFrame = 0x6474E550;

struct Cursor {
    const uint8_t* p = nullptr;
    size_t off = 0;
    size_t size = 0;
    uintptr_t base = 0;          // runtime address of p[0]
    bool ok = true;

    bool Have(size_t n) const { return ok && off + n <= size; }
    uintptr_t Here() const { return base + off; }
};

// One fixed-size encoded value. Marks the cursor bad (rather than returning a
// wrong number) on truncation or an unimplemented encoding.
uintptr_t ReadEncoded(Cursor& c, uint8_t enc, uintptr_t dataBase) {
    if (!c.ok) return 0;
    int32_t raw = 0;
    switch (enc & kFmtMask) {
        case kUData4:
            if (!c.Have(4)) { c.ok = false; return 0; }
            raw = static_cast<int32_t>(*reinterpret_cast<const uint32_t*>(c.p + c.off));
            break;
        case kSData4:
            if (!c.Have(4)) { c.ok = false; return 0; }
            std::memcpy(&raw, c.p + c.off, 4);
            break;
        default:
            Log::Warn("EhFrame: unimplemented value encoding 0x%02x — failing closed", enc);
            c.ok = false;
            return 0;
    }
    const uintptr_t at = c.Here();
    c.off += 4;

    switch (enc & kApplyMask) {
        case kApplyAbs:     return static_cast<uintptr_t>(static_cast<uint32_t>(raw));
        case kApplyPcRel:   return at + static_cast<uintptr_t>(static_cast<intptr_t>(raw));
        case kApplyDataRel: return dataBase + static_cast<uintptr_t>(static_cast<intptr_t>(raw));
        default:
            Log::Warn("EhFrame: unimplemented encoding application 0x%02x — failing closed", enc);
            c.ok = false;
            return 0;
    }
}

// steamclient.so's on-disk path, from /proc/self/maps (Steam's loader hides the
// module from dl_iterate_phdr, so this mirrors how the rest of the code finds it).
std::string SteamclientPath() {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        const auto pos = line.find("/steamclient.so");
        if (pos == std::string::npos) continue;
        const auto slash = line.rfind(" /", pos);
        if (slash == std::string::npos) continue;
        return line.substr(slash + 1);
    }
    return {};
}

// File vaddr of PT_GNU_EH_FRAME, read from the on-disk program headers.
uintptr_t EhFrameHdrVaddr(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    uint8_t eh[52] = {};
    f.read(reinterpret_cast<char*>(eh), sizeof(eh));
    if (f.gcount() < static_cast<std::streamsize>(sizeof(eh))) return 0;
    if (std::memcmp(eh, "\x7f" "ELF", 4) != 0 || eh[4] != 1) return 0;

    uint32_t phoff = 0; uint16_t phentsize = 0, phnum = 0;
    std::memcpy(&phoff, eh + 0x1C, 4);
    std::memcpy(&phentsize, eh + 0x2A, 2);
    std::memcpy(&phnum, eh + 0x2C, 2);
    if (!phoff || !phnum || phentsize < 32) return 0;

    for (uint16_t i = 0; i < phnum; ++i) {
        uint8_t ph[32] = {};
        f.seekg(phoff + static_cast<size_t>(i) * phentsize);
        f.read(reinterpret_cast<char*>(ph), sizeof(ph));
        if (f.gcount() < static_cast<std::streamsize>(sizeof(ph))) return 0;
        uint32_t type = 0, vaddr = 0;
        std::memcpy(&type, ph + 0, 4);
        std::memcpy(&vaddr, ph + 8, 4);
        if (type == kPtGnuEhFrame) return vaddr;
    }
    return 0;
}

// Parsed once: the sorted function-start table. Built on the first call and kept,
// because it is ~130k entries and the input (a mapped module) does not change.
struct Table {
    bool tried = false;
    bool valid = false;
    std::vector<uintptr_t> starts;   // runtime addresses, ascending
};

Table& Get() {
    static Table t;
    if (t.tried) return t;
    t.tried = true;

    const std::string path = SteamclientPath();
    if (path.empty()) {
        Log::Debug("EhFrame: steamclient.so path not found in /proc/self/maps");
        return t;
    }
    const uintptr_t hdrVaddr = EhFrameHdrVaddr(path.c_str());
    if (!hdrVaddr) {
        Log::Warn("EhFrame: no PT_GNU_EH_FRAME in %s", path.c_str());
        return t;
    }
    // The RVA feed Init()s this too, but it may never have run (no feed for this
    // build, no network); Init is idempotent and cheap, so do not depend on it.
    if (!VaddrXlate::Init(path.c_str())) {
        Log::Warn("EhFrame: vaddr translation init failed");
        return t;
    }
    const uintptr_t hdr = VaddrXlate::ToRuntime(hdrVaddr);
    if (!hdr) {
        Log::Warn("EhFrame: .eh_frame_hdr vaddr 0x%lx did not translate",
                  (unsigned long)hdrVaddr);
        return t;
    }

    // The header's own size is not in the program header, and the table is the
    // last thing in the section, so bound the cursor by the remaining bytes of
    // the mapping that holds it rather than by a length we do not have.
    const uint8_t* p = reinterpret_cast<const uint8_t*>(hdr);
    Cursor c{p, 0, /*size=*/0, hdr, true};
    {
        std::ifstream maps("/proc/self/maps");
        std::string line;
        while (std::getline(maps, line)) {
            unsigned long s = 0, e = 0;
            if (std::sscanf(line.c_str(), "%lx-%lx", &s, &e) < 2) continue;
            if (hdr >= s && hdr < e) { c.size = e - hdr; break; }
        }
    }
    if (c.size < 8) {
        Log::Warn("EhFrame: .eh_frame_hdr at 0x%lx is not in a readable mapping",
                  (unsigned long)hdr);
        return t;
    }

    const uint8_t version = p[0];
    const uint8_t ehFramePtrEnc = p[1];
    const uint8_t fdeCountEnc = p[2];
    const uint8_t tableEnc = p[3];
    c.off = 4;
    if (version != 1) {
        Log::Warn("EhFrame: .eh_frame_hdr version %u unsupported — failing closed", version);
        return t;
    }

    (void)ReadEncoded(c, ehFramePtrEnc, hdr);          // eh_frame_ptr, unused
    const uintptr_t fdeCount = ReadEncoded(c, fdeCountEnc, hdr);
    if (!c.ok || !fdeCount || fdeCount > (c.size / 8) + 1) {
        Log::Warn("EhFrame: implausible fde_count %lu — failing closed",
                  (unsigned long)fdeCount);
        return t;
    }

    t.starts.reserve(fdeCount);
    uintptr_t prev = 0;
    for (uintptr_t i = 0; i < fdeCount; ++i) {
        const uintptr_t loc = ReadEncoded(c, tableEnc, hdr);
        (void)ReadEncoded(c, tableEnc, hdr);           // fde pointer, unused
        if (!c.ok) {
            Log::Warn("EhFrame: table truncated at entry %lu of %lu — failing closed",
                      (unsigned long)i, (unsigned long)fdeCount);
            t.starts.clear();
            return t;
        }
        // Sortedness is what makes the binary search valid, so verify it instead
        // of assuming it; an unsorted table would silently return wrong answers.
        if (loc < prev) {
            Log::Warn("EhFrame: table not sorted at entry %lu — failing closed",
                      (unsigned long)i);
            t.starts.clear();
            return t;
        }
        prev = loc;
        t.starts.push_back(loc);
    }

    t.valid = true;
    Log::Info("EhFrame: %lu function starts from .eh_frame_hdr @ 0x%lx (0x%lx..0x%lx)",
              (unsigned long)t.starts.size(), (unsigned long)hdr,
              (unsigned long)t.starts.front(), (unsigned long)t.starts.back());
    return t;
}

} // namespace

namespace EhFrame {

bool FindFunction(uintptr_t addr, uintptr_t execEnd,
                  uintptr_t& start, uintptr_t& end) {
    Table& t = Get();
    if (!t.valid || t.starts.empty()) return false;

    // Largest start <= addr.
    size_t lo = 0, hi = t.starts.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (t.starts[mid] <= addr) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return false;                    // before the first function
    const size_t i = lo - 1;

    // A function ends where the next one starts; the last needs the caller's
    // bound, without which any address past it would look "inside" and the
    // fail-closed guarantee would be lost.
    const uintptr_t e = (i + 1 < t.starts.size()) ? t.starts[i + 1] : execEnd;
    if (!e || addr >= e) return false;

    start = t.starts[i];
    end = e;
    return true;
}

} // namespace EhFrame
