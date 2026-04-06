#pragma once
// loader / elf.h
// ELF32 big-endian loader for PowerPC.
// Supports standard ELF executables and relocatable objects (.o) used by
// GC/Wii homebrew and SDK tools.

#pragma once
#include "instructions.h"
#include <string>
#include <vector>
#include <optional>

// ELF32 constants
static constexpr u16 EM_PPC = 20;
static constexpr u8  ELFMAG0 = 0x7f;
static constexpr u8  ET_EXEC = 2;
static constexpr u8  ET_DYN = 3;
static constexpr u8  PT_LOAD = 1;
static constexpr u8  PF_X = 1;   // segment executable flag
static constexpr u8  SHF_EXECINSTR = 4;

#pragma pack(push,1)
struct Elf32_Ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u32 e_entry;
    u32 e_phoff;
    u32 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};
struct Elf32_Phdr {
    u32 p_type;
    u32 p_offset;
    u32 p_vaddr;
    u32 p_paddr;
    u32 p_filesz;
    u32 p_memsz;
    u32 p_flags;
    u32 p_align;
};
struct Elf32_Shdr {
    u32 sh_name;
    u32 sh_type;
    u32 sh_flags;
    u32 sh_addr;
    u32 sh_offset;
    u32 sh_size;
    u32 sh_link;
    u32 sh_info;
    u32 sh_addralign;
    u32 sh_entsize;
};
#pragma pack(pop)

struct ElfSection {
    std::string  name;
    u32          load_addr;
    u32          size;
    bool         executable;
    std::vector<u8> data;

    bool contains(u32 addr) const { return addr >= load_addr && addr < load_addr + size; }
    u32  read_u32(u32 addr) const {
        const u8* p = data.data() + (addr - load_addr);
        return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
    }
};

struct ElfImage {
    std::string  path;
    u32          entry_point;
    std::vector<ElfSection> sections;

    const ElfSection* find_section(u32 addr) const {
        for (const auto& s : sections)
            if (s.contains(addr)) return &s;
        return nullptr;
    }
    bool is_executable(u32 addr) const {
        const ElfSection* s = find_section(addr);
        return s && s->executable;
    }
    std::optional<u32> read_u32(u32 addr) const {
        if (auto* s = find_section(addr)) return s->read_u32(addr);
        return std::nullopt;
    }
    std::vector<const ElfSection*> text_sections() const {
        std::vector<const ElfSection*> r;
        for (const auto& s : sections)
            if (s.executable) r.push_back(&s);
        return r;
    }
};

std::optional<ElfImage> load_elf(const std::string& path);