// runtime/ppc_state.h
// Guest CPU state structure shared between the recompiled code and the runtime.
// This is the single most performance-critical struct: keep it cache-friendly.
// Layout mirrors Dolphin's PowerPCState where applicable.

#pragma once
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;
typedef double   f64;

// -------------------------------------------------------------------------
// Condition Register fields
// -------------------------------------------------------------------------
typedef union {
    u32 hex;
    struct {
        u32 SO7 : 1; u32 EQ7 : 1; u32 GT7 : 1; u32 LT7 : 1;
        u32 SO6 : 1; u32 EQ6 : 1; u32 GT6 : 1; u32 LT6 : 1;
        u32 SO5 : 1; u32 EQ5 : 1; u32 GT5 : 1; u32 LT5 : 1;
        u32 SO4 : 1; u32 EQ4 : 1; u32 GT4 : 1; u32 LT4 : 1;
        u32 SO3 : 1; u32 EQ3 : 1; u32 GT3 : 1; u32 LT3 : 1;
        u32 SO2 : 1; u32 EQ2 : 1; u32 GT2 : 1; u32 LT2 : 1;
        u32 SO1 : 1; u32 EQ1 : 1; u32 GT1 : 1; u32 LT1 : 1;
        u32 SO0 : 1; u32 EQ0 : 1; u32 GT0 : 1; u32 LT0 : 1;
    };
} CondReg;

// -------------------------------------------------------------------------
// XER – integer exception register
// -------------------------------------------------------------------------
typedef union {
    u32 hex;
    struct {
        u32 byte_count : 7;  // for lswx/stswx
        u32 : 22;
        u32 CA : 1;
        u32 OV : 1;
        u32 SO : 1;
    };
} XERReg;

// -------------------------------------------------------------------------
// FPSCR – floating-point status/control register
// -------------------------------------------------------------------------
typedef union {
    u32 hex;
    struct {
        u32 RN : 2;      // rounding mode
        u32 NI : 1;      // non-IEEE mode (flush denormals)
        u32 XE : 1;      u32 ZE : 1; u32 UE : 1; u32 OE : 1; u32 VE : 1;
        u32 VXCVI : 1;   u32 VXSQRT : 1; u32 VXSOFT : 1;
        u32 : 1;
        u32 FPRF : 5;    // floating-point result flags
        u32 : 1;
        u32 FI : 1;      // inexact
        u32 FR : 1;      // rounded
        u32 VXVC : 1;    u32 VXIMZ : 1; u32 VXZDZ : 1; u32 VXIDI : 1;
        u32 VXISI : 1;   u32 VXSNAN : 1;
        u32 XX : 1;      u32 ZX : 1; u32 UX : 1; u32 OX : 1;
        u32 VX : 1;      u32 FEX : 1; u32 FX : 1;
    };
} FPSCRReg;

// -------------------------------------------------------------------------
// Double FP register (Gekko stores both ps0 and ps1 for paired-singles)
// -------------------------------------------------------------------------
typedef union {
    u64 as_u64;
    f64 as_f64;
    struct { f32 ps1; f32 ps0; };   // ps0 in low 32 bits (big-endian layout)
} FPReg;

// -------------------------------------------------------------------------
// Main CPU state block
// -------------------------------------------------------------------------
typedef struct PPCState {
    // General-purpose registers  (offset 0)
    u32 gpr[32];        // r0..r31

    // Floating-point registers   (offset 0x80)
    FPReg fpr[32];      // f0..f31 (each holds ps0+ps1 for GQR paired-single)

    // Special-purpose registers  (offset 0x180)
    u32 pc;             // program counter
    u32 lr;             // link register
    u32 ctr;            // count register
    CondReg cr;         // condition register
    XERReg  xer;        // XER
    FPSCRReg fpscr;     // FPSCR

    // Segment registers
    u32 sr[16];

    // SPRs most used by GC/Wii code
    u32 srr0, srr1;     // machine state save/restore
    u32 dsisr, dar;
    u32 dec;            // decrementer
    u32 hid0, hid2;
    u32 wpar;           // write-gather pipe address register

    // GQRs (gather/quantize registers) for paired-single loads/stores
    u32 gqr[8];

    // Host memory base – used by the runtime to convert guest?host addresses
    u8* mem_base;       // points to 24MB (GC) or 88MB (Wii) guest RAM
    u32 mem_size;

    // Cycle counter (approximate)
    u64 cycle_count;
} PPCState;

// -------------------------------------------------------------------------
// Helper macros used in generated code
// -------------------------------------------------------------------------
#define STATE_GPR(s,n)   ((s)->gpr[(n)])
#define STATE_FPR_f64(s,n) ((s)->fpr[(n)].as_f64)
#define STATE_FPR_ps0(s,n) ((s)->fpr[(n)].ps0)
#define STATE_FPR_ps1(s,n) ((s)->fpr[(n)].ps1)

// -------------------------------------------------------------------------
// Memory access helpers (handle big-endian conversion on host)
// -------------------------------------------------------------------------
#if defined(__GNUC__) || defined(__clang__)
#  include <byteswap.h>
#  define BSWAP16(x) __builtin_bswap16(x)
#  define BSWAP32(x) __builtin_bswap32(x)
#  define BSWAP64(x) __builtin_bswap64(x)
#else
static inline u16 BSWAP16(u16 x) { return (x >> 8) | (x << 8); }
static inline u32 BSWAP32(u32 x) {
    return ((x & 0xFF000000u) >> 24) | ((x & 0xFF0000u) >> 8) | ((x & 0xFF00u) << 8) | ((x & 0xFFu) << 24);
}
static inline u64 BSWAP64(u64 x) {
    return ((u64)BSWAP32((u32)x) << 32) | BSWAP32((u32)(x >> 32));
}
#endif

// Read helpers (guest address ? host value, big-endian)
static inline u8  mem_read8(PPCState* s, u32 ea) { return s->mem_base[ea & (s->mem_size - 1)]; }
static inline u16 mem_read16(PPCState* s, u32 ea) {
    u16 v; __builtin_memcpy(&v, s->mem_base + (ea & (s->mem_size - 1)), 2);
    return BSWAP16(v);
}
static inline u32 mem_read32(PPCState* s, u32 ea) {
    u32 v; __builtin_memcpy(&v, s->mem_base + (ea & (s->mem_size - 1)), 4);
    return BSWAP32(v);
}
static inline u64 mem_read64(PPCState* s, u32 ea) {
    u64 v; __builtin_memcpy(&v, s->mem_base + (ea & (s->mem_size - 1)), 8);
    return BSWAP64(v);
}
// Write helpers
static inline void mem_write8(PPCState* s, u32 ea, u8  v) { s->mem_base[ea & (s->mem_size - 1)] = v; }
static inline void mem_write16(PPCState* s, u32 ea, u16 v) {
    v = BSWAP16(v); __builtin_memcpy(s->mem_base + (ea & (s->mem_size - 1)), &v, 2);
}
static inline void mem_write32(PPCState* s, u32 ea, u32 v) {
    v = BSWAP32(v); __builtin_memcpy(s->mem_base + (ea & (s->mem_size - 1)), &v, 4);
}
static inline void mem_write64(PPCState* s, u32 ea, u64 v) {
    v = BSWAP64(v); __builtin_memcpy(s->mem_base + (ea & (s->mem_size - 1)), &v, 8);
}

// -------------------------------------------------------------------------
// Condition register helpers
// -------------------------------------------------------------------------
static inline void cr_set_so(PPCState* s, int crn, int so) {
    u32 shift = (7 - crn) * 4;
    s->cr.hex = (s->cr.hex & ~(1u << (shift + 0))) | ((u32)(so & 1) << (shift + 0));
}
static inline void cr_set_field(PPCState* s, int crn, int lt, int gt, int eq, int so) {
    u32 shift = (7 - crn) * 4;
    u32 mask = 0xFu << shift;
    u32 val = ((u32)(lt & 1) << (shift + 3)) | ((u32)(gt & 1) << (shift + 2))
        | ((u32)(eq & 1) << (shift + 1)) | ((u32)(so & 1) << shift);
    s->cr.hex = (s->cr.hex & ~mask) | val;
}
static inline u32 cr_get_field(PPCState* s, int crn) {
    return (s->cr.hex >> ((7 - crn) * 4)) & 0xF;
}
static inline int cr_get_bit(PPCState* s, int bi) {
    // bi: 0=LT0,1=GT0,2=EQ0,3=SO0, 4=LT1,...
    return (s->cr.hex >> (31 - bi)) & 1;
}
static inline void cr_set_bit(PPCState* s, int bi, int val) {
    u32 mask = 1u << (31 - bi);
    if (val) s->cr.hex |= mask;
    else     s->cr.hex &= ~mask;
}

// Compare and set CR0
static inline void cmp_set_cr0(PPCState* s, s32 a, s32 b) {
    int lt = a < b, gt = a > b, eq = a == b;
    cr_set_field(s, 0, lt, gt, eq, s->xer.SO);
}
static inline void cmpl_set_cr0(PPCState* s, u32 a, u32 b) {
    int lt = a < b, gt = a > b, eq = a == b;
    cr_set_field(s, 0, lt, gt, eq, s->xer.SO);
}

// Update XER carry
static inline void xer_set_ca(PPCState* s, int ca) { s->xer.CA = ca & 1; }

// Rotate helpers
static inline u32 rotl32(u32 v, u32 n) { n &= 31; return (v << n) | (v >> (32 - n)); }

// Sign extend from arbitrary width
static inline s32 sign_extend(u32 v, u32 bits) {
    u32 shift = 32 - bits;
    return (s32)(v << shift) >> shift;
}
