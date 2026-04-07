# Dolphin PPC-to-C Static Recompiler — Prototype

A static recompiler using dolphin emulator code as a foundation that lifts PowerPC Gekko/Broadway (GameCube/Wii) machine code
to C source, which can then be compiled by any modern C compiler for the host platform. Fair warning I had some help of Claude AI for the experiment. 

## Architecture

```
Input (.dol/.elf)
    │
    ▼
loader/dol.cpp + loader/elf.cpp      — parse binary, map sections to guest addresses
    │
    ▼
ppc/disasm.cpp  (ppc_decode)         — decode one 32-bit PPC word → DecodedInstr
ppc/Instructions.cpp                 — analysis helpers (register deps, predicates)
    │
    ▼
analysis/cfg.cpp  (CFGBuilder)       — BFS over instructions → Function / BasicBlock DAG
    │
    ▼
codegen/c_backend.cpp (CBackend)     — emit one C statement per PPC instruction
    │
    ▼
output.c  +  optional dispatch table header
```

The generated `.c` file `#include`s `ppc_state.h` and `runtime.c` which provide:
- `PPCState` struct (GPRs, FPRs, CR, XER, LR, CTR, memory base pointer …)
- Memory access helpers with big→little endian conversion
- Condition register / XER helpers
- OS syscall stubs, hardware register stubs, GX FIFO stubs

## Building the recompiler

### Linux / macOS (GNU make)
```bash
make
# or with clang:
make CXX=clang++
```

**Note:** On Linux the build creates two lowercase-alias symlinks inside
`DolphinRecompilerPrototype/` (`instructions.h → Instructions.h` and
`elf.h → Elf.h`) so that `#include "instructions.h"` resolves correctly on
case-sensitive filesystems.  The Makefile does this automatically.

### Cross-platform (CMake)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows (Visual Studio)
Open `DolphinRecompilerPrototype.slnx` in Visual Studio 2022 or later and
build the `DolphinRecompilerPrototype` project.

## Usage

```
ppc_recompiler <input.dol|input.elf> [options]

Options:
  -o <output.c>     Output C file  (default: <input>_recomp.c)
  -entry <addr>     Additional entry point (hex)
  -wii              Wii mode (enable 64 MB MEM2)
  -no-comments      Suppress PPC disassembly comments in generated C
  -pc-updates       Keep s->pc updated every instruction (slower but debuggable)
  -cycle-count      Increment s->cycle_count every instruction
  -dispatch         Also write a dispatch-table header <output>.dispatch.h
  -h / --help       Print help
```

### Example
```bash
# Recompile a GameCube homebrew ELF
./ppc_recompiler hello.elf -o hello_recomp.c -dispatch

# Recompile a DOL, keep comments, also emit dispatch table
./ppc_recompiler game.dol -o game_recomp.c -no-comments -dispatch

# Wii mode with extra hand-specified entry point
./ppc_recompiler wii_app.elf -wii -entry 0x80010000 -o wii_app_recomp.c
```

## Using the generated C

The generated file must be compiled alongside `runtime.c` and with
`DolphinRecompilerPrototype/` on the include path (for `ppc_state.h`):

```bash
gcc -O2 -IDolphinRecompilerPrototype -o hello_recomp \
    hello_recomp.c DolphinRecompilerPrototype/runtime.c -lm
```

A minimal host harness:
```c
#include "ppc_state.h"
#include "hello_recomp.dispatch.h"   // defines dispatch_lookup()

extern void rt_init_memory(PPCState*, int is_wii);
extern void rt_setup_stack(PPCState*);

int main(void) {
    PPCState s = {0};
    rt_init_memory(&s, 0);   // allocate 24 MB guest RAM
    rt_setup_stack(&s);
    // Optionally copy additional data into s.mem_base ...

    TranslatedFn entry = dispatch_lookup(0x80000000);
    if (entry) entry(&s);
    return 0;
}
```
## Instruction coverage

| Group              | Status                                                                 |
|--------------------|------------------------------------------------------------------------|
| Integer ALU        | ✅ Full (add/sub/mul/div/shifts/rotate, all Rc/OE variants)            |
| Integer compare    | ✅ cmpi / cmpli / cmp / cmpl → CR field update                        |
| CR logical         | ✅ crnor / crandc / crxor / crnand / crand / creqv / crorc / cror / mcrf |
| Branches           | ✅ b / bc / blr / bcctr — direct, conditional, indirect                |
| Jump-table (bctr)  | ✅ Pattern-matched & recovered; emits `switch(s->ctr)` with real gotos |
| Load/Store int     | ✅ D-form + X-form + update forms (lwz/stw/lbz/stb/lhz/sth/lha/…)    |
| Load/Store FP      | ✅ lfs/lfd/stfs/stfd + update forms + X-form variants                  |
| PSQ loads/stores   | ✅ psq_l / psq_lu / psq_st / psq_stu — full GQR type/scale decode      |
| FP arithmetic      | ✅ fadd/fsub/fmul/fdiv/fsqrt/fmadd/fmsub/fnmadd/fnmsub (s + d)        |
| FP compare         | ✅ fcmpu / fcmpo → CR field update                                     |
| FP convert         | ✅ frsp / fctiw / fctiwz / fneg / fmr / fabs / fnabs                  |
| FP exceptions      | ✅ FPSCR sticky bits + FPRF updated via fenv.h (opt-in with `-fpscr`)  |
| Paired-single      | ✅ ps_add/sub/mul/div/madd/msub/nmadd/nmsub/neg/mr/abs/nabs/merge/res  |
| System / SPR       | ✅ mfspr/mtspr: LR, CTR, XER, DEC, SRR0/1, GQR0-7                    |
| Cache hints        | ✅ dcbz (zero 32-byte line); dcbf/dcbst/icbi/sync/eieio = no-ops       |
| Hardware registers | ✅ Full GC/Wii peripheral map: CP, VI, PE, MI, DSP, DI, SI, EXI, AI   |
|                    |    PI, plus Wii-only IPC, USB, AHB — 16-bit and 32-bit access   

## Known limitations
- **Indirect branches without a recoverable table** still fall back to the
  dispatcher return. The pattern-matcher requires the `lis/addi/lwzx/mtctr/bctr`
  sequence to be intact in the same basic block; obfuscated or PIE code may need
  manual `-entry` hints.
- **`lswi`/`lswx`/`stswi`/`stswx`** (string load/store) are not yet lifted;
  they emit `rt_unimplemented`.
- **Wii IPC** beyond the basic mailbox acknowledgement stub is not modelled.
  Full OS-level IPC (IOCTL, file I/O, USB) requires a Starlet ARM co-simulation.
- **DSI/ISI exceptions** from bad memory accesses are not raised; out-of-bounds
  guest addresses wrap silently within `mem_size`.
