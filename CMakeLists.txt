cmake_minimum_required(VERSION 3.16)
project(DolphinRecompilerPrototype CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(SOURCES
    DolphinRecompilerPrototype/main.cpp
    DolphinRecompilerPrototype/Instructions.cpp
    DolphinRecompilerPrototype/Instruction.cpp
    DolphinRecompilerPrototype/disasm.cpp
    DolphinRecompilerPrototype/cfg.cpp
    DolphinRecompilerPrototype/c_backend.cpp
    DolphinRecompilerPrototype/dol.cpp
    DolphinRecompilerPrototype/elf.cpp
    DolphinRecompilerPrototype/runtime.c
)

add_executable(ppc_recompiler ${SOURCES})

target_include_directories(ppc_recompiler PRIVATE
    DolphinRecompilerPrototype
)

if(MSVC)
    target_compile_options(ppc_recompiler PRIVATE /W3 /WX-)
else()
    target_compile_options(ppc_recompiler PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
    )
endif()

if(UNIX)
    target_link_libraries(ppc_recompiler m)
endif()

install(TARGETS ppc_recompiler RUNTIME DESTINATION bin)
