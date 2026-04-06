// analysis/cfg.cpp

#include "cfg.h"
#include "instructions.h"
#include <cstdio>
#include <queue>
#include <algorithm>

// -------------------------------------------------------------------------
// Build all basic blocks for a function starting at `start`.
// Uses a worklist (BFS over block leaders).
// -------------------------------------------------------------------------
void CFGBuilder::build_blocks(Function& fn, u32 start) {
    std::queue<u32> worklist;
    auto enqueue = [&](u32 addr) {
        if (addr && m_is_exec(addr) && fn.blocks.find(addr) == fn.blocks.end()) {
            fn.blocks[addr] = BasicBlock{};
            fn.blocks[addr].start_addr = addr;
            worklist.push(addr);
        }
        };
    enqueue(start);
    fn.blocks[start].is_function_entry = true;

    while (!worklist.empty()) {
        u32 pc = worklist.front(); worklist.pop();
        BasicBlock& bb = fn.blocks[pc];
        bb.start_addr = pc;

        // Decode instructions until block terminator
        for (u32 addr = pc; ; addr += 4) {
            auto opt_word = m_read_word(addr);
            if (!opt_word || !m_is_exec(addr)) {
                // Ran off executable region – treat as implicit return
                bb.end_addr = addr;
                bb.ends_with_blr = true;
                break;
            }

            DecodedInstr di = ppc_decode(*opt_word, addr);
            bb.instructions.push_back(di);

            if (di.ends_block || di.is_branch) {
                bb.end_addr = addr + 4;

                if (di.is_branch && di.branch_link) {
                    // bl / bcl: call – the function continues after the call
                    bb.ends_with_call = true;
                    bb.call_target = di.branch_target;
                    // Queue callee for separate function analysis
                    if (di.branch_target != UINT32_MAX && m_is_exec(di.branch_target))
                        m_queued.insert(di.branch_target);
                    // Fall-through continues this basic block
                    if (!di.branch_conditional) {
                        // unconditional call (bl) → next instruction is fall-through
                        // Don't end the block here for bl – treat the call as a midblock event.
                        // Instead split the block after the bl.
                        enqueue(addr + 4);
                        bb.successors.push_back(addr + 4);
                        break;
                    }
                    else {
                        // Conditional call (bcl) – can also fall through
                        enqueue(addr + 4);
                        bb.successors.push_back(addr + 4);
                        break;
                    }
                }

                if (di.mnemonic == "blr" || di.mnemonic == "blrl") {
                    bb.ends_with_blr = true;
                    break;
                }

                if (di.is_branch && !di.branch_conditional) {
                    // Unconditional non-linking branch
                    if (di.branch_target != UINT32_MAX) {
                        bb.successors.push_back(di.branch_target);
                        enqueue(di.branch_target);
                    }
                    else {
                        bb.ends_with_indirect = true;
                    }
                    break;
                }

                if (di.is_branch && di.branch_conditional) {
                    // Conditional branch: taken and fall-through
                    if (di.branch_target != UINT32_MAX) {
                        bb.successors.push_back(di.branch_target);
                        enqueue(di.branch_target);
                    }
                    else {
                        bb.ends_with_indirect = true;
                    }
                    // fall-through
                    bb.successors.push_back(addr + 4);
                    enqueue(addr + 4);
                    break;
                }

                if (di.mnemonic == "rfi" || di.mnemonic == "sc") {
                    bb.ends_with_blr = true;  // treat as function terminator
                    break;
                }

                // Other ends_block instructions
                bb.ends_with_blr = true;
                break;
            }

            // Check if next instruction is already a known block leader
            // (e.g., target of another branch) – if so, end this block
            if (fn.blocks.find(addr + 4) != fn.blocks.end() &&
                addr + 4 != bb.start_addr) {
                bb.end_addr = addr + 4;
                bb.successors.push_back(addr + 4);
                break;
            }
        }
    }
}

// -------------------------------------------------------------------------
// Link predecessors
// -------------------------------------------------------------------------
void CFGBuilder::link_blocks(Function& fn) {
    for (auto& [addr, bb] : fn.blocks) {
        for (u32 succ : bb.successors) {
            auto it = fn.blocks.find(succ);
            if (it != fn.blocks.end()) {
                it->second.predecessors.push_back(addr);
            }
        }
    }
}

// -------------------------------------------------------------------------
// Analyse a single function
// -------------------------------------------------------------------------
Function CFGBuilder::analyse_function(u32 entry_addr) {
    Function fn;
    fn.entry_addr = entry_addr;
    fn.name = [entry_addr]() {
        char buf[32]; snprintf(buf, sizeof(buf), "fn_%08x", entry_addr); return std::string(buf);
        }();

    if (!m_is_exec(entry_addr)) return fn;

    build_blocks(fn, entry_addr);
    link_blocks(fn);

    fprintf(stderr, "[CFG] Function %s: %zu blocks\n", fn.name.c_str(), fn.blocks.size());
    return fn;
}

// -------------------------------------------------------------------------
// Analyse all reachable functions from a seed list
// -------------------------------------------------------------------------
std::map<u32, Function> CFGBuilder::analyse_all(const std::vector<u32>& seeds) {
    for (u32 s : seeds) m_queued.insert(s);

    while (!m_queued.empty()) {
        u32 entry = *m_queued.begin();
        m_queued.erase(m_queued.begin());

        if (m_functions.count(entry)) continue;

        Function fn = analyse_function(entry);
        // Collect any new callees discovered during block building
        for (auto& [addr, bb] : fn.blocks) {
            if (bb.ends_with_call && bb.call_target != UINT32_MAX
                && m_is_exec(bb.call_target)
                && !m_functions.count(bb.call_target))
                m_queued.insert(bb.call_target);
        }
        m_functions[entry] = std::move(fn);
    }

    fprintf(stderr, "[CFG] Total functions discovered: %zu\n", m_functions.size());
    return m_functions;
}

// -------------------------------------------------------------------------
// Function helpers
// -------------------------------------------------------------------------
bool Function::is_leaf() const {
    for (const auto& [addr, bb] : blocks)
        if (bb.ends_with_call) return false;
    return true;
}
