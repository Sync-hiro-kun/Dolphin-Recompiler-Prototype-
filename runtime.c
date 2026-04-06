// runtime / runtime.c
// Host-side runtime that recompiled code calls into for:
//   - OS syscalls (OSReport, OSFatal, etc.)
//   - Hardware register access stubs
//   - Memory-mapped I/O
//   - System call (sc) handling

#include "ppc_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
// -------------------------------------------------------------------------
// Logging / debugging
// -------------------------------------------------------------------------
void rt_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

// -------------------------------------------------------------------------
// Unimplemented instruction handler
// Recompiled code calls this for any instruction not yet lifted.
// -------------------------------------------------------------------------
void rt_unimplemented(PPCState* s, u32 pc, u32 opcode) {
    fprintf(stderr, "[RECOMP] Unimplemented instruction at 0x%08x: 0x%08x\n", pc, opcode);
    // In a real implementation, fall back to interpreter here.
    // For now, just continue so the developer can see coverage.
}

// -------------------------------------------------------------------------
// System call handler (sc instruction)
// GC/Wii use sc for a handful of privileged operations.
// -------------------------------------------------------------------------
void rt_syscall(PPCState* s) {
    u32 id = s->gpr[0];
    switch (id) {
    case 0x01:  // dvdRead / legacy
        fprintf(stderr, "[SC] dvdRead stub (r3=0x%x)\n", s->gpr[3]);
        break;
    case 0x02:  // dvdInquiry
        s->gpr[3] = 0;
        break;
    default:
        fprintf(stderr, "[SC] Unknown syscall 0x%x at PC=0x%x\n", id, s->pc);
        break;
    }
}

// -------------------------------------------------------------------------
// Hardware register emulation
// Stubbed out enough to let simple homebrew run without crashing.
// -------------------------------------------------------------------------

// PI (processor interface) registers
#define PI_BASE 0xCC003000u

static u32 pi_regs[0x100 / 4] = { 0 };

u32 rt_hw_read32(PPCState* s, u32 addr) {
    if (addr >= PI_BASE && addr < PI_BASE + 0x100)
        return pi_regs[(addr - PI_BASE) >> 2];
    fprintf(stderr, "[HW] Unknown read32 @ 0x%08x\n", addr);
    return 0;
}

void rt_hw_write32(PPCState* s, u32 addr, u32 val) {
    if (addr >= PI_BASE && addr < PI_BASE + 0x100) {
        pi_regs[(addr - PI_BASE) >> 2] = val;
        return;
    }
    fprintf(stderr, "[HW] Unknown write32 @ 0x%08x = 0x%x\n", addr, val);
}

// -------------------------------------------------------------------------
// GC/Wii memory layout helpers
// -------------------------------------------------------------------------
#define GC_MEM1_BASE  0x80000000u
#define GC_MEM1_SIZE  (24 * 1024 * 1024)
#define WII_MEM1_SIZE (24 * 1024 * 1024)
#define WII_MEM2_SIZE (64 * 1024 * 1024)

static u8* s_mem1 = NULL;
static u8* s_mem2 = NULL;  // Wii MEM2
static u32  s_mem1_size = 0;

void rt_init_memory(PPCState* s, int is_wii) {
    s_mem1_size = is_wii ? WII_MEM1_SIZE : GC_MEM1_SIZE;
    s_mem1 = (u8*)calloc(1, s_mem1_size);
    if (!s_mem1) { fprintf(stderr, "[RT] Out of memory\n"); exit(1); }
    s->mem_base = s_mem1;
    s->mem_size = s_mem1_size;
    if (is_wii) {
        s_mem2 = (u8*)calloc(1, WII_MEM2_SIZE);
        if (!s_mem2) { fprintf(stderr, "[RT] Out of memory (MEM2)\n"); exit(1); }
    }
    fprintf(stderr, "[RT] Memory: MEM1=%uMB%s\n",
        s_mem1_size >> 20, is_wii ? " MEM2=64MB" : "");
}

void rt_load_section(PPCState* s, u32 guest_addr, const void* data, u32 size) {
    u32 masked = guest_addr & 0x017FFFFFu;  // strip GC virtual base
    if (masked + size > s->mem_size) {
        fprintf(stderr, "[RT] Section 0x%x+%u overflows memory\n", guest_addr, size);
        return;
    }
    memcpy(s->mem_base + masked, data, size);
}

void rt_setup_stack(PPCState* s) {
    // GC/Wii convention: stack grows down from near end of MEM1
    u32 stack_top = (s->mem_size - 0x1000) & ~0xFu;
    s->gpr[1] = GC_MEM1_BASE | stack_top;
    // Write stack sentinel
    u32 sentinel = 0xDEADBEEFu;
    mem_write32(s, s->gpr[1], sentinel);
}

// -------------------------------------------------------------------------
// Decrementer / time-base stubs
// -------------------------------------------------------------------------
u32 rt_read_dec(PPCState* s) { return s->dec; }
u32 rt_read_tbu(PPCState* s) { return (u32)(s->cycle_count >> 32); }
u32 rt_read_tbl(PPCState* s) { return (u32)(s->cycle_count); }
void rt_write_dec(PPCState* s, u32 v) { s->dec = v; }

// -------------------------------------------------------------------------
// Write-gather pipe (WPAR) – used by GX FIFO
// -------------------------------------------------------------------------
static u8 s_gx_fifo[0x20000];
static u32 s_gx_fifo_pos = 0;

void rt_gx_write8(PPCState* s, u8  v) { s_gx_fifo[s_gx_fifo_pos++ & 0x1FFFF] = v; }
void rt_gx_write16(PPCState* s, u16 v) {
    s_gx_fifo[(s_gx_fifo_pos) & 0x1FFFF] = (u8)(v >> 8);
    s_gx_fifo[(s_gx_fifo_pos + 1) & 0x1FFFF] = (u8)(v);
    s_gx_fifo_pos += 2;
}
void rt_gx_write32(PPCState* s, u32 v) {
    s_gx_fifo[(s_gx_fifo_pos) & 0x1FFFF] = (u8)(v >> 24);
    s_gx_fifo[(s_gx_fifo_pos + 1) & 0x1FFFF] = (u8)(v >> 16);
    s_gx_fifo[(s_gx_fifo_pos + 2) & 0x1FFFF] = (u8)(v >> 8);
    s_gx_fifo[(s_gx_fifo_pos + 3) & 0x1FFFF] = (u8)(v);
    s_gx_fifo_pos += 4;
}