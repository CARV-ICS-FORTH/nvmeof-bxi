#include "os_utils.h"
#include "log.h"
#include <cuda_runtime.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

int os_get_bdev_maj_min(int fd, uint32_t *majdev, uint32_t *mindev) {
  struct stat st;
  if (fstat(fd, &st) < 0) {
    TCU_ERR("fstat failed on fd %d", fd);
    return -1;
  }

  // If the user passed a direct block device (e.g. /dev/nvme0n1)
  if (S_ISBLK(st.st_mode)) {
    *majdev = major(st.st_rdev);
    *mindev = minor(st.st_rdev);
  }
  // If the user passed a regular file on a filesystem
  else {
    *majdev = major(st.st_dev);
    *mindev = minor(st.st_dev);
  }

  TCU_DEBUG("Resolved block device maj:%u min:%u for fd %d", *majdev, *mindev,
            fd);
  return 0;
}

int os_get_gpu_pdevinfo(const void *devPtr, uint64_t *pdevinfo) {
  struct cudaPointerAttributes attrs;
  cudaError_t err = cudaPointerGetAttributes(&attrs, devPtr);
  if (err != cudaSuccess) {
    TCU_ERR("cudaPointerGetAttributes failed: %s", cudaGetErrorString(err));
    return -1;
  }

  int device_id = attrs.device;
  char pciBusId[20];
  err = cudaDeviceGetPCIBusId(pciBusId, sizeof(pciBusId), device_id);
  if (err != cudaSuccess) {
    TCU_ERR("cudaDeviceGetPCIBusId failed: %s", cudaGetErrorString(err));
    return -1;
  }

  unsigned int dom, b, d, f;
  if (sscanf(pciBusId, "%x:%x:%x.%x", &dom, &b, &d, &f) != 4) {
    TCU_ERR("Failed to parse PCI Bus ID: %s", pciBusId);
    return -1;
  }

  *pdevinfo = ((uint64_t)dom << 32) | (b << 8) | (d << 3) | f;
  TCU_DEBUG("Resolved GPU %d (Bus %s) to pdevinfo 0x%lx", device_id, pciBusId,
            *pdevinfo);

  return 0;
}
