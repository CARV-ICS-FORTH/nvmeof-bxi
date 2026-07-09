/*
 * simple_async -- asynchronous (hardware-polling) GPUDirect verifier.
 *
 * Two modes:
 *   verify (default) -- write a known pattern GPU->disk with cuFileWriteAsync,
 *                       wipe the GPU buffer, read it back with cuFileReadAsync,
 *                       and compare byte-for-byte. Exercises the stream-ordered
 *                       fence path end to end, including multi-chunk accounting.
 *   error (-e)       -- issue a deliberately misaligned (rejected) async request
 *                       and check that it (a) does NOT deadlock -- a watchdog
 *                       guards this -- and (b) surfaces a negative byte count.
 *
 * The cuFile* calls route to tiny_cu_*_async via the preload shim.
 *
 * Usage: simple_async [-e|--error] [path] [size_MB]   (see -h)
 */
#define _GNU_SOURCE
#include "../cufile.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MB (1024UL * 1024UL)
#define DEFAULT_PATH "/mnt/nvme_test/gds_async.dat"
#define DEFAULT_MB 32
#define WATCHDOG_SECS 10

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
          "Asynchronous GPUDirect verifier (cuFileWriteAsync/cuFileReadAsync).\n"
          "\n"
          "Usage: %s [-e|--error] [path] [size_MB]\n"
          "  (default)    round-trip verify: pattern write, wipe, read, compare\n"
          "  -e,--error   run the error/no-deadlock negative test instead\n"
          "  path         O_DIRECT-capable target file (default: %s)\n"
          "  size_MB      payload size in MiB, verify mode only (default: %d)\n"
          "  -h,--help    show this help\n"
          "\n"
          "Exit: 0 = pass, 2 = wrong result, 3 = watchdog/deadlock,\n"
          "      1 = setup error.\n",
          prog, DEFAULT_PATH, DEFAULT_MB);
}

/* -------------------------------------------------------------------------- */
/* verify mode                                                                */
/* -------------------------------------------------------------------------- */
static int run_verify(const char *path, size_t size_mb) {
  size_t SZ = size_mb * MB;
  printf("[*] mode=verify  path=%s  size=%zu MB\n", path, size_mb);

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

  cudaStream_t s;
  CK(cudaStreamCreate(&s));

  /* 1. fill GPU buffer with the known pattern */
  CK(cudaMemcpyAsync(d_buf, h_pat, SZ, cudaMemcpyHostToDevice, s));

  /* 2. write GPU -> disk */
  size_t wsz = SZ;
  off_t wfo = 0, wbo = 0;
  ssize_t wb = 0;
  cuFileWriteAsync(h, d_buf, &wsz, &wfo, &wbo, &wb, s);

  /* 3. sabotage: wipe the GPU buffer. If the write DMA is not ordered ahead of
   *    this, on-disk data is garbage; 0xFF also proves the read repopulates. */
  CK(cudaMemsetAsync(d_buf, 0xFF, SZ, s));

  /* 4. read disk -> GPU */
  size_t rsz = SZ;
  off_t rfo = 0, rbo = 0;
  ssize_t rb = 0;
  cuFileReadAsync(h, d_buf, &rsz, &rfo, &rbo, &rb, s);

  /* 5. copy GPU -> host for verification */
  CK(cudaMemcpyAsync(h_ver, d_buf, SZ, cudaMemcpyDeviceToHost, s));

  printf("[*] main thread unblocked; syncing stream...\n");
  CK(cudaStreamSynchronize(s));

  printf("[+] bytes_written=%zd  bytes_read=%zd  (expected %zu)\n", wb, rb, SZ);
  int bytes_ok = (wb == (ssize_t)SZ && rb == (ssize_t)SZ);

  size_t bad = (size_t)-1;
  for (size_t i = 0; i < SZ; i++) {
    if (h_ver[i] != h_pat[i]) {
      bad = i;
      break;
    }
  }

  int rc;
  printf("=====================================================\n");
  if (bad == (size_t)-1 && bytes_ok) {
    printf("[SUCCESS] full %zu-byte round-trip verified byte-for-byte.\n", SZ);
    rc = 0;
  } else if (bad != (size_t)-1) {
    printf("[FAILURE] first mismatch at offset %zu: disk=0x%02x expected=0x%02x\n",
           bad, h_ver[bad], h_pat[bad]);
    rc = 2;
  } else {
    printf("[FAILURE] data matched but byte counts wrong "
           "(accounting bug across chunks).\n");
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
  cudaStreamDestroy(s);
  return rc;
}

/* -------------------------------------------------------------------------- */
/* error mode                                                                 */
/* -------------------------------------------------------------------------- */
static void watchdog(int sig) {
  (void)sig;
  const char *msg =
      "\n[FAILURE] watchdog fired: cudaStreamSynchronize did NOT return.\n"
      "          The rejected submit left the GPU's cuStreamWaitValue64\n"
      "          waiting on a fence that was never written -> DEADLOCK.\n";
  write(STDERR_FILENO, msg, strlen(msg));
  _exit(3);
}

static int run_error(const char *path) {
  printf("[*] mode=error  path=%s\n", path);

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

  size_t bufsz = 4 * MB;
  void *d_buf;
  CK(cudaMalloc(&d_buf, bufsz));
  CK(cudaMemset(d_buf, 0xAB, bufsz));
  cuFileBufRegister(d_buf, bufsz, 0);

  cudaStream_t s;
  CK(cudaStreamCreate(&s));

  /* Deliberately misaligned: O_DIRECT requires block-aligned (>=512) size and
   * offset, so an odd size at an odd offset is rejected synchronously. */
  size_t bad_size = 1023;
  off_t bad_file_off = 1;
  off_t buf_off = 0;
  ssize_t bytes = 12345; /* sentinel: must be overwritten to < 0 */

  printf("[*] issuing misaligned write (size=%zu, file_off=%ld) -> "
         "driver should reject the submit\n",
         bad_size, (long)bad_file_off);

  /* Arm the watchdog BEFORE the sync: a regression deadlocks there. */
  signal(SIGALRM, watchdog);
  alarm(WATCHDOG_SECS);

  size_t sz = bad_size;
  cuFileWriteAsync(h, d_buf, &sz, &bad_file_off, &buf_off, &bytes, s);

  printf("[*] main thread unblocked; syncing stream (watchdog: %ds)...\n",
         WATCHDOG_SECS);
  CK(cudaStreamSynchronize(s));
  alarm(0); /* made it back -> no deadlock */

  int rc;
  printf("=====================================================\n");
  printf("[+] stream returned (NO deadlock) -> fence published on failure.\n");
  printf("[+] bytes_written = %zd\n", bytes);
  if (bytes < 0) {
    printf("[SUCCESS] failure surfaced as a negative byte count (-errno).\n");
    rc = 0;
  } else if (bytes == 12345) {
    printf("[FAILURE] sentinel untouched -> finalize never ran for the chunk.\n");
    rc = 2;
  } else {
    printf("[FAILURE] expected bytes < 0; the driver accepted the misaligned\n"
           "          request or the error was not propagated.\n");
    rc = 2;
  }
  printf("=====================================================\n");

  cuFileBufDeregister(d_buf);
  cudaFree(d_buf);
  cuFileHandleDeregister(h);
  close(fd);
  cuFileDriverClose();
  cudaStreamDestroy(s);
  return rc;
}

int main(int argc, char **argv) {
  int error_mode = 0;
  const char *path = NULL;
  size_t size_mb = DEFAULT_MB;
  int pos = 0;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    }
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--error")) {
      error_mode = 1;
      continue;
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

  if (error_mode)
    return run_error(path ? path : DEFAULT_PATH);

  if (size_mb == 0) {
    fprintf(stderr, "size_MB must be > 0.\n\n");
    usage(argv[0]);
    return 1;
  }
  return run_verify(path ? path : DEFAULT_PATH, size_mb);
}
