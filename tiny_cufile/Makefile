# ============================================================================
# tiny_cufile -- build
#
# Profiles (objects cached per-profile under build/<profile>/, no clobber):
#   make            release build: -O2 -g -DNDEBUG              (default)
#   make debug      -O0 -g3 -DTINY_CU_DEBUG  (libs + harnesses)
#   make asan       debug + AddressSanitizer + UBSan
#   make tsan       debug + ThreadSanitizer (validates the per-chunk mutex)
#   make tests      build the harnesses for the current profile
#   make <name>     build one harness, e.g. `make stress_threads`
#   make <name>_real build it against the genuine libcufile (A/B twin)
#   make clean      remove build/ and all outputs
#   make help       list targets
#
# Knobs (override on the command line, e.g. `make STRICT=1 CUDA_HOME=/opt/cuda`):
#   BUILD=release|debug|asan|tsan   profile (default release)
#   STRICT=1                        add -Wconversion -Wsign-conversion -Wcast-qual -Werror
#   CUDA_HOME=/usr/local/cuda       CUDA toolkit root
#   CUDA_ARCH=x86_64-linux          toolkit target triple (targets/<arch>/lib)
#   CC=gcc                          compiler
#   TCU_DEFAULT_MAX_CHUNK_MB=N      bake a different default chunk size
# ============================================================================

# Default to gcc, but honor an explicit `make CC=clang` / env override. (Plain
# `CC ?= gcc` wouldn't work: make pre-defines CC=cc with origin 'default'.)
ifeq ($(origin CC),default)
  CC := gcc
endif
BUILD ?= release

# ---- CUDA toolkit ----------------------------------------------------------
CUDA_HOME ?= /usr/local/cuda
CUDA_ARCH ?= x86_64-linux
# cudart/cuda live in lib64; libcufile lives under targets/<arch>/lib -- search both.
CUDA_LIBDIRS  := $(CUDA_HOME)/lib64 $(CUDA_HOME)/targets/$(CUDA_ARCH)/lib
CUFILE_LIBDIR ?= $(CUDA_HOME)/targets/$(CUDA_ARCH)/lib
CUDA_LDFLAGS  := $(addprefix -L,$(CUDA_LIBDIRS)) -lcudart -lcuda

# ---- per-profile flags -----------------------------------------------------
ifeq ($(BUILD),release)
  OPT     := -O2 -g
  PROF_CF := -DNDEBUG
  PROF_LD :=
else ifeq ($(BUILD),debug)
  OPT     := -O0 -g3
  PROF_CF := -DTINY_CU_DEBUG
  PROF_LD :=
else ifeq ($(BUILD),asan)
  OPT     := -O1 -g3 -fno-omit-frame-pointer
  PROF_CF := -DTINY_CU_DEBUG -fsanitize=address,undefined
  PROF_LD := -fsanitize=address,undefined
else ifeq ($(BUILD),tsan)
  OPT     := -O1 -g3 -fno-omit-frame-pointer
  PROF_CF := -DTINY_CU_DEBUG -fsanitize=thread
  PROF_LD := -fsanitize=thread
else
  $(error Unknown BUILD='$(BUILD)' -- use release|debug|asan|tsan)
endif

# ---- warnings (default sane; STRICT=1 turns on the aggressive/CI set) -------
WARN := -Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
        -Wpointer-arith -Wvla -Wundef
ifeq ($(STRICT),1)
  WARN += -Wconversion -Wsign-conversion -Wcast-qual -Wwrite-strings -Werror
endif

# ---- layout / flags --------------------------------------------------------
SRC_DIR  := src
OBJ_DIR  := build/$(BUILD)
INCLUDES := -I./include -I$(CUDA_HOME)/include
STD      := -std=gnu11

# EXTRA_CFLAGS is an append hook for one-off defines, e.g.
#   make EXTRA_CFLAGS=-DTCU_DEFAULT_MAX_CHUNK_MB=4
CFLAGS   := $(STD) $(WARN) $(OPT) -fPIC $(INCLUDES) $(PROF_CF) $(EXTRA_CFLAGS)
DEPFLAGS := -MMD -MP

LIB_NAME    := libtiny_cufile.so
PRELOAD_LIB := ldp_libcufile.so

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# Harnesses built by `make tests` (each also buildable by name). trigger_raw_ioctl
# is a raw-ioctl diagnostic, left out of the default set but buildable on demand.
TEST_BINS := simple_sync simple_async stress_threads bench_write
PTHREAD   := -lpthread

# ---- default + profile convenience targets ---------------------------------
.DEFAULT_GOAL := all

all: $(LIB_NAME) $(PRELOAD_LIB)

tests: $(TEST_BINS)

# `make debug|asan|tsan|release` re-invokes with that profile, libs + harnesses.
release debug asan tsan:
	@$(MAKE) --no-print-directory all tests BUILD=$@

# ---- shared libraries ------------------------------------------------------
$(LIB_NAME): $(OBJS)
	$(CC) $(PROF_LD) -shared -o $@ $^ $(CUDA_LDFLAGS)

$(PRELOAD_LIB): $(OBJS)
	$(CC) $(PROF_LD) -shared -o $@ $^ $(CUDA_LDFLAGS)

# ---- compile (with automatic header-dependency tracking) -------------------
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR):
	@mkdir -p $@

# ---- harnesses -------------------------------------------------------------
# tiny-linked: rpath=$ORIGIN so a binary finds libtiny_cufile.so beside it from
# any working directory.
%: test/%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $(PROF_LD) $< -o $@ -L. -ltiny_cufile $(CUDA_LDFLAGS) $(PTHREAD) \
	    -Wl,-rpath,'$$ORIGIN'

# real-libcufile A/B twin: `foo_real` builds test/foo.c against genuine libcufile.
%_real: test/%.c
	$(CC) $(CFLAGS) $(PROF_LD) -DTCU_LIBLABEL='"real"' $< -o $@ \
	    -L$(CUFILE_LIBDIR) $(CUDA_LDFLAGS) -lcufile $(PTHREAD) \
	    -Wl,-rpath,$(CUFILE_LIBDIR)

# ---- housekeeping ----------------------------------------------------------
clean:
	rm -rf build obj $(LIB_NAME) $(PRELOAD_LIB) \
	       $(TEST_BINS) $(patsubst %,%_real,$(TEST_BINS)) trigger_raw_ioctl

help:
	@printf '%s\n' \
	  'tiny_cufile build targets:' \
	  '  make             release build of $(LIB_NAME) + $(PRELOAD_LIB)' \
	  '  make debug       -O0 -g3 -DTINY_CU_DEBUG (libs + harnesses)' \
	  '  make asan        + AddressSanitizer/UBSan' \
	  '  make tsan        + ThreadSanitizer (checks the per-chunk mutex)' \
	  '  make tests       build harnesses for the current profile' \
	  '  make <name>      build one harness (e.g. stress_threads)' \
	  '  make <name>_real build it against genuine libcufile (A/B)' \
	  '  make clean       remove build/ and outputs' \
	  '' \
	  'Knobs: BUILD=release|debug|asan|tsan  STRICT=1  CUDA_HOME=  CUDA_ARCH=' \
	  '       TCU_DEFAULT_MAX_CHUNK_MB=N (bake default chunk size)'

.PHONY: all tests release debug asan tsan clean help

-include $(DEPS)
