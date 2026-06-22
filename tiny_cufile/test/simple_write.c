#define _GNU_SOURCE
#include "../include/tiny_cufile.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE (1024 * 1024 * 200) // 1MB test payload

int main() {
  // 1. Initialize API
  CUfileError_t status = tiny_cu_init();
  if (IS_CUFILE_ERR(status.err)) {
    fprintf(stderr, "Failed to init API: %s\n", CUFILE_ERRSTR(status.err));
    return -1;
  }

  // 2. Open Target File on NVMe

  int nvme_fd = open("/mnt/nvme_test/gds_file.dat", O_RDWR | O_DIRECT);
  if (nvme_fd < 0) {
    perror("Failed to open NVMe file");
    tiny_cu_cleanup();
    return -1;
  }

  // 2.5 We register the handle and the descriptor

  tcufile_descr_t descr;
  descr.fd = nvme_fd;
  tcufile_handle_t handle;
  tiny_cu_file_register(&handle, &descr);

  // 3. Allocate and prep host data
  unsigned char *h_payload = malloc(BUF_SIZE);
  unsigned char *h_verify = malloc(BUF_SIZE);
  memset(h_payload, 0x66, BUF_SIZE);
  memset(h_verify, 0x00, BUF_SIZE);

  snprintf(h_payload, BUF_SIZE, "hey im working");

  // 4. Allocate GPU data and copy payload
  void *d_buf;
  if (cudaMalloc(&d_buf, BUF_SIZE) != cudaSuccess) {
    fprintf(stderr, "cudaMalloc failed\n");
    goto cleanup;
  }
  cudaMemcpy(d_buf, h_payload, BUF_SIZE, cudaMemcpyHostToDevice);
  printf("[+] Injected pattern into GPU VRAM at %p\n", d_buf);

  // 5. Register Buffer with TinyCuFile
  CUfileError_t buf_status = tiny_cu_buf_register(d_buf, BUF_SIZE);
  if (IS_CUFILE_ERR(buf_status.err)) {
    fprintf(stderr, "Failed to register buffer: %s\n", CUFILE_ERRSTR(buf_status.err));
    goto cleanup;
  }
  printf("[+] Buffer registered successfully\n");

  // 6. Write (GPU -> NVMe)
  printf("[+] Firing tiny_cu_write...\n");
  ssize_t w_ret = tiny_cu_write(handle, d_buf, BUF_SIZE, 0, 0);
  if (w_ret < 0) {
    if (IS_CUFILE_ERR(w_ret)) {
      fprintf(stderr, "tiny_cu_write failed: %s\n", CUFILE_ERRSTR(w_ret));
    } else {
      fprintf(stderr, "tiny_cu_write failed with POSIX error code: %ld\n", w_ret);
    }
  } else {
    printf("[+] tiny_cu_write complete!\n");
  }

  // Sabotage GPU memory to prove read accuracy
  cudaMemset(d_buf, 0x00, BUF_SIZE);

  // 7. Read (NVMe -> GPU)
  printf("[+] Firing tiny_cu_read...\n");
  ssize_t r_ret = tiny_cu_read(handle, d_buf, BUF_SIZE, 0, 0);
  if (r_ret < 0) {
    if (IS_CUFILE_ERR(r_ret)) {
      fprintf(stderr, "tiny_cu_read failed: %s\n", CUFILE_ERRSTR(r_ret));
    } else {
      fprintf(stderr, "tiny_cu_read failed with POSIX error code: %ld\n", r_ret);
    }
  } else {
    printf("[+] tiny_cu_read complete!\n");
  }

  // 8. Verify
  cudaMemcpy(h_verify, d_buf, BUF_SIZE, cudaMemcpyDeviceToHost);
  if (memcmp(h_payload, h_verify, BUF_SIZE) == 0) {
    printf("\n======================================================\n");
    printf("[SUCCESS] DATA VERIFIED! Hardware loopback matches perfectly.\n");
    printf("======================================================\n");
  } else {
    printf("\n======================================================\n");
    printf("[FAILURE] CORRUPTION DETECTED. Memory does not match!\n");
    printf("======================================================\n");
  }
  // Print the string we read back from the GPU!
  printf("\n[RESULT] Read from disk: '%s'\n", h_verify);
  if (strcmp(h_payload, h_verify) == 0) {
    printf("[SUCCESS] Strings match perfectly!\n");
  } else {
    printf("[FAILURE] Strings do not match!\n");
  }

  // 9. Deregister Buffer
  tiny_cu_buf_dereg(d_buf);

cleanup:
  if (d_buf)
    cudaFree(d_buf);
  free(h_payload);
  free(h_verify);
  tiny_cu_file_dereg(handle);
  close(nvme_fd);
  tiny_cu_cleanup();

  return 0;
}
