# Makefile for Linux/macOS.  Usage: make  OR  make CXX=clang++
CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
CFLAGS   ?= -std=c11  -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS  ?= -lm

SRC = DolphinRecompilerPrototype
OBJS = \
    $(SRC)/main.o          \
    $(SRC)/Instructions.o  \
    $(SRC)/Instruction.o   \
    $(SRC)/disasm.o        \
    $(SRC)/cfg.o           \
    $(SRC)/c_backend.o     \
    $(SRC)/dol.o           \
    $(SRC)/elf.o           \
    $(SRC)/runtime.o

TARGET = ppc_recompiler

.PHONY: all clean symlinks

all: symlinks $(TARGET)

# Case-insensitive symlinks needed on Linux for headers included as lowercase
symlinks:
	@ln -sf Instructions.h $(SRC)/instructions.h 2>/dev/null || true
	@ln -sf Elf.h $(SRC)/elf.h 2>/dev/null || true

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(SRC)/%.o: $(SRC)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(SRC) -c -o $@ $<

$(SRC)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(SRC) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(SRC)/instructions.h $(SRC)/elf.h
