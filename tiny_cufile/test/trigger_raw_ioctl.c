#define _GNU_SOURCE

#include <cuda_runtime.h>
#include <driver_types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define NVFS_MAGIC 't'
#define NVFS_IOCTL_MAP _IOW(NVFS_MAGIC, 3, int)
#define NVFS_IOCTL_WRITE _IOW(NVFS_MAGIC, 4, int)
#define NVFS_IOCTL_READ _IOW(NVFS_MAGIC, 2, int)

typedef struct nvfs_ioctl_map_s {
  int64_t size;
  uint64_t pdevinfo;
  uint64_t cpuvaddr;
  uint64_t gpuvaddr;
  uint64_t end_fence_addr;
  uint32_t sbuf_block;
  uint16_t is_bounce_buffer;
  uint8_t padding[2];
} __attribute__((packed, aligned(8))) nvfs_ioctl_map_t;

typedef struct nvfs_file_args {
  unsigned long inum;
  uint32_t generation;
  uint32_t majdev;
  uint32_t mindev;
  uint64_t devptroff;
} __attribute__((packed, aligned(8))) nvfs_file_args_t;

typedef struct nvfs_ioctl_ioargs {
  uint64_t cpuvaddr;
  off_t offset;
  uint64_t size;
  uint64_t end_fence_value;
  int64_t ioctl_return;
  nvfs_file_args_t file_args;
  int fd;
  uint8_t sync : 1;
  uint8_t hipri : 1;
  uint8_t allowreads : 1;
  uint8_t use_rkeys : 1;
  uint8_t optype : 3; // 1 for WRITE, 0 for READ
  uint8_t reserved : 1;
  uint8_t padding[3];
} __attribute__((packed, aligned(8))) nvfs_ioctl_ioargs_t;

union nvfs_ioctl_param_u {
  nvfs_ioctl_map_t map_args;
  nvfs_ioctl_ioargs_t ioargs;
} __attribute__((packed, aligned(8)));

#define BUF_SIZE (1024 * 1024) // 1MB test payload

int main() {
  int nvfs_fd, nvme_fd;
  void *d_buf;
  void *shadow_buf;
  volatile uint64_t *
      fence_page; // Marked volatile so the CPU actually polls memory, not cache
  union nvfs_ioctl_param_u param;
  struct stat st;
  char pciBusId[20];
  unsigned int dom, b, d, f;

  // Host buffers for data generation and verification
  unsigned char *h_payload = malloc(BUF_SIZE);
  unsigned char *h_verify = malloc(BUF_SIZE);

  // Fill host payload with a known repeating pattern (0x65)
  memset(h_payload, 0x66, BUF_SIZE);
  memset(h_verify, 0x00, BUF_SIZE);

  nvfs_fd = open("/dev/nvidia-fs0", O_RDWR);
  // opening a file on the nvme0n1
  nvme_fd = open("/mnt/nvme_test/gds_file.dat", O_RDWR | O_DIRECT);

  // stat block device to get correct major/minor
  struct stat bdev_st;
  stat("/dev/nvme0n1", &bdev_st);

  if (nvfs_fd < 0 || nvme_fd < 0) {
    perror("[-] Failed to open devices");
    return -1;
  }

  fstat(nvme_fd, &st);

  // 1. Allocate VRAM and Inject the Pattern
  cudaMalloc(&d_buf, BUF_SIZE);
  if (cudaMemcpy(d_buf, h_payload, BUF_SIZE, cudaMemcpyHostToDevice) !=
      cudaSuccess)
    goto cleanup;
  printf("[+] Injected pattern into 1MB GPU VRAM at %p\n", d_buf);

  // 2. Allocate Native Shadow Buffer & Synchronization Fence
  shadow_buf =
      mmap(NULL, BUF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, nvfs_fd, 0);
  cudaMallocHost((void **)&fence_page, 4096);
  memset((void *)fence_page, 0, 4096);
  *fence_page = 0; // Initialize mailbox

  cudaDeviceGetPCIBusId(pciBusId, sizeof(pciBusId), 0);
  sscanf(pciBusId, "%x:%x:%x.%x", &dom, &b, &d, &f);
  uint64_t pdevinfo = ((uint64_t)dom << 32) | (b << 8) | (d << 3) | f;

  // ==========================================
  // STAGE 1: MAP THE MEMORY
  // ==========================================
  memset(&param, 0, sizeof(param));
  param.map_args.size = BUF_SIZE;
  param.map_args.pdevinfo = pdevinfo;
  param.map_args.cpuvaddr = (uint64_t)shadow_buf;
  param.map_args.gpuvaddr = (uint64_t)d_buf;
  param.map_args.end_fence_addr = (uint64_t)fence_page;
  param.map_args.sbuf_block = BUF_SIZE / 4096;
  param.map_args.is_bounce_buffer = 0;

  if (ioctl(nvfs_fd, NVFS_IOCTL_MAP, &param) < 0) {
    perror("[-] STAGE 1 MAP IOCTL failed");
    return -1;
  }
  printf("[+] STAGE 1 SUCCESS! Memory registered.\n");

  // ==========================================
  // STAGE 2: THE WRITE CYCLE
  // ==========================================
  memset(&param, 0, sizeof(param));
  param.ioargs.cpuvaddr = (uint64_t)shadow_buf;
  param.ioargs.offset = 0; // Writing to block 0
  param.ioargs.size = BUF_SIZE;
  param.ioargs.fd = nvme_fd;
  param.ioargs.end_fence_value =
      1; // Tell kernel to write '1' when write is done

  // Setup file args using the block device's major/minor numbers
  // This is required when operating on a file inside the block device
  param.ioargs.file_args.inum = st.st_ino;
  param.ioargs.file_args.majdev = major(bdev_st.st_rdev);
  param.ioargs.file_args.mindev = minor(bdev_st.st_rdev);

  param.ioargs.optype = 1; // 1 = WRITE
  param.ioargs.sync = 1;

  printf("[+] Firing STAGE 2: NVFS_IOCTL_WRITE...\n");
  int ioctl_ret = ioctl(nvfs_fd, NVFS_IOCTL_WRITE, &param);

  if (ioctl_ret < 0) {
    printf("[-] STAGE 2 WRITE IOCTL failed. return=%d, errno=%d (%s)\n",
           ioctl_ret, errno, strerror(errno));
    printf("[-] nvidia-fs internal return: %ld\n", param.ioargs.ioctl_return);
  } else {
    printf("[+] STAGE 2 SUCCESS! NVMe Write Physically Complete.\n");
  }

  // Optional: Asynchronous Hardware Spin-Wait
  // If sync=0, you would poll *fence_page for the end_fence_value here.
  // We are using sync=1, so the ioctl blocks until complete.

  // ==========================================
  // STAGE 3: THE SABOTAGE (CLEAR VRAM)
  // ==========================================
  if (cudaMemset(d_buf, 0x00, BUF_SIZE) != cudaSuccess)
    goto cleanup;
  printf("[+] SABOTAGE: GPU VRAM wiped to 0x00 to prove read accuracy.\n");

  // ==========================================
  // STAGE 4: THE READ CYCLE
  // ==========================================
  *fence_page = 0; // Reset the mailbox

  // Note: If IOCTL 5 throws an "Invalid argument" error, change NVFS_IOCTL_READ
  // back to _IOW(NVFS_MAGIC, 4, int) at the top of the file. Some versions of
  // nvidia-fs use IOCTL 4 as a universal "DO_IO" command and rely solely on
  // `optype` to determine direction.

  param.ioargs.optype = 0; // 0 = READ
  param.ioargs.end_fence_value =
      2; // Tell kernel to write '2' when read is done

  printf("[+] Firing STAGE 4: NVFS_IOCTL_READ...\n");
  if (ioctl(nvfs_fd, NVFS_IOCTL_READ, &param) < 0) {
    perror("[-] STAGE 4 READ IOCTL failed");
    return -1;
  }

  // Optional: Asynchronous Hardware Spin-Wait
  // If sync=0, you would poll *fence_page here.
  printf("[+] STAGE 4 SUCCESS! NVMe Read Physically Complete.\n");

  // ==========================================
  // STAGE 5: DATA VERIFICATION
  // ==========================================
  if (cudaMemcpy(h_verify, d_buf, BUF_SIZE, cudaMemcpyDeviceToHost) !=
      cudaSuccess)
    goto cleanup;

  if (memcmp(h_payload, h_verify, BUF_SIZE) == 0) {
    printf("\n======================================================\n");
    printf("[SUCCESS] DATA VERIFIED! Hardware loopback matches perfectly.\n");
    printf("======================================================\n");
  } else {
    printf("\n======================================================\n");
    printf("[FAILURE] CORRUPTION DETECTED. Memory does not match!\n");
    printf("======================================================\n");
  }

cleanup:
  // Cleanup
  cudaFreeHost((void *)fence_page);
  munmap(shadow_buf, BUF_SIZE);
  close(nvme_fd);
  close(nvfs_fd);
  cudaFree(d_buf);
  free(h_payload);
  free(h_verify);

  return 0;
}
