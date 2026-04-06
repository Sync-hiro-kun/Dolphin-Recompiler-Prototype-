// codegen/c_backend.h
// C code generator.
// Translates each PowerPC instruction in a CFG to equivalent C statements
// that operate on a PPCState* named `s`.
//
// Design goals:
//   1. Correctness first – emit a semantically-correct C implementation for
//      every instruction, even at the cost of some verbosity.
//   2. Host-compiler friendly – the C output is fed into GCC/Clang which
//      will apply its own optimisations (inlining, constant folding, etc.).
//   3. Readable – the generated C includes the original PPC disassembly as
//      comments so developers can audit the translation.

#pragma once
#include "cfg.h"
#include <string>
#include <sstream>

// Configuration for the code generator
struct CGenConfig {
    bool emit_comments = true;   // embed PPC disasm as C comments
    bool emit_pc_updates = false;  // keep s->pc accurate (slower but safer)
    bool emit_cycle_count = false;  // increment s->cycle_count per instruction
    std::string runtime_header = "runtime/ppc_state.h";
};

class CBackend {
public:
    explicit CBackend(CGenConfig cfg = {}) : m_cfg(std::move(cfg)) {}

    // Generate a complete .c file containing all functions in the CFG.
    std::string emit_translation_unit(const std::map<u32, Function>& functions,
        const std::string& module_name);

    // Generate just one function (useful for incremental compilation)
    std::string emit_function(const Function& fn);

    // Generate a dispatch table header so the host can call functions by
    // guest address.
    std::string emit_dispatch_table(const std::map<u32, Function>& functions);

private:
    CGenConfig m_cfg;

    // Emit one basic block (label + instructions)
    void emit_block(std::ostringstream& out, const BasicBlock& bb,
        const Function& fn);

    // Emit one instruction.  Returns the C statement(s) as a string.
    std::string emit_instr(const DecodedInstr& d);

    // ---- per-group emitters ----
    std::string emit_integer(const DecodedInstr& d);
    std::string emit_branch(const DecodedInstr& d, u32 next_pc);
    std::string emit_loadstore(const DecodedInstr& d);
    std::string emit_float(const DecodedInstr& d);
    std::string emit_system(const DecodedInstr& d);
    std::string emit_ps(const DecodedInstr& d);

    static std::string block_label(u32 addr) {
        char buf[32]; snprintf(buf, sizeof(buf), "bb_%08x", addr); return buf;
    }
    static std::string fn_proto(const Function& fn) {
        return "void " + fn.name + "(PPCState* s)";
    }
};