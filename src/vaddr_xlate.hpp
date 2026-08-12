#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Translate a steamclient.so FILE vaddr (RVA, image base 0) to its live runtime
// address in THIS process. Correct even when the loader maps the module's
// segments at DIFFERENT biases (split mapping) — where `mappingBase + rva` (what
// a Windows tool does on a contiguous DLL) would be wrong on Linux.
//
// It maps:  rva -> file offset  (via the on-disk ELF program headers)
//               -> runtime addr  (via the file-offset column of /proc/self/maps).
//
// This is the runtime half of the RVA-first feed (docs/rva-feed-design.md): the
// cron publishes file vaddrs (what static analysis produces); the runtime feeds
// each through ToRuntime() before hooking. Ports tools/xlate_vaddr.py, validated
// live (a published DepotKey RVA 0x118c1f0 translated to the exact accessor
// bytes in a running Steam).
namespace VaddrXlate {

struct Load {                      // a file-backed PT_LOAD segment
    uintptr_t p_offset;
    uintptr_t p_vaddr;
    uintptr_t p_filesz;
};
struct Mapping {                   // a steamclient.so line from /proc/*/maps
    uintptr_t start;
    uintptr_t end;
    uintptr_t offset;              // file offset of the mapping
};

// Pure, testable: file vaddr -> runtime address, or 0 if the vaddr is not inside
// a file-backed PT_LOAD whose file offset is covered by a steamclient.so mapping.
uintptr_t Translate(uintptr_t fileVaddr,
                    const std::vector<Load>& loads,
                    const std::vector<Mapping>& maps);

// Parse the ELF32 PT_LOAD segments of `steamclientPath` from disk. Returns false
// on I/O error or a non-ELFCLASS32 file.
bool ParseLoads(const char* steamclientPath, std::vector<Load>& out);

// Parse steamclient.so mappings from a /proc/*/maps text.
void ParseMaps(const std::string& mapsText, std::vector<Mapping>& out);

// Convenience: ParseLoads(path) + ParseMaps(/proc/self/maps), cached for
// ToRuntime(). Returns false if the module isn't mapped / can't be read.
bool Init(const char* steamclientPath);

// file vaddr -> runtime address using the Init()'d state, or 0.
uintptr_t ToRuntime(uintptr_t fileVaddr);

} // namespace VaddrXlate
