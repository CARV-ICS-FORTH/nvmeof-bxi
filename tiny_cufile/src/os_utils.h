#ifndef OS_UTILS_H
#define OS_UTILS_H

#include <stdint.h>
#include <stdio.h>

int os_get_bdev_maj_min(int fd, uint32_t *majdev, uint32_t *mindev);
int os_get_gpu_pdevinfo(const void *devPtr, uint64_t *pdevinfo);
size_t os_probe_nvfs_chunk_limit(int nvfs_fd);
#endif /* OS_UTILS_H */