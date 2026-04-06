#include "dol.h"
#include "Elf.h"
#include "cfg.h"
#include "c_backend.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <algorithm>

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
static void usage(const char* prog) {
    fprintf(stderr,
        "Dolphin PPC-to-C Static Recompiler\n"
        "Usage: %s <input.dol|input.elf> [options]\n"
        "\n"
        "Options:\n"
        "  -o <output.c>     Output C file (default: output.c)\n"
        "  -entry <addr>     Additional entry point (hex, e.g. 0x80003100)\n"
        "  -wii              Wii mode (64 MB MEM2)\n"
        "  -no-comments      Suppress PPC disassembly comments in output\n"
        "  -pc-updates       Keep s->pc updated every instruction\n"
        "  -cycle-count      Increment s->cycle_count every instruction\n"
        "  -dispatch         Write dispatch table to <output>.dispatch.h\n"
        "  -h / --help       Print this help\n",
        prog);
}

static bool ends_with(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string strip_extension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return path;
    return path.substr(0, dot);
}

// -------------------------------------------------------------------------
// File writer
// -------------------------------------------------------------------------
static bool write_file(const std::string& path, const std::string& content) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "[MAIN] Cannot open '%s' for writing\n", path.c_str());
        return false;
    }
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
    return true;
}

// -------------------------------------------------------------------------
// Print summary statistics
// -------------------------------------------------------------------------
static void print_stats(const std::map<u32, Function>& functions) {
    size_t total_blocks = 0, total_instrs = 0;
    size_t leaf_fns = 0;
    for (const auto& [addr, fn] : functions) {
        total_blocks += fn.blocks.size();
        for (const auto& [ba, bb] : fn.blocks)
            total_instrs += bb.instructions.size();
        if (fn.is_leaf()) leaf_fns++;
    }
    fprintf(stderr,
        "[STATS] Functions  : %zu  (%zu leaf)\n"
        "[STATS] Blocks     : %zu\n"
        "[STATS] Instructions: %zu\n",
        functions.size(), leaf_fns,
        total_blocks,
        total_instrs);
}

// -------------------------------------------------------------------------
// main
// -------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    // ---- Parse arguments ----
    std::string input_path;
    std::string output_path;          // derived from input if not set
    std::vector<u32> extra_entries;
    bool emit_dispatch = false;
    bool is_wii = false;
    CGenConfig cfg;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (arg == "-o") {
            if (i + 1 >= argc) { fprintf(stderr, "-o requires argument\n"); return 1; }
            output_path = argv[++i];
        }
        else if (arg == "-entry") {
            if (i + 1 >= argc) { fprintf(stderr, "-entry requires argument\n"); return 1; }
            char* endp = nullptr;
            u32 addr = (u32)strtoul(argv[++i], &endp, 16);
            extra_entries.push_back(addr);
        }
        else if (arg == "-wii") { is_wii = true; }
        else if (arg == "-no-comments") { cfg.emit_comments = false; }
        else if (arg == "-pc-updates") { cfg.emit_pc_updates = true; }
        else if (arg == "-cycle-count") { cfg.emit_cycle_count = true; }
        else if (arg == "-dispatch") { emit_dispatch = true; }
        else if (arg[0] != '-') {
            if (!input_path.empty()) {
                fprintf(stderr, "Multiple input files specified.\n");
                return 1;
            }
            input_path = arg;
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            usage(argv[0]);
            return 1;
        }
    }

    if (input_path.empty()) {
        fprintf(stderr, "No input file specified.\n");
        usage(argv[0]);
        return 1;
    }

    // Derive output path
    if (output_path.empty())
        output_path = strip_extension(input_path) + "_recomp.c";

    // ---- Detect file type ----
    bool is_elf = ends_with(input_path, ".elf") ||
        ends_with(input_path, ".ELF") ||
        ends_with(input_path, ".o");

    // ---- Load the binary ----
    std::function<std::optional<u32>(u32)> read_word;
    std::function<bool(u32)>               is_exec;
    u32 entry_point = 0;

    std::optional<DolImage> dol_img;
    std::optional<ElfImage> elf_img;

    if (is_elf) {
        elf_img = load_elf(input_path);
        if (!elf_img) {
            fprintf(stderr, "[MAIN] Failed to load ELF '%s'\n", input_path.c_str());
            return 1;
        }
        entry_point = elf_img->entry_point;
        read_word = [&](u32 addr) { return elf_img->read_u32(addr); };
        is_exec = [&](u32 addr) { return elf_img->is_executable(addr); };
    }
    else {
        dol_img = load_dol(input_path);
        if (!dol_img) {
            fprintf(stderr, "[MAIN] Failed to load DOL '%s'\n", input_path.c_str());
            return 1;
        }
        entry_point = dol_img->entry_point;
        read_word = [&](u32 addr) { return dol_img->read_u32(addr); };
        is_exec = [&](u32 addr) { return dol_img->is_executable(addr); };
    }

    fprintf(stderr, "[MAIN] File     : %s (%s)\n",
        input_path.c_str(), is_elf ? "ELF" : "DOL");
    fprintf(stderr, "[MAIN] Entry    : 0x%08x\n", entry_point);
    fprintf(stderr, "[MAIN] Mode     : %s\n", is_wii ? "Wii" : "GameCube");
    if (!extra_entries.empty()) {
        fprintf(stderr, "[MAIN] Extra entries:");
        for (u32 e : extra_entries) fprintf(stderr, " 0x%08x", e);
        fprintf(stderr, "\n");
    }

    // ---- Build CFG ----
    std::vector<u32> seeds = { entry_point };
    for (u32 e : extra_entries) seeds.push_back(e);

    CFGBuilder builder(read_word, is_exec);
    auto functions = builder.analyse_all(seeds);

    if (functions.empty()) {
        fprintf(stderr, "[MAIN] No functions discovered — check that the entry "
            "point lies in an executable section.\n");
        return 1;
    }

    print_stats(functions);

    // ---- Emit C ----
    cfg.runtime_header = "ppc_state.h";
    CBackend backend(cfg);

    std::string module_name = input_path;
    std::string tu = backend.emit_translation_unit(functions, module_name);

    if (!write_file(output_path, tu)) return 1;
    fprintf(stderr, "[MAIN] Wrote %s  (%zu bytes)\n",
        output_path.c_str(), tu.size());

    // ---- Optionally emit dispatch table header ----
    if (emit_dispatch) {
        std::string disp_path = strip_extension(output_path) + ".dispatch.h";
        std::string disp = backend.emit_dispatch_table(functions);
        if (!write_file(disp_path, disp)) return 1;
        fprintf(stderr, "[MAIN] Wrote %s  (%zu bytes)\n",
            disp_path.c_str(), disp.size());
    }

    fprintf(stderr, "[MAIN] Done.\n");
    return 0;
}