// analysis/cfg.h
// Control-flow graph construction for PowerPC code.
// The CFG is the backbone of the recompiler: every basic block becomes
// a labelled section in the generated C output; every function gets its
// own C function.

#pragma once
#include "instructions.h"
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <string>
#include <optional>

// -------------------------------------------------------------------------
// A single basic block: linear sequence of instructions ending in a
// branch, return, or call to an unknown location.
// -------------------------------------------------------------------------
struct BasicBlock {
    u32 start_addr;
    u32 end_addr;               // address of last instruction + 4
    std::vector<DecodedInstr> instructions;

    // Successor addresses (may be 0 if indirect)
    std::vector<u32> successors;
    // Predecessor addresses
    std::vector<u32> predecessors;

    bool is_function_entry = false;
    bool ends_with_blr = false;
    bool ends_with_indirect = false;   // bctr, bcctr, etc.
    bool ends_with_call = false;   // bl/bcl
    u32  call_target = 0;       // if ends_with_call
};

// -------------------------------------------------------------------------
// A function: a set of basic blocks discovered from one entry point.
// -------------------------------------------------------------------------
struct Function {
    u32 entry_addr;
    std::string name;                           // e.g. "fn_80003abc"
    std::map<u32, BasicBlock> blocks;           // keyed by start_addr

    bool is_leaf() const;  // no bl instructions
};

// -------------------------------------------------------------------------
// CFG builder
// -------------------------------------------------------------------------
class CFGBuilder {
public:
    // read_word: callback to read a 32-bit big-endian word from guest address.
    // Returns false/0 if the address is not mapped.
    using ReadWordFn = std::function<std::optional<u32>(u32 addr)>;
    using IsExecFn = std::function<bool(u32 addr)>;

    explicit CFGBuilder(ReadWordFn rw, IsExecFn ie)
        : m_read_word(std::move(rw)), m_is_exec(std::move(ie)) {
    }

    // Analyse a single function starting at entry_addr.
    // May recursively discover callees.
    Function analyse_function(u32 entry_addr);

    // Analyse all reachable functions starting from seeds.
    // Returns map: entry_addr → Function.
    std::map<u32, Function> analyse_all(const std::vector<u32>& seeds);

    // Total functions discovered so far
    const std::map<u32, Function>& functions() const { return m_functions; }

private:
    ReadWordFn  m_read_word;
    IsExecFn    m_is_exec;
    std::map<u32, Function> m_functions;
    std::set<u32>           m_queued;

    // Recursively build all basic blocks for a function.
    void build_blocks(Function& fn, u32 start);

    // Resolve block successors and fill predecessor lists.
    void link_blocks(Function& fn);
};
