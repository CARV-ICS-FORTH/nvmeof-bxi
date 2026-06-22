#ifndef NVFS_IOCTL_H
#define NVFS_IOCTL_H

#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#ifndef NVFS_MAGIC
#define NVFS_MAGIC 't'
#define NVFS_IOCTL_MAP _IOW(NVFS_MAGIC, 3, int)
#define NVFS_IOCTL_WRITE _IOW(NVFS_MAGIC, 4, int)
#define NVFS_IOCTL_READ _IOW(NVFS_MAGIC, 2, int)
#endif

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

#endif // NVFS_IOCTL_H
