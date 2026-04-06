#pragma once
// loader/dol.h
// GameCube/Wii DOL executable loader.
// The DOL format is the primary executable container used by GC/Wii games.
// It holds up to 7 text (code) sections and 11 data sections.
#pragma once
#include "instructions.h"
#include <string>
#include <vector>
#include <optional>

// -------------------------------------------------------------------------
// DOL header (on-disk layout, big-endian)
// -------------------------------------------------------------------------
#pragma pack(push, 1)
struct DolHeader {
    u32 text_offsets[7];    // file offsets to text sections
    u32 data_offsets[11];   // file offsets to data sections
    u32 text_addrs[7];      // load addresses for text sections
    u32 data_addrs[11];     // load addresses for data sections
    u32 text_sizes[7];      // sizes of text sections
    u32 data_sizes[11];     // sizes of data sections
    u32 bss_addr;           // BSS address
    u32 bss_size;           // BSS size
    u32 entry_point;        // execution entry point
    u32 padding[7];
};
#pragma pack(pop)
static_assert(sizeof(DolHeader) == 0x100, "DOL header must be 0x100 bytes");

// -------------------------------------------------------------------------
// A loaded section (both text and data)
// -------------------------------------------------------------------------
struct DolSection {
    std::string  name;
    u32          load_addr;    // guest virtual address
    u32          size;
    std::vector<u8> data;      // host copy of section bytes

    bool contains(u32 addr) const {
        return addr >= load_addr && addr < load_addr + size;
    }
    const u8* host_ptr(u32 addr) const {
        return data.data() + (addr - load_addr);
    }
    u32 read_u32(u32 addr) const {  // big-endian
        const u8* p = host_ptr(addr);
        return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
    }
};

// -------------------------------------------------------------------------
// Loaded DOL image
// -------------------------------------------------------------------------
struct DolImage {
    std::string           path;
    u32                   entry_point;
    u32                   bss_addr;
    u32                   bss_size;
    std::vector<DolSection> sections;    // text first, then data

    // Find section containing guest address
    const DolSection* find_section(u32 addr) const {
        for (const auto& s : sections)
            if (s.contains(addr)) return &s;
        return nullptr;
    }

    // Read a 32-bit big-endian word from anywhere in the image
    std::optional<u32> read_u32(u32 addr) const {
        if (auto* s = find_section(addr))
            return s->read_u32(addr);
        return std::nullopt;
    }

    // True if the address is in an executable (text) section
    bool is_executable(u32 addr) const;

    // All text sections
    std::vector<const DolSection*> text_sections() const;
};

// Load a DOL file from disk.  Returns nullopt on error.
std::optional<DolImage> load_dol(const std::string& path);
