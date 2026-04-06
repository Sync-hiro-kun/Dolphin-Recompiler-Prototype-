# Dolphin PPC-to-C Static Recompiler — Prototype

A static recompiler that lifts PowerPC Gekko/Broadway (GameCube/Wii) machine code
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

| Group            | Status                                      |
|------------------|---------------------------------------------|
| Integer ALU      | ✅ Full (add, sub, mul, div, shifts, rotate) |
| Integer compare  | ✅ cmpi / cmpli / cmp / cmpl                |
| CR logical       | ✅ crnor / crandc / crxor / crand / creqv / crorc / cror / crnand / mcrf |
| Branches         | ✅ b / bc / blr / bcctr (direct + indirect) |
| Load/Store int   | ✅ D-form + X-form + update forms           |
| Load/Store FP    | ✅ lfs/lfd/stfs/stfd + update + X-form      |
| FP arithmetic    | ✅ fadd/fsub/fmul/fdiv/fsqrt/fmadd/fmsub …  |
| FP compare       | ✅ fcmpu / fcmpo                            |
| FP convert       | ✅ frsp / fctiw / fctiwz                    |
| Paired-single    | ✅ ps_add/sub/mul/div/madd/neg/mr/abs/merge |
| System / SPR     | ✅ mfspr/mtspr (LR,CTR,XER,SRR0/1) + stubs |
| Cache hints      | ✅ dcbz (zero 32-byte line) + no-op stubs  |

## Known limitations

- **Indirect branches through a table (`bctr`)** fall back to dispatcher return;
  a jump-table recovery pass is not yet implemented.
- **Paired-single quantized loads/stores** (`psq_l`/`psq_st`) emit stubs; the
  GQR scaling logic is not yet modelled.
- **Floating-point exceptions and FPSCR flags** are not updated (stubs only).
- **Wii IPC / DSP / VI registers** beyond basic PI are not emulated.
