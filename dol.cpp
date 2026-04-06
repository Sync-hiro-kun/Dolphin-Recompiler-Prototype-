// loader/dol.cpp
#define _CRT_SECURE_NO_WARNINGS
#include "dol.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// Byte-swap helpers (DOL is big-endian)
static u32 bswap32(u32 v) {
    return ((v & 0xFF000000u) >> 24) |
        ((v & 0x00FF0000u) >> 8) |
        ((v & 0x0000FF00u) << 8) |
        ((v & 0x000000FFu) << 24);
}

static void swap_header(DolHeader& h) {
    for (auto& v : h.text_offsets) v = bswap32(v);
    for (auto& v : h.data_offsets) v = bswap32(v);
    for (auto& v : h.text_addrs)   v = bswap32(v);
    for (auto& v : h.data_addrs)   v = bswap32(v);
    for (auto& v : h.text_sizes)   v = bswap32(v);
    for (auto& v : h.data_sizes)   v = bswap32(v);
    h.bss_addr = bswap32(h.bss_addr);
    h.bss_size = bswap32(h.bss_size);
    h.entry_point = bswap32(h.entry_point);
}

std::optional<DolImage> load_dol(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[DOL] Cannot open '%s'\n", path.c_str());
        return std::nullopt;
    }

    // Read the full file
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<u8> file_data(file_size);
    if (fread(file_data.data(), 1, file_size, f) != (size_t)file_size) {
        fclose(f);
        fprintf(stderr, "[DOL] Read error\n");
        return std::nullopt;
    }
    fclose(f);

    if (file_size < (long)sizeof(DolHeader)) {
        fprintf(stderr, "[DOL] File too small to be a DOL\n");
        return std::nullopt;
    }

    DolHeader hdr;
    memcpy(&hdr, file_data.data(), sizeof(hdr));
    swap_header(hdr);

    DolImage img;
    img.path = path;
    img.entry_point = hdr.entry_point;
    img.bss_addr = hdr.bss_addr;
    img.bss_size = hdr.bss_size;

    // Load text sections
    for (int i = 0; i < 7; i++) {
        if (hdr.text_sizes[i] == 0) continue;
        DolSection sec;
        sec.name = ".text" + std::to_string(i);
        sec.load_addr = hdr.text_addrs[i];
        sec.size = hdr.text_sizes[i];
        u32 off = hdr.text_offsets[i];
        if (off + sec.size > (u32)file_size) {
            fprintf(stderr, "[DOL] Text section %d out of bounds\n", i);
            return std::nullopt;
        }
        sec.data.assign(file_data.begin() + off, file_data.begin() + off + sec.size);
        img.sections.push_back(std::move(sec));
    }

    // Load data sections
    for (int i = 0; i < 11; i++) {
        if (hdr.data_sizes[i] == 0) continue;
        DolSection sec;
        sec.name = ".data" + std::to_string(i);
        sec.load_addr = hdr.data_addrs[i];
        sec.size = hdr.data_sizes[i];
        u32 off = hdr.data_offsets[i];
        if (off + sec.size > (u32)file_size) {
            fprintf(stderr, "[DOL] Data section %d out of bounds\n", i);
            return std::nullopt;
        }
        sec.data.assign(file_data.begin() + off, file_data.begin() + off + sec.size);
        img.sections.push_back(std::move(sec));
    }

    fprintf(stderr, "[DOL] Loaded '%s': entry=0x%08x, %zu sections, bss=0x%08x+0x%x\n",
        path.c_str(), img.entry_point, img.sections.size(), img.bss_addr, img.bss_size);
    return img;
}

bool DolImage::is_executable(u32 addr) const {
    // Text sections are the first 7 (at most)
    int count = 0;
    for (const auto& s : sections) {
        if (s.name.find(".text") != std::string::npos && s.contains(addr)) return true;
        count++;
        if (count >= 7) break;
    }
    return false;
}

std::vector<const DolSection*> DolImage::text_sections() const {
    std::vector<const DolSection*> result;
    for (const auto& s : sections)
        if (s.name.find(".text") != std::string::npos)
            result.push_back(&s);
    return result;
}