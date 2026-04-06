// ppc/disasm.cpp
// PowerPC Gekko/Broadway instruction decoder & disassembler.
// Covers all instruction groups needed by GameCube / Wii software.
#include "Instructions.h"
#include <cstdio>
#include <cstring>

// -------------------------------------------------------------------------
// Forward helpers
// -------------------------------------------------------------------------
static const char* gpr_name(u32 r) {
    static const char* n[32] = {
"r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","r12","r13",
"r14","r15","r16","r17","r18","r19","r20","r21","r22","r23","r24","r25",
"r26","r27","r28","r29","r30","r31" }; return n[r & 31];
}
static const char* fpr_name(u32 r) {
    static const char* n[32] = {
"f0","f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12","f13",
"f14","f15","f16","f17","f18","f19","f20","f21","f22","f23","f24","f25",
"f26","f27","f28","f29","f30","f31" }; return n[r & 31];
}
static const char* cr_name(u32 cr) {
    static const char* n[8] = {
"cr0","cr1","cr2","cr3","cr4","cr5","cr6","cr7" }; return n[cr & 7];
}

static std::string hex(u32 v) {
    char buf[12]; snprintf(buf, sizeof(buf), "0x%x", v); return buf;
}

// -------------------------------------------------------------------------
// Primary decoder
// -------------------------------------------------------------------------
DecodedInstr ppc_decode(u32 word, u32 address) {
    DecodedInstr d{};
    d.raw = UGeckoInstruction(word);
    d.address = address;
    d.branch_target = UINT32_MAX;

    auto& I = d.raw;
    u32 op = I.OPCD;

    auto set_branch = [&](bool cond, bool link, bool abs, u32 tgt) {
        d.is_branch = true;
        d.branch_conditional = cond;
        d.branch_link = link;
        d.branch_absolute = abs;
        d.branch_target = tgt;
        d.iclass = PPCInstrClass::BRANCH;
        if (!cond) d.ends_block = true;
        };

    switch (op) {
        // ---- Branches ------------------------------------------------
    case 18: { // bx (unconditional branch)
        s32 off = I.LIOffset();
        u32 tgt = I.AA ? (u32)off : address + (u32)off;
        set_branch(false, I.LK, I.AA, tgt);
        d.mnemonic = I.LK ? "bl" : "b";
        break;
    }
    case 16: { // bcx (conditional branch)
        s32 off = I.BranchOffset();
        u32 tgt = I.AA_2 ? (u32)off : address + (u32)off;
        set_branch(true, I.LK_2, I.AA_2, tgt);
        d.mnemonic = I.LK_2 ? "bcl" : "bc";
        break;
    }
    case 19: { // extended table 19
        u32 xop = I.XO;
        if (xop == 16) { // bclrx
            d.iclass = PPCInstrClass::BRANCH;
            d.is_branch = true; d.branch_link = I.LK_3;
            d.branch_conditional = true;
            d.ends_block = ((I.BO_2 & 0x14) == 0x14); // unconditional BO
            d.mnemonic = I.LK_3 ? "blrl" : "blr";
        }
        else if (xop == 528) { // bcctrx
            d.iclass = PPCInstrClass::BRANCH;
            d.is_branch = true; d.branch_link = I.LK_3;
            d.branch_conditional = true;
            d.ends_block = ((I.BO_2 & 0x14) == 0x14);
            d.mnemonic = I.LK_3 ? "bcctrl" : "bcctr";
        }
        else if (xop == 50) { // rfi
            d.iclass = PPCInstrClass::SYSTEM;
            d.is_branch = true; d.ends_block = true;
            d.mnemonic = "rfi";
        }
        else if (xop == 150) { // isync
            d.iclass = PPCInstrClass::SYSTEM;
            d.mnemonic = "isync";
        }
        else if (xop == 0) { // mcrf
            d.iclass = PPCInstrClass::INTEGER;
            d.mnemonic = "mcrf";
        }
        else if (xop == 33) { // crnor
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "crnor";
        }
        else if (xop == 129) { // crandc
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "crandc";
        }
        else if (xop == 193) { // crxor
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "crxor";
        }
        else if (xop == 225) { // crnand
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "crnand";
        }
        else if (xop == 257) { // crand
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "crand";
        }
        else if (xop == 289) { // creqv
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "creqv";
        }
        else if (xop == 417) { // crorc
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "crorc";
        }
        else if (xop == 449) { // cror
            d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "cror";
        }
        else {
            d.iclass = PPCInstrClass::UNKNOWN;
            d.mnemonic = "?19";
        }
        break;
    }
           // ---- System --------------------------------------------------
    case 17: d.iclass = PPCInstrClass::SYSTEM; d.mnemonic = "sc";
        d.is_branch = true; d.ends_block = true; break;
        // ---- Trap ----------------------------------------------------
    case 3:  d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "twi"; break;
        // ---- Integer immediate ---------------------------------------
    case 7:  d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "mulli";  break;
    case 8:  d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "subfic"; break;
    case 10: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "cmpli";  break;
    case 11: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "cmpi";   break;
    case 12: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "addic";  break;
    case 13: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "addic."; break;
    case 14: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "addi";   break;
    case 15: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "addis";  break;
    case 24: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "ori";    break;
    case 25: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "oris";   break;
    case 26: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "xori";   break;
    case 27: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "xoris";  break;
    case 28: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "andi.";  break;
    case 29: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = "andis."; break;
        // ---- Rotate/shift immediate ----------------------------------
    case 20: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = I.Rc_3 ? "rlwimi." : "rlwimi"; break;
    case 21: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = I.Rc_3 ? "rlwinm." : "rlwinm"; break;
    case 23: d.iclass = PPCInstrClass::INTEGER; d.mnemonic = I.Rc_3 ? "rlwnm." : "rlwnm";  break;
        // ---- Load/Store integer --------------------------------------
    case 32: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lwz";   break;
    case 33: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lwzu";  break;
    case 34: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lbz";   break;
    case 35: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lbzu";  break;
    case 36: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stw";   break;
    case 37: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stwu";  break;
    case 38: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stb";   break;
    case 39: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stbu";  break;
    case 40: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhz";   break;
    case 41: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhzu";  break;
    case 42: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lha";   break;
    case 43: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhau";  break;
    case 44: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "sth";   break;
    case 45: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "sthu";  break;
    case 46: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lmw";   break;
    case 47: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stmw";  break;
        // ---- FP load/store -------------------------------------------
    case 48: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfs";   break;
    case 49: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfsu";  break;
    case 50: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfd";   break;
    case 51: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfdu";  break;
    case 52: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfs";  break;
    case 53: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfsu"; break;
    case 54: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfd";  break;
    case 55: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfdu"; break;
        // ---- Paired-single -------------------------------------------
    case 56: d.iclass = PPCInstrClass::PAIRED_SINGLE; d.mnemonic = "psq_l";   break;
    case 57: d.iclass = PPCInstrClass::PAIRED_SINGLE; d.mnemonic = "psq_lu";  break;
    case 60: d.iclass = PPCInstrClass::PAIRED_SINGLE; d.mnemonic = "psq_st";  break;
    case 61: d.iclass = PPCInstrClass::PAIRED_SINGLE; d.mnemonic = "psq_stu"; break;
        // ---- Integer register-register (table 31) --------------------
    case 31: {
        u32 xop = I.SUBOP10;
        d.iclass = PPCInstrClass::INTEGER;
        switch (xop) {
        case 0:   d.mnemonic = "cmp";    break;
        case 4:   d.mnemonic = "tw";     break;
        case 8:   d.mnemonic = I.OE ? (I.Rc ? "subfc.o" : "subfco") : (I.Rc ? "subfc." : "subfc"); break;
        case 10:  d.mnemonic = I.OE ? (I.Rc ? "addc.o" : "addco") : (I.Rc ? "addc." : "addc"); break;
        case 11:  d.mnemonic = "mulhwu"; break;
        case 19:  d.mnemonic = "mfcr";  break;
        case 20:  d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lwarx";  break;
        case 23:  d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lwzx";   break;
        case 24:  d.mnemonic = I.Rc ? "slw." : "slw"; break;
        case 26:  d.mnemonic = I.Rc ? "cntlzw." : "cntlzw"; break;
        case 28:  d.mnemonic = I.Rc ? "and." : "and"; break;
        case 32:  d.mnemonic = "cmpl";   break;
        case 40:  d.mnemonic = I.OE ? (I.Rc ? "subf.o" : "subfo") : (I.Rc ? "subf." : "subf"); break;
        case 54:  d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "dcbst";  break;
        case 55:  d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lwzux";  break;
        case 60:  d.mnemonic = I.Rc ? "andc." : "andc"; break;
        case 75:  d.mnemonic = "mulhw";  break;
        case 83:  d.mnemonic = "mfmsr";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 86:  d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "dcbf";   break;
        case 87:  d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lbzx";   break;
        case 104: d.mnemonic = I.OE ? (I.Rc ? "neg.o" : "nego") : (I.Rc ? "neg." : "neg"); break;
        case 119: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lbzux";  break;
        case 124: d.mnemonic = I.Rc ? "nor." : "nor"; break;
        case 136: d.mnemonic = I.OE ? (I.Rc ? "subfe.o" : "subfeo") : (I.Rc ? "subfe." : "subfe"); break;
        case 138: d.mnemonic = I.OE ? (I.Rc ? "adde.o" : "addeo") : (I.Rc ? "adde." : "adde"); break;
        case 144: d.mnemonic = "mtcrf";  break;
        case 146: d.mnemonic = "mtmsr";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 150: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stwcx."; break;
        case 151: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stwx";   break;
        case 183: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stwux";  break;
        case 200: d.mnemonic = I.OE ? (I.Rc ? "subfze.o" : "subfzeo") : (I.Rc ? "subfze." : "subfze"); break;
        case 202: d.mnemonic = I.OE ? (I.Rc ? "addze.o" : "addzeo") : (I.Rc ? "addze." : "addze"); break;
        case 215: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stbx";   break;
        case 234: d.mnemonic = I.OE ? (I.Rc ? "addme.o" : "addmeo") : (I.Rc ? "addme." : "addme"); break;
        case 235: d.mnemonic = I.OE ? (I.Rc ? "mullw.o" : "mullwo") : (I.Rc ? "mullw." : "mullw"); break;
        case 247: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stbux";  break;
        case 266: d.mnemonic = I.OE ? (I.Rc ? "add.o" : "addo") : (I.Rc ? "add." : "add"); break;
        case 279: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhzx";   break;
        case 284: d.mnemonic = I.Rc ? "eqv." : "eqv"; break;
        case 306: d.mnemonic = "tlbie";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 310: d.mnemonic = "eciwx";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 311: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhzux";  break;
        case 316: d.mnemonic = I.Rc ? "xor." : "xor"; break;
        case 339: d.mnemonic = "mfspr";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 343: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhax";   break;
        case 371: d.mnemonic = "mftb";   d.iclass = PPCInstrClass::SYSTEM; break;
        case 375: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhaux";  break;
        case 407: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "sthx";   break;
        case 412: d.mnemonic = I.Rc ? "orc." : "orc"; break;
        case 438: d.mnemonic = "ecowx";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 439: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "sthux";  break;
        case 444: d.mnemonic = I.Rc ? "or." : "or"; break;
        case 459: d.mnemonic = I.OE ? (I.Rc ? "divwu.o" : "divwuo") : (I.Rc ? "divwu." : "divwu"); break;
        case 467: d.mnemonic = "mtspr";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 470: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "dcbi";   break;
        case 476: d.mnemonic = I.Rc ? "nand." : "nand"; break;
        case 491: d.mnemonic = I.OE ? (I.Rc ? "divw.o" : "divwo") : (I.Rc ? "divw." : "divw"); break;
        case 512: d.mnemonic = "mcrxr";  break;
        case 533: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lswx";   break;
        case 534: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lwbrx";  break;
        case 535: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfsx";   break;
        case 536: d.mnemonic = I.Rc ? "srw." : "srw"; break;
        case 566: d.mnemonic = "tlbsync"; d.iclass = PPCInstrClass::SYSTEM; break;
        case 567: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfsux";  break;
        case 595: d.mnemonic = "mfsr";   d.iclass = PPCInstrClass::SYSTEM; break;
        case 597: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lswi";   break;
        case 598: d.mnemonic = "sync";   d.iclass = PPCInstrClass::SYSTEM; break;
        case 599: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfdx";   break;
        case 631: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "lfdux";  break;
        case 659: d.mnemonic = "mfsrin"; d.iclass = PPCInstrClass::SYSTEM; break;
        case 661: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stswx";  break;
        case 662: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stwbrx"; break;
        case 663: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfsx";  break;
        case 695: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfsux"; break;
        case 725: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "stswi";  break;
        case 727: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfdx";  break;
        case 759: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfdux"; break;
        case 790: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "lhbrx";  break;
        case 792: d.mnemonic = I.Rc ? "sraw." : "sraw"; break;
        case 824: d.mnemonic = I.Rc ? "srawi." : "srawi"; break;
        case 854: d.mnemonic = "eieio";  d.iclass = PPCInstrClass::SYSTEM; break;
        case 918: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "sthbrx"; break;
        case 922: d.mnemonic = I.Rc ? "extsh." : "extsh"; break;
        case 954: d.mnemonic = I.Rc ? "extsb." : "extsb"; break;
        case 982: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "icbi";   break;
        case 983: d.iclass = PPCInstrClass::FLOAT; d.mnemonic = "stfiwx"; break;
        case 1014: d.iclass = PPCInstrClass::LOADSTORE; d.mnemonic = "dcbz";  break;
        default:  d.mnemonic = "?31";  d.iclass = PPCInstrClass::UNKNOWN; break;
        }
        break;
    }
           // ---- FP register-register (table 59 – single precision) ------
    case 59: {
        u32 xop = I.SUBOP5;
        d.iclass = PPCInstrClass::FLOAT;
        switch (xop) {
        case 18: d.mnemonic = I.Rc ? "fdivs." : "fdivs";   break;
        case 20: d.mnemonic = I.Rc ? "fsubs." : "fsubs";   break;
        case 21: d.mnemonic = I.Rc ? "fadds." : "fadds";   break;
        case 22: d.mnemonic = I.Rc ? "fsqrts." : "fsqrts";  break;
        case 24: d.mnemonic = I.Rc ? "fres." : "fres";    break;
        case 25: d.mnemonic = I.Rc ? "fmuls." : "fmuls";   break;
        case 28: d.mnemonic = I.Rc ? "fmsubs." : "fmsubs";  break;
        case 29: d.mnemonic = I.Rc ? "fmadds." : "fmadds";  break;
        case 30: d.mnemonic = I.Rc ? "fnmsubs." : "fnmsubs"; break;
        case 31: d.mnemonic = I.Rc ? "fnmadds." : "fnmadds"; break;
        default: d.mnemonic = "?59"; d.iclass = PPCInstrClass::UNKNOWN; break;
        }
        break;
    }
           // ---- FP register-register (table 63 – double precision) ------
    case 63: {
        u32 xop = I.SUBOP10;
        u32 xop5 = I.SUBOP5;
        d.iclass = PPCInstrClass::FLOAT;
        switch (xop) {
        case 0:   d.mnemonic = "fcmpu";  break;
        case 12:  d.mnemonic = I.Rc ? "frsp." : "frsp";   break;
        case 14:  d.mnemonic = I.Rc ? "fctiw." : "fctiw";  break;
        case 15:  d.mnemonic = I.Rc ? "fctiwz." : "fctiwz"; break;
        case 32:  d.mnemonic = "fcmpo";  break;
        case 38:  d.mnemonic = "mtfsb1"; break;
        case 40:  d.mnemonic = I.Rc ? "fneg." : "fneg";   break;
        case 64:  d.mnemonic = "mcrfs";  break;
        case 70:  d.mnemonic = "mtfsb0"; break;
        case 72:  d.mnemonic = I.Rc ? "fmr." : "fmr";    break;
        case 134: d.mnemonic = I.Rc ? "mtfsfi." : "mtfsfi"; break;
        case 136: d.mnemonic = I.Rc ? "fnabs." : "fnabs";  break;
        case 264: d.mnemonic = I.Rc ? "fabs." : "fabs";   break;
        case 583: d.mnemonic = I.Rc ? "mffs." : "mffs";   break;
        case 711: d.mnemonic = I.Rc ? "mtfsf." : "mtfsf";  break;
        default:
            // fall back to 5-bit sub-opcode for multiply-add group
            switch (xop5) {
            case 18: d.mnemonic = I.Rc ? "fdiv." : "fdiv";    break;
            case 20: d.mnemonic = I.Rc ? "fsub." : "fsub";    break;
            case 21: d.mnemonic = I.Rc ? "fadd." : "fadd";    break;
            case 22: d.mnemonic = I.Rc ? "fsqrt." : "fsqrt";   break;
            case 23: d.mnemonic = I.Rc ? "fsel." : "fsel";    break;
            case 25: d.mnemonic = I.Rc ? "fmul." : "fmul";    break;
            case 26: d.mnemonic = I.Rc ? "frsqrte." : "frsqrte"; break;
            case 28: d.mnemonic = I.Rc ? "fmsub." : "fmsub";   break;
            case 29: d.mnemonic = I.Rc ? "fmadd." : "fmadd";   break;
            case 30: d.mnemonic = I.Rc ? "fnmsub." : "fnmsub";  break;
            case 31: d.mnemonic = I.Rc ? "fnmadd." : "fnmadd";  break;
            default: d.mnemonic = "?63"; d.iclass = PPCInstrClass::UNKNOWN; break;
            }
        }
        break;
    }
           // ---- Paired-single arithmetic (table 4) ----------------------
    case 4: {
        u32 xop = I.SUBOP10;
        u32 xop5 = I.SUBOP5;
        d.iclass = PPCInstrClass::PAIRED_SINGLE;
        switch (xop) {
        case 0:   d.mnemonic = "ps_cmpu0";  break;
        case 32:  d.mnemonic = "ps_cmpo0";  break;
        case 40:  d.mnemonic = I.Rc ? "ps_neg." : "ps_neg";    break;
        case 64:  d.mnemonic = "ps_cmpu1";  break;
        case 72:  d.mnemonic = I.Rc ? "ps_mr." : "ps_mr";     break;
        case 96:  d.mnemonic = "ps_cmpo1";  break;
        case 136: d.mnemonic = I.Rc ? "ps_nabs." : "ps_nabs";   break;
        case 264: d.mnemonic = I.Rc ? "ps_abs." : "ps_abs";    break;
        case 528: d.mnemonic = I.Rc ? "ps_merge00." : "ps_merge00"; break;
        case 560: d.mnemonic = I.Rc ? "ps_merge01." : "ps_merge01"; break;
        case 592: d.mnemonic = I.Rc ? "ps_merge10." : "ps_merge10"; break;
        case 624: d.mnemonic = I.Rc ? "ps_merge11." : "ps_merge11"; break;
        default:
            switch (xop5) {
            case 10: d.mnemonic = I.Rc ? "ps_sum0." : "ps_sum0";   break;
            case 11: d.mnemonic = I.Rc ? "ps_sum1." : "ps_sum1";   break;
            case 12: d.mnemonic = I.Rc ? "ps_muls0." : "ps_muls0";  break;
            case 13: d.mnemonic = I.Rc ? "ps_muls1." : "ps_muls1";  break;
            case 14: d.mnemonic = I.Rc ? "ps_madds0." : "ps_madds0"; break;
            case 15: d.mnemonic = I.Rc ? "ps_madds1." : "ps_madds1"; break;
            case 18: d.mnemonic = I.Rc ? "ps_div." : "ps_div";    break;
            case 20: d.mnemonic = I.Rc ? "ps_sub." : "ps_sub";    break;
            case 21: d.mnemonic = I.Rc ? "ps_add." : "ps_add";    break;
            case 23: d.mnemonic = I.Rc ? "ps_sel." : "ps_sel";    break;
            case 24: d.mnemonic = I.Rc ? "ps_res." : "ps_res";    break;
            case 25: d.mnemonic = I.Rc ? "ps_mul." : "ps_mul";    break;
            case 26: d.mnemonic = I.Rc ? "ps_rsqrte." : "ps_rsqrte"; break;
            case 28: d.mnemonic = I.Rc ? "ps_msub." : "ps_msub";   break;
            case 29: d.mnemonic = I.Rc ? "ps_madd." : "ps_madd";   break;
            case 30: d.mnemonic = I.Rc ? "ps_nmsub." : "ps_nmsub";  break;
            case 31: d.mnemonic = I.Rc ? "ps_nmadd." : "ps_nmadd";  break;
            default: d.mnemonic = "?4"; d.iclass = PPCInstrClass::UNKNOWN; break;
            }
        }
        break;
    }
    default:
        d.mnemonic = "???";
        d.iclass = PPCInstrClass::UNKNOWN;
        break;
    }
    return d;
}

// -------------------------------------------------------------------------
// Pretty-print a decoded instruction
// -------------------------------------------------------------------------
std::string ppc_disasm(const DecodedInstr& d) {
    char buf[128];
    const auto& I = d.raw;
    u32 op = I.OPCD;

    // Branches
    if (d.is_branch) {
        if (op == 18) {
            snprintf(buf, sizeof(buf), "%-10s 0x%08x", d.mnemonic.c_str(), d.branch_target);
            return buf;
        }
        if (op == 16) {
            snprintf(buf, sizeof(buf), "%-10s %u,%u,0x%08x",
                d.mnemonic.c_str(), I.BO, I.BI, d.branch_target);
            return buf;
        }
        return d.mnemonic;
    }

    // Integer immediate (D-form)
    switch (op) {
    case 7: case 8: case 12: case 13: case 14: case 15:
        snprintf(buf, sizeof(buf), "%-10s %s,%s,%d",
            d.mnemonic.c_str(), gpr_name(I.RD), gpr_name(I.RA), (s16)I.SIMM_16);
        return buf;
    case 10: case 11:
        snprintf(buf, sizeof(buf), "%-10s %s,%s,%u",
            d.mnemonic.c_str(), cr_name(I.CRFD), gpr_name(I.RA), I.UIMM);
        return buf;
    case 24: case 25: case 26: case 27:
        snprintf(buf, sizeof(buf), "%-10s %s,%s,0x%04x",
            d.mnemonic.c_str(), gpr_name(I.RA), gpr_name(I.RS), I.UIMM);
        return buf;
    case 28: case 29:
        snprintf(buf, sizeof(buf), "%-10s %s,%s,0x%04x",
            d.mnemonic.c_str(), gpr_name(I.RA), gpr_name(I.RS), I.UIMM);
        return buf;
        // Load/store D-form
    case 32: case 33: case 34: case 35:
    case 36: case 37: case 38: case 39:
    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47:
        snprintf(buf, sizeof(buf), "%-10s %s,%d(%s)",
            d.mnemonic.c_str(), gpr_name(I.RD), (s16)I.SIMM_16, gpr_name(I.RA));
        return buf;
        // FP load/store
    case 48: case 49: case 50: case 51:
    case 52: case 53: case 54: case 55:
        snprintf(buf, sizeof(buf), "%-10s %s,%d(%s)",
            d.mnemonic.c_str(), fpr_name(I.FD), (s16)I.SIMM_16, gpr_name(I.RA));
        return buf;
    }
    // Rotate/shift
    if (op == 20 || op == 21) {
        snprintf(buf, sizeof(buf), "%-10s %s,%s,%u,%u,%u",
            d.mnemonic.c_str(), gpr_name(I.RA), gpr_name(I.RS), I.SH, I.MB, I.ME);
        return buf;
    }
    if (op == 23) {
        snprintf(buf, sizeof(buf), "%-10s %s,%s,%s,%u,%u",
            d.mnemonic.c_str(), gpr_name(I.RA), gpr_name(I.RS), gpr_name(I.RB_2), I.MB, I.ME);
        return buf;
    }
    // Fallback
    snprintf(buf, sizeof(buf), "%-10s", d.mnemonic.c_str());
    return buf;
}
