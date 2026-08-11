#include "vaddr_xlate.hpp"

#include <cstdio>
#include <cstring>

namespace VaddrXlate {
namespace {

std::vector<Load>    g_loads;
std::vector<Mapping> g_maps;

uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }

} // namespace

uintptr_t Translate(uintptr_t v,
                    const std::vector<Load>& loads,
                    const std::vector<Mapping>& maps) {
    for (const auto& L : loads) {
        if (v < L.p_vaddr || v >= L.p_vaddr + L.p_filesz) continue;  // file-backed only
        const uintptr_t foff = L.p_offset + (v - L.p_vaddr);
        for (const auto& m : maps) {
            if (foff >= m.offset && foff < m.offset + (m.end - m.start))
                return m.start + (foff - m.offset);
        }
        return 0;  // file offset not currently mapped
    }
    return 0;
}

bool ParseLoads(const char* path, std::vector<Load>& out) {
    out.clear();
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    uint8_t hdr[52];
    if (std::fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        std::memcmp(hdr, "\x7f" "ELF", 4) != 0 || hdr[4] != 1 /* ELFCLASS32 */) {
        std::fclose(f);
        return false;
    }
    const uint32_t e_phoff     = rd32(hdr + 0x1C);
    const uint16_t e_phentsize = rd16(hdr + 0x2A);
    const uint16_t e_phnum     = rd16(hdr + 0x2C);
    if (e_phentsize < 32 || e_phnum == 0 || e_phnum > 256) { std::fclose(f); return false; }
    if (std::fseek(f, static_cast<long>(e_phoff), SEEK_SET) != 0) { std::fclose(f); return false; }

    std::vector<uint8_t> ph(static_cast<size_t>(e_phentsize) * e_phnum);
    if (std::fread(ph.data(), 1, ph.size(), f) != ph.size()) { std::fclose(f); return false; }
    std::fclose(f);

    for (uint16_t i = 0; i < e_phnum; ++i) {
        const uint8_t* p = ph.data() + static_cast<size_t>(i) * e_phentsize;
        if (rd32(p) != 1) continue;   // PT_LOAD
        out.push_back({ rd32(p + 4), rd32(p + 8), rd32(p + 16) });  // offset, vaddr, filesz
    }
    return !out.empty();
}

void ParseMaps(const std::string& mapsText, std::vector<Mapping>& out) {
    out.clear();
    size_t pos = 0;
    while (pos < mapsText.size()) {
        const size_t nl = mapsText.find('\n', pos);
        const std::string line = mapsText.substr(
            pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? mapsText.size() : nl + 1;

        if (line.find("steamclient.so") == std::string::npos) continue;
        unsigned long s = 0, e = 0, off = 0;
        char perms[8] = {};
        if (std::sscanf(line.c_str(), "%lx-%lx %7s %lx", &s, &e, perms, &off) < 4) continue;
        if (e <= s) continue;
        out.push_back({ static_cast<uintptr_t>(s),
                        static_cast<uintptr_t>(e),
                        static_cast<uintptr_t>(off) });
    }
}

bool Init(const char* steamclientPath) {
    if (!ParseLoads(steamclientPath, g_loads)) return false;

    FILE* f = std::fopen("/proc/self/maps", "rb");
    if (!f) return false;
    std::string text;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);

    ParseMaps(text, g_maps);
    return !g_maps.empty();
}

uintptr_t ToRuntime(uintptr_t fileVaddr) {
    return Translate(fileVaddr, g_loads, g_maps);
}

} // namespace VaddrXlate
