#pragma once
// ppc/instruction.h
// PowerPC Gekko/Broadway instruction decoder.
// Adapted from Dolphin Emulator's Gekko.h (GPL-2.0-or-later).
// The UGeckoInstruction union layout is preserved exactly so that any Dolphin
// opcode-table logic can be re-used with zero changes.

#pragma once
#include <cstdint>
#include <string>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// -------------------------------------------------------------------------
// Exact replica of Dolphin's UGeckoInstruction union
// -------------------------------------------------------------------------
union UGeckoInstruction {
    u32 hex = 0;

    UGeckoInstruction() = default;
    explicit UGeckoInstruction(u32 h) : hex(h) {}

    struct { u32 Rc : 1; u32 SUBOP10 : 10; u32 RB : 5; u32 RA : 5; u32 RD : 5; u32 OPCD : 6; };
    struct { s32 SIMM_16 : 16; u32 : 5; u32 TO : 5; u32 OPCD_2 : 6; };
    struct { u32 Rc_2 : 1; u32 : 10; u32 : 5; u32 : 5; u32 RS : 5; u32 OPCD_3 : 6; };
    struct { u32 UIMM : 16; u32 : 5; u32 : 5; u32 OPCD_4 : 6; };
    struct { u32 LK : 1; u32 AA : 1; u32 LI : 24; u32 OPCD_5 : 6; };
    struct { u32 LK_2 : 1; u32 AA_2 : 1; u32 BD : 14; u32 BI : 5; u32 BO : 5; u32 OPCD_6 : 6; };
    struct { u32 LK_3 : 1; u32 XO : 10; u32 : 5; u32 BI_2 : 5; u32 BO_2 : 5; u32 OPCD_7 : 6; };
    struct { u32 : 11; u32 RB_2 : 5; u32 RA_2 : 5; u32 RS_2 : 5; u32 OPCD_8 : 6; };
    struct { u32 : 1; u32 SUBOP10_2 : 10; u32 RB_3 : 5; u32 RA_3 : 5; u32 RT : 5; u32 OPCD_9 : 6; };
    struct { u32 RC : 1; u32 : 10; u32 : 5; u32 RA_4 : 5; u32 RD_2 : 5; u32 OPCD_a : 6; };
    struct { u32 : 1; u32 SUBOP10_3 : 10; u32 : 5; u32 RA_5 : 5; u32 : 5; u32 OPCD_b : 6; };
    struct { u32 : 1; u32 XO_2 : 9; u32 OE : 1; u32 RB_4 : 5; u32 RA_6 : 5; u32 RT_2 : 5; u32 OPCD_c : 6; };
    struct { u32 : 1; u32 XO_3 : 10; u32 : 5; u32 : 5; u32 : 5; u32 OPCD_d : 6; };
    // FP fields
    struct { u32 RC_2 : 1; u32 SUBOP5 : 5; u32 RC_3 : 5; u32 RB_5 : 5; u32 RA_7 : 5; u32 FD : 5; u32 OPCD_e : 6; };
    // Shift/mask fields
    struct { u32 Rc_3 : 1; u32 SH : 5; u32 MB : 5; u32 ME : 5; u32 RA_8 : 5; u32 RS_3 : 5; u32 OPCD_f : 6; };
    struct { u32 Rc_4 : 1; u32 SH_2 : 5; u32 MB_2 : 5; u32 ME_2 : 5; u32 : 5; u32 : 5; u32 OPCD_10 : 6; };
    // CRF
    struct { u32 : 18; u32 CRFS : 3; u32 : 2; u32 CRFD : 3; u32 OPCD_11 : 6; };
    // SPR
    struct { u32 : 1; u32 : 10; u32 SPRH : 5; u32 SPRL : 5; u32 RD_3 : 5; u32 OPCD_12 : 6; };
    // lswi
    struct { u32 : 1; u32 XO_4 : 10; u32 NB : 5; u32 : 5; u32 : 5; u32 OPCD_13 : 6; };
    // paired-single load/store
    struct { s32 SIMM_12 : 12; u32 W : 1; u32 I : 3; u32 RA_9 : 5; u32 RS_4 : 5; u32 OPCD_14 : 6; };

    // Helper: get 10-bit extended opcode for tables 19/31/59/63
    u32 Function()  const { return SUBOP10; }
    // Helper: reconstruct signed branch offset for bcx
    s32 BranchOffset() const { return (s32)(((s32)BD << 16) >> 14); }
    // Helper: reconstruct LI for bx (-/+ 25-bit, shifted << 2)
    s32 LIOffset()   const { return (s32)(((s32)LI << 8) >> 6); }
    // SPR index
    u32 SPR()        const { return (SPRH << 5) | SPRL; }
};

static_assert(sizeof(UGeckoInstruction) == 4, "Instruction must be 4 bytes");

// -------------------------------------------------------------------------
// Instruction categories used by the lifter
// -------------------------------------------------------------------------
enum class PPCInstrClass : u8 {
    INTEGER,
    FLOAT,
    LOADSTORE,
    BRANCH,
    SYSTEM,
    PAIRED_SINGLE,
    UNKNOWN,
};

struct DecodedInstr {
    UGeckoInstruction raw;
    u32               address;
    PPCInstrClass     iclass;
    std::string       mnemonic;   // human-readable name
    bool              is_branch;
    bool              branch_conditional;
    bool              branch_link;         // sets LR
    bool              branch_absolute;
    u32               branch_target;       // UINT32_MAX if indirect
    bool              ends_block;          // true for unconditional direct branches, rfi, blr, etc.
};

// -------------------------------------------------------------------------
// Disassemble one instruction into a DecodedInstr.
// address: guest PC of this instruction.
// -------------------------------------------------------------------------
DecodedInstr ppc_decode(u32 word, u32 address);

// Stringify a decoded instruction for debug output
std::string ppc_disasm(const DecodedInstr& d);
