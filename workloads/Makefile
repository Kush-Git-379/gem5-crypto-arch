# Makefile — builds the three cipher workloads for gem5 SE mode.
#
# Two build flavours:
#   make            -> gem5 binaries: static, with m5 ROI markers
#   make native     -> host binaries: dynamic, no markers, for correctness checks
#
# Key flags and why they are non-negotiable:
#   -static      gem5 SE mode cannot handle a dynamic loader reliably.
#   -mno-aes     AES-NI would make the AES comparison meaningless. This is the
#                single most important flag in the file.
#   -mno-sse4.2  keeps out CRC32/other accelerators that could skew the mix.
#   -O2          the realistic optimisation level. -O0 would measure the
#                compiler's spilling, not the cipher.
#   -fno-tree-vectorize
#                keeps the comparison at scalar ILP. Auto-vectorising ASCON's
#                five independent lanes into SIMD would answer a different
#                question than the one being asked. Revisit only deliberately.

CC       ?= gcc
M5_PATH  ?= $(HOME)/gem5

# Blocks processed inside the ROI. Override: make N_BLOCKS=2000
N_BLOCKS    ?= 1000
BLOCK_BYTES ?= 64

COMMON_CFLAGS := -O2 -std=c99 -Wall -Wextra \
                 -mno-aes -mno-sse4.2 -fno-tree-vectorize \
                 -DN_BLOCKS=$(N_BLOCKS) -DBLOCK_BYTES=$(BLOCK_BYTES)

# gem5's m5ops library, for m5_reset_stats / m5_dump_stats.
M5_INC := -I$(M5_PATH)/include
M5_LIB := $(M5_PATH)/util/m5/build/x86/out/libm5.a

GEM5_CFLAGS  := $(COMMON_CFLAGS) -static -DGEM5_ROI $(M5_INC)
GEM5_LDFLAGS := $(M5_LIB)

NATIVE_CFLAGS := $(COMMON_CFLAGS)

BIN      := bin
WORKLOADS := ascon aes des

GEM5_BINS   := $(addprefix $(BIN)/,$(addsuffix .gem5,$(WORKLOADS)))
NATIVE_BINS := $(addprefix $(BIN)/,$(addsuffix .native,$(WORKLOADS)))

.PHONY: all native check clean

all: $(GEM5_BINS)

native: $(NATIVE_BINS)

$(BIN):
	mkdir -p $(BIN)

# gem5 targets: static + ROI markers + m5 library
$(BIN)/%.gem5: %.c harness.h | $(BIN)
	@test -f $(M5_LIB) || { \
	  echo "ERROR: $(M5_LIB) not found."; \
	  echo "Build it first:  cd $(M5_PATH)/util/m5 && scons build/x86/out/m5"; \
	  echo "Or point M5_PATH at your gem5 checkout:  make M5_PATH=/path/to/gem5"; \
	  exit 1; }
	$(CC) $(GEM5_CFLAGS) -o $@ $< $(GEM5_LDFLAGS)

# native targets: no markers, dynamically linked, for correctness testing
$(BIN)/%.native: %.c harness.h | $(BIN)
	$(CC) $(NATIVE_CFLAGS) -o $@ $<

# Verify the implementations against known-answer test vectors before trusting
# any performance number they produce.
check: | $(BIN)
	$(CC) $(NATIVE_CFLAGS) -o $(BIN)/selftest selftest.c
	./$(BIN)/selftest

clean:
	rm -rf $(BIN)
