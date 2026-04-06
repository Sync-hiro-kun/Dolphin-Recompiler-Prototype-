// ppc/Instructions.cpp
// PowerPC instruction analysis utilities used by the recompiler passes.
// The core decode/disassemble logic lives in disasm.cpp; this file provides
// higher-level predicates and register-dependency queries consumed by the
// CFG builder and the C backend.

#include "Instructions.h"
#include <cstdio>
#include <cstring>

// -------------------------------------------------------------------------
// Instruction class name (for debug output)
// -------------------------------------------------------------------------
const char* ppc_iclass_name(PPCInstrClass c) {
    switch (c) {
    case PPCInstrClass::INTEGER:       return "INTEGER";
    case PPCInstrClass::FLOAT:         return "FLOAT";
    case PPCInstrClass::LOADSTORE:     return "LOADSTORE";
    case PPCInstrClass::BRANCH:        return "BRANCH";
    case PPCInstrClass::SYSTEM:        return "SYSTEM";
    case PPCInstrClass::PAIRED_SINGLE: return "PAIRED_SINGLE";
    default:                           return "UNKNOWN";
    }
}

// -------------------------------------------------------------------------
// Quick predicate: does this instruction always end the basic block?
// -------------------------------------------------------------------------
bool ppc_always_ends_block(const DecodedInstr& d) {
    return d.ends_block;
}

// -------------------------------------------------------------------------
// Quick predicate: is this a call (branch-with-link)?
// -------------------------------------------------------------------------
bool ppc_is_call(const DecodedInstr& d) {
    return d.is_branch && d.branch_link;
}

// -------------------------------------------------------------------------
// Quick predicate: is this an indirect branch (bctr, blr, etc.)?
// -------------------------------------------------------------------------
bool ppc_is_indirect_branch(const DecodedInstr& d) {
    return d.is_branch && d.branch_target == UINT32_MAX;
}

// -------------------------------------------------------------------------
// Quick predicate: is this a NOP?
// The canonical PPC NOP is `ori r0,r0,0` (opcode 24, RA=0, RS=0, UIMM=0).
// -------------------------------------------------------------------------
bool ppc_is_nop(const DecodedInstr& d) {
    const auto& I = d.raw;
    return I.OPCD == 24 && I.RA == 0 && I.RS == 0 && I.UIMM == 0;
}

// -------------------------------------------------------------------------
// Quick predicate: is this an unconditional simple return (blr BO=0x14)?
// -------------------------------------------------------------------------
bool ppc_is_simple_return(const DecodedInstr& d) {
    if (!d.is_branch || !d.ends_block) return false;
    const auto& I = d.raw;
    return I.OPCD == 19 && I.XO == 16 && (I.BO_2 & 0x14) == 0x14;
}

// -------------------------------------------------------------------------
// Does this instruction write CR0?
// -------------------------------------------------------------------------
bool ppc_affects_cr0(const DecodedInstr& d) {
    if (d.iclass != PPCInstrClass::INTEGER) return false;
    const auto& I = d.raw;
    u32 op = I.OPCD;
    if (op == 11 || op == 10)  return true;  // cmpi, cmpli
    if (op == 13)              return true;  // addic.
    if (op == 28 || op == 29) return true;  // andi., andis.
    if (op == 31 && I.Rc)     return true;
    if ((op == 20 || op == 21 || op == 23) && I.Rc_3) return true;
    return false;
}

// -------------------------------------------------------------------------
// Does this instruction write XER (carry / overflow)?
// -------------------------------------------------------------------------
bool ppc_affects_xer(const DecodedInstr& d) {
    if (d.iclass != PPCInstrClass::INTEGER) return false;
    const auto& I = d.raw;
    u32 op = I.OPCD;
    if (op == 8 || op == 12 || op == 13) return true;
    if (op == 31) {
        u32 xop = I.SUBOP10;
        switch (xop) {
        case 8: case 10: case 136: case 138: case 200: case 202: case 234:
        case 792: case 824:
            return true;
        default: break;
        }
        if (I.OE) return true;
    }
    return false;
}

// -------------------------------------------------------------------------
// Which GPRs does this instruction read?  Returns count (0-3).
// -------------------------------------------------------------------------
int ppc_gpr_reads(const DecodedInstr& d, u32 regs[3]) {
    const auto& I = d.raw;
    u32 op = I.OPCD;
    int n = 0;
    if (d.iclass == PPCInstrClass::BRANCH) return 0;

    // D-form loads/stores (op 32-47)
    if (op >= 32 && op <= 47) {
        if (I.RA != 0) regs[n++] = I.RA;
        if (op >= 36)  regs[n++] = I.RS;   // stores read RS
        return n;
    }
    // Integer immediate: read RA (except li/lis where RA==0 means no source)
    if (op == 7 || op == 8 || op == 12 || op == 13) { regs[n++] = I.RA; return n; }
    if (op == 14 || op == 15) { if (I.RA != 0) regs[n++] = I.RA; return n; }
    if (op == 10 || op == 11) { regs[n++] = I.RA; return n; }
    if (op == 24 || op == 25 || op == 26 || op == 27 || op == 28 || op == 29)
        { regs[n++] = I.RS; return n; }
    if (op == 20 || op == 21) { regs[n++] = I.RS; return n; }
    if (op == 23)             { regs[n++] = I.RS; regs[n++] = I.RB_2; return n; }

    if (op == 31) {
        u32 xop = I.SUBOP10;
        // One-source ops
        switch (xop) {
        case 104: case 200: case 202: case 234:
        case 26: case 922: case 954: case 792: case 824:
        case 19: case 339:   // mfcr, mfspr  – no GPR input (SPR/CR are separate)
            regs[n++] = I.RA; return n;
        case 467: regs[n++] = I.RS; return n;  // mtspr
        case 144: regs[n++] = I.RS; return n;  // mtcrf
        }
        // Two-source ops
        regs[n++] = I.RA;
        regs[n++] = I.RB;
        return n;
    }
    return n;
}

// -------------------------------------------------------------------------
// Which GPR does this instruction write?  Returns reg index or -1.
// -------------------------------------------------------------------------
int ppc_gpr_write(const DecodedInstr& d) {
    const auto& I = d.raw;
    u32 op = I.OPCD;
    if (d.iclass == PPCInstrClass::BRANCH) return -1;
    if (op == 7 || op == 8 || op == 12 || op == 13 || op == 14 || op == 15)
        return (int)I.RD;
    if (op == 10 || op == 11) return -1;                      // compare, no GPR write
    if (op == 24 || op == 25 || op == 26 || op == 27 ||
        op == 28 || op == 29 || op == 20 || op == 21 || op == 23)
        return (int)I.RA;
    if (op >= 32 && op <= 35) return (int)I.RD;               // lwz/lbz loads
    if (op == 40 || op == 41 || op == 42 || op == 43 || op == 46) return (int)I.RD;
    if (op == 36 || op == 37 || op == 38 || op == 39 ||
        op == 44 || op == 45 || op == 47) return -1;          // stores
    if (op == 31) {
        u32 xop = I.SUBOP10;
        if (xop == 144 || xop == 146 || xop == 467 || xop == 0 || xop == 32 ||
            xop == 4 || xop == 54 || xop == 86 || xop == 150 || xop == 151 ||
            xop == 183 || xop == 215 || xop == 247 || xop == 407 || xop == 439 ||
            xop == 598 || xop == 854 || xop == 982 || xop == 1014 ||
            xop == 662 || xop == 918 || xop == 597 || xop == 661 || xop == 725 ||
            xop == 663 || xop == 695 || xop == 727 || xop == 759 ||
            xop == 470 || xop == 306 || xop == 566)
            return -1;
        return (int)I.RD;
    }
    return -1;
}

// -------------------------------------------------------------------------
// Format a one-line operand summary string for debug output.
// -------------------------------------------------------------------------
std::string ppc_operand_summary(const DecodedInstr& d) {
    char buf[128];
    int wr = ppc_gpr_write(d);
    u32 rd[3] = {};
    int nr = ppc_gpr_reads(d, rd);
    if      (wr >= 0 && nr >= 2) snprintf(buf, sizeof(buf), "r%d <- r%u, r%u", wr, rd[0], rd[1]);
    else if (wr >= 0 && nr == 1) snprintf(buf, sizeof(buf), "r%d <- r%u", wr, rd[0]);
    else if (wr >= 0 && nr == 0) snprintf(buf, sizeof(buf), "r%d <- imm", wr);
    else if (wr < 0  && nr >= 1) snprintf(buf, sizeof(buf), "r%u -> mem/spr", rd[0]);
    else                         snprintf(buf, sizeof(buf), "(other)");
    return buf;
}
