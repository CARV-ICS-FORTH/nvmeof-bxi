CC = gcc
DBFLAGS = -DTINY_CU_DEBUG
CFLAGS = -Wall -Wextra -fPIC -I./include -I/usr/local/cuda/include 
LDFLAGS = -shared
CUDA_LDFLAGS = -L/usr/local/cuda/lib64 -lcudart

SRC_DIR = src
OBJ_DIR = obj
INC_DIR = include
LIB_NAME = libtiny_cufile.so

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

PRELOAD_LIB = ldp_libcufile.so

all: $(LIB_NAME) $(PRELOAD_LIB) simple_write

$(LIB_NAME): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(CUDA_LDFLAGS)

$(PRELOAD_LIB): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(CUDA_LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) $(DBFLAGS) -c $< -o $@

simple_write: test/simple_write.c $(LIB_NAME)
	$(CC) $(CFLAGS) $< -o $@ -L. -ltiny_cufile $(CUDA_LDFLAGS) -Wl,-rpath=.

clean:
	rm -rf $(OBJ_DIR) $(LIB_NAME) $(PRELOAD_LIB) simple_write
