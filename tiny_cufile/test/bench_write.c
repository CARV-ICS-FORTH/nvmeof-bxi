// A/B latency harness for the official cuFile API.
// Build twice from the SAME source:
//   bench_real  -> linked against genuine NVIDIA  -lcufile
//   bench_tiny  -> linked against our             -ltiny_cufile
// Identical workload; only the library differs.
//
// Usage: ./bench_xxx [size_mb=32] [iters=200] [path=/gds_test.dat]
#define _GNU_SOURCE
#include "../cufile.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}

static void report(const char *label, double *s, int n, size_t bytes) {
  qsort(s, n, sizeof(double), cmp_double);
  double sum = 0;
  for (int i = 0; i < n; i++)
    sum += s[i];
  double mean = sum / n;
  double med = s[n / 2];
  double mn = s[0];
  double p99 = s[(int)(n * 0.99)];
  double gbps = (double)bytes /
                (med * 1e3); // bytes/us -> GB/s (1e3 = us->ns per GB scaling)
  printf("  %-6s  min %8.1f us   median %8.1f us   mean %8.1f us   p99 %8.1f "
         "us   (%.2f GB/s @ median)\n",
         label, mn, med, mean, p99, gbps);
}

int main(int argc, char **argv) {
  size_t size_mb = (argc > 1) ? (size_t)atol(argv[1]) : 32;
  int iters = (argc > 2) ? atoi(argv[2]) : 200;
  const char *path = (argc > 3) ? argv[3] : "/tmp/gds_test.dat";
  size_t size = size_mb * 1024 * 1024;

  printf("=== bench: %zu MB x %d iters, file=%s ===\n", size_mb, iters, path);

  CUfileError_t st = cuFileDriverOpen();
  if (st.err != CU_FILE_SUCCESS) {
    fprintf(stderr, "cuFileDriverOpen failed: err=%d cu_err=%d\n", st.err,
            st.cu_err);
    return 1;
  }

  int fd = open(path, O_CREAT | O_RDWR | O_DIRECT, 0664);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  if (fallocate(fd, 0, 0, size) != 0)
    perror("fallocate (continuing)");

  CUfileDescr_t descr;
  memset(&descr, 0, sizeof(descr));
  descr.handle.fd = fd;
  descr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
  CUfileHandle_t h;
  st = cuFileHandleRegister(&h, &descr);
  if (st.err != CU_FILE_SUCCESS) {
    fprintf(stderr, "cuFileHandleRegister failed: err=%d\n", st.err);
    return 1;
  }

  void *d_buf = NULL;
  if (cudaMalloc(&d_buf, size) != cudaSuccess) {
    fprintf(stderr, "cudaMalloc failed\n");
    return 1;
  }
  cudaMemset(d_buf, 0xAB, size);

  st = cuFileBufRegister(d_buf, size, 0);
  if (st.err != CU_FILE_SUCCESS) {
    fprintf(stderr, "cuFileBufRegister failed: err=%d\n", st.err);
    return 1;
  }

  double *ws = malloc(iters * sizeof(double));
  double *rs = malloc(iters * sizeof(double));

  // ---- WRITE ----
  ssize_t r = cuFileWrite(h, d_buf, size, 0, 0); // warmup
  if (r != (ssize_t)size)
    fprintf(stderr, "WARN: warmup write returned %zd (expected %zu)\n", r,
            size);
  for (int i = 0; i < iters; i++) {
    double t0 = now_us();
    cuFileWrite(h, d_buf, size, 0, 0);
    ws[i] = now_us() - t0;
  }

  // ---- READ ----
  cuFileRead(h, d_buf, size, 0, 0); // warmup
  for (int i = 0; i < iters; i++) {
    double t0 = now_us();
    cuFileRead(h, d_buf, size, 0, 0);
    rs[i] = now_us() - t0;
  }

  printf("Results (%zu MB per op):\n", size_mb);
  report("write", ws, iters, size);
  report("read", rs, iters, size);

  cuFileBufDeregister(d_buf);
  cudaFree(d_buf);
  cuFileHandleDeregister(h);
  close(fd);
  cuFileDriverClose();
  free(ws);
  free(rs);
  return 0;
}
