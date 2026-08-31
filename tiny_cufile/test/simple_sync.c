/*
 * simple_sync -- synchronous GPUDirect round-trip verifier.
 *
 * Writes a known, offset-dependent pattern from GPU memory to disk with
 * cuFileWrite, wipes the GPU buffer, reads it back with cuFileRead, and
 * compares byte-for-byte. Exercises the synchronous path (tiny_cu_write /
 * tiny_cu_read) through the public cuFile* shim.
 *
 * Usage: simple_sync [path] [size_MB]   (see -h)
 */
#define _GNU_SOURCE
#include "../cufile.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MB (1024UL * 1024UL)
#define DEFAULT_PATH "/mnt/nvme_test/gds_sync.dat"
#define DEFAULT_MB 32

/* Offset-dependent byte: any truncation, misplacement, or stale chunk shows up
 * as a mismatch at a specific offset. */
static inline unsigned char patbyte(size_t i) {
  return (unsigned char)(((uint32_t)i * 2654435761u) >> 24);
}

#define CK(call)                                                               \
  do {                                                                         \
    cudaError_t e = (call);                                                    \
    if (e != cudaSuccess) {                                                    \
      fprintf(stderr, "CUDA fail %s: %s\n", #call, cudaGetErrorString(e));     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static void usage(const char *prog) {
  fprintf(stderr,
          "Synchronous GPUDirect round-trip verifier (cuFileWrite/cuFileRead).\n"
          "\n"
          "Usage: %s [path] [size_MB]\n"
          "  path       O_DIRECT-capable target file (default: %s)\n"
          "  size_MB    payload size in MiB (default: %d)\n"
          "  -h,--help  show this help\n"
          "\n"
          "Writes a known pattern GPU->disk, wipes the GPU buffer, reads it\n"
          "back, and compares byte-for-byte.\n"
          "Exit: 0 = verified, 2 = mismatch, 1 = setup error.\n",
          prog, DEFAULT_PATH, DEFAULT_MB);
}

int main(int argc, char **argv) {
  const char *path = DEFAULT_PATH;
  size_t size_mb = DEFAULT_MB;

  /* Positional args: [path] [size_MB]; -h/--help anywhere prints usage. */
  int pos = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
      usage(argv[0]);
      return 1;
    }
    if (pos == 0) {
      path = argv[i];
    } else if (pos == 1) {
      size_mb = strtoull(argv[i], NULL, 10);
    } else {
      fprintf(stderr, "Too many arguments.\n\n");
      usage(argv[0]);
      return 1;
    }
    pos++;
  }
  if (size_mb == 0) {
    fprintf(stderr, "size_MB must be > 0.\n\n");
    usage(argv[0]);
    return 1;
  }
  size_t SZ = size_mb * MB;
  printf("[*] path=%s  size=%zu MB\n", path, size_mb);

  CUfileError_t st = cuFileDriverOpen();
  if (st.err != CU_FILE_SUCCESS) {
    fprintf(stderr, "cuFileDriverOpen failed (%d)\n", st.err);
    return 1;
  }

  int fd = open(path, O_CREAT | O_RDWR | O_DIRECT, 0666);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  CUfileDescr_t descr;
  memset(&descr, 0, sizeof(descr));
  descr.handle.fd = fd;
  descr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
  CUfileHandle_t h;
  cuFileHandleRegister(&h, &descr);

  unsigned char *h_pat, *h_ver;
  CK(cudaMallocHost((void **)&h_pat, SZ));
  CK(cudaMallocHost((void **)&h_ver, SZ));
  for (size_t i = 0; i < SZ; i++)
    h_pat[i] = patbyte(i);
  memset(h_ver, 0xAA, SZ); /* distinct from data and from the 0xFF sabotage */

  void *d_buf;
  CK(cudaMalloc(&d_buf, SZ));
  cuFileBufRegister(d_buf, SZ, 0);

  /* 1. fill GPU buffer with the known pattern */
  CK(cudaMemcpy(d_buf, h_pat, SZ, cudaMemcpyHostToDevice));

  /* 2. write GPU -> disk */
  ssize_t wb = cuFileWrite(h, d_buf, SZ, 0, 0);
  printf("[+] cuFileWrite returned %zd (expected %zu)\n", wb, SZ);

  /* 3. sabotage: wipe the GPU buffer so the read must repopulate it */
  CK(cudaMemset(d_buf, 0xFF, SZ));

  /* 4. read disk -> GPU */
  ssize_t rb = cuFileRead(h, d_buf, SZ, 0, 0);
  printf("[+] cuFileRead  returned %zd (expected %zu)\n", rb, SZ);

  /* 5. copy GPU -> host and compare */
  CK(cudaMemcpy(h_ver, d_buf, SZ, cudaMemcpyDeviceToHost));

  size_t bad = (size_t)-1;
  for (size_t i = 0; i < SZ; i++) {
    if (h_ver[i] != h_pat[i]) {
      bad = i;
      break;
    }
  }

  int rc;
  printf("=====================================================\n");
  if (bad == (size_t)-1) {
    printf("[SUCCESS] full %zu-byte round-trip verified byte-for-byte.\n", SZ);
    rc = 0;
  } else {
    printf("[FAILURE] first mismatch at offset %zu: disk=0x%02x expected=0x%02x\n",
           bad, h_ver[bad], h_pat[bad]);
    rc = 2;
  }
  printf("=====================================================\n");

  cuFileBufDeregister(d_buf);
  cudaFree(d_buf);
  cudaFreeHost(h_pat);
  cudaFreeHost(h_ver);
  cuFileHandleDeregister(h);
  close(fd);
  cuFileDriverClose();
  return rc;
}
