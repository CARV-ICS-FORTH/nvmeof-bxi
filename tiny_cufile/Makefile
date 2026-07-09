CC = gcc

# Opt-in debug logging: `make DEBUG=1` enables TCU_DEBUG/TCU_INFO tracing.
ifeq ($(DEBUG),1)
  DBFLAGS = -DTINY_CU_DEBUG
else
  DBFLAGS =
endif

CUDA_HOME ?= /usr/local/cuda

CFLAGS  = -Wall -Wextra -fPIC -I./include -I$(CUDA_HOME)/include
LDFLAGS = -shared
# -lcuda   : CUDA driver API (cuStreamWaitValue64 etc.) used by the async path
# -lcudart : CUDA runtime API (cudaMalloc/cudaLaunchHostFunc/...)
CUDA_LDFLAGS = -L$(CUDA_HOME)/lib64 -lcudart -lcuda

SRC_DIR = src
OBJ_DIR = obj
INC_DIR = include
LIB_NAME = libtiny_cufile.so

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# LD_PRELOAD shim: same objects, loaded ahead of a real app to intercept its
# cuFile* symbols and route them into tiny_cufile.
PRELOAD_LIB = ldp_libcufile.so

TESTS = simple_sync simple_async

all: $(LIB_NAME) $(PRELOAD_LIB) $(TESTS)

$(LIB_NAME): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(CUDA_LDFLAGS)

$(PRELOAD_LIB): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(CUDA_LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(DBFLAGS) -c $< -o $@

# Test/example binaries link against the shared lib (rpath=. for in-tree runs).
%: test/%.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -o $@ -L. -ltiny_cufile $(CUDA_LDFLAGS) -Wl,-rpath=.

clean:
	rm -rf $(OBJ_DIR) $(LIB_NAME) $(PRELOAD_LIB) $(TESTS)

.PHONY: all clean
