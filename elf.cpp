// loader/elf.cpp
#define _CRT_SECURE_NO_WARNINGS
#include "elf.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static u16 bs16(u16 v) { return (v >> 8) | (v << 8); }
static u32 bs32(u32 v) {
    return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) |
        ((v & 0x0000FF00u) << 8) | ((v & 0x000000FFu) << 24);
}

static void swap_ehdr(Elf32_Ehdr& h) {
    h.e_type = bs16(h.e_type);
    h.e_machine = bs16(h.e_machine);
    h.e_version = bs32(h.e_version);
    h.e_entry = bs32(h.e_entry);
    h.e_phoff = bs32(h.e_phoff);
    h.e_shoff = bs32(h.e_shoff);
    h.e_phnum = bs16(h.e_phnum);
    h.e_shnum = bs16(h.e_shnum);
    h.e_shstrndx = bs16(h.e_shstrndx);
    h.e_phentsize = bs16(h.e_phentsize);
    h.e_shentsize = bs16(h.e_shentsize);
}
static void swap_phdr(Elf32_Phdr& p) {
    p.p_type = bs32(p.p_type);
    p.p_offset = bs32(p.p_offset);
    p.p_vaddr = bs32(p.p_vaddr);
    p.p_filesz = bs32(p.p_filesz);
    p.p_memsz = bs32(p.p_memsz);
    p.p_flags = bs32(p.p_flags);
}
static void swap_shdr(Elf32_Shdr& s) {
    s.sh_name = bs32(s.sh_name);
    s.sh_type = bs32(s.sh_type);
    s.sh_flags = bs32(s.sh_flags);
    s.sh_addr = bs32(s.sh_addr);
    s.sh_offset = bs32(s.sh_offset);
    s.sh_size = bs32(s.sh_size);
}

std::optional<ElfImage> load_elf(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "[ELF] Cannot open '%s'\n", path.c_str()); return std::nullopt; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<u8> data(fsz);
    if (fread(data.data(), 1, fsz, f) != (size_t)fsz) { fclose(f); return std::nullopt; }
    fclose(f);

    if (fsz < (long)sizeof(Elf32_Ehdr)) { fprintf(stderr, "[ELF] Too small\n"); return std::nullopt; }
    // Check magic
    if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        fprintf(stderr, "[ELF] Bad magic\n"); return std::nullopt;
    }
    // Must be big-endian (data[5]==2) and 32-bit (data[4]==1)
    if (data[4] != 1 || data[5] != 2) {
        fprintf(stderr, "[ELF] Must be 32-bit big-endian ELF\n"); return std::nullopt;
    }

    Elf32_Ehdr ehdr;
    memcpy(&ehdr, data.data(), sizeof(ehdr));
    swap_ehdr(ehdr);

    if (ehdr.e_machine != EM_PPC) {
        fprintf(stderr, "[ELF] Not a PPC ELF (machine=0x%x)\n", ehdr.e_machine);
        return std::nullopt;
    }

    ElfImage img;
    img.path = path;
    img.entry_point = ehdr.e_entry;

    // Try program headers first (for executable ELFs)
    if (ehdr.e_phnum > 0 && ehdr.e_phoff > 0) {
        for (int i = 0; i < ehdr.e_phnum; i++) {
            Elf32_Phdr ph;
            memcpy(&ph, data.data() + ehdr.e_phoff + i * ehdr.e_phentsize, sizeof(ph));
            swap_phdr(ph);
            if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;

            ElfSection sec;
            sec.name = "seg" + std::to_string(i);
            sec.load_addr = ph.p_vaddr;
            sec.size = ph.p_memsz;
            sec.executable = (ph.p_flags & PF_X) != 0;
            sec.data.resize(ph.p_memsz, 0);
            if (ph.p_filesz > 0)
                memcpy(sec.data.data(), data.data() + ph.p_offset,
                    std::min((size_t)ph.p_filesz, (size_t)ph.p_memsz));
            img.sections.push_back(std::move(sec));
        }
    }

    // Fall back to section headers if no program headers
    if (img.sections.empty() && ehdr.e_shnum > 0 && ehdr.e_shoff > 0) {
        // Read section name string table
        std::string shstrtab;
        if (ehdr.e_shstrndx < ehdr.e_shnum) {
            Elf32_Shdr sh;
            memcpy(&sh, data.data() + ehdr.e_shoff + ehdr.e_shstrndx * ehdr.e_shentsize, sizeof(sh));
            swap_shdr(sh);
            if (sh.sh_offset + sh.sh_size <= (u32)fsz)
                shstrtab.assign((const char*)data.data() + sh.sh_offset, sh.sh_size);
        }
        for (int i = 0; i < ehdr.e_shnum; i++) {
            Elf32_Shdr sh;
            memcpy(&sh, data.data() + ehdr.e_shoff + i * ehdr.e_shentsize, sizeof(sh));
            swap_shdr(sh);
            if (sh.sh_addr == 0 || sh.sh_size == 0) continue;
            ElfSection sec;
            if (sh.sh_name < shstrtab.size())
                sec.name = shstrtab.c_str() + sh.sh_name;
            else
                sec.name = "sec" + std::to_string(i);
            sec.load_addr = sh.sh_addr;
            sec.size = sh.sh_size;
            sec.executable = (sh.sh_flags & SHF_EXECINSTR) != 0;
            sec.data.resize(sh.sh_size, 0);
            if (sh.sh_offset > 0)
                memcpy(sec.data.data(), data.data() + sh.sh_offset,
                    std::min((size_t)sh.sh_size, (size_t)(fsz - sh.sh_offset)));
            img.sections.push_back(std::move(sec));
        }
    }

    fprintf(stderr, "[ELF] Loaded '%s': entry=0x%08x, %zu sections\n",
        path.c_str(), img.entry_point, img.sections.size());
    return img;
}