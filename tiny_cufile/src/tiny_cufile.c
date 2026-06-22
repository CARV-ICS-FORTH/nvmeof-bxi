#include "../include/tiny_cufile.h"
#include "log.h"
#include "nvfs_ioctl.h"
#include "os_utils.h"
#include "state.h"

#include <cuda_runtime.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_CHUNK_GPU (16 * 1024 * 1024)

typedef struct {
  int fd;
  uint32_t majdev;
  uint32_t mindev;
  unsigned long inum;
} tcufile_handle_internal_t;

static int g_nvfs_fd = -1;

CUfileError_t tiny_cu_init(void) {
  CUfileError_t status;

  if (g_nvfs_fd >= 0) {
    TCU_INFO("Already initialized.");
    status.err = CU_FILE_DRIVER_ALREADY_OPEN;
    return status;
  }

  g_nvfs_fd = open("/dev/nvidia-fs0", O_RDWR);
  if (g_nvfs_fd < 0) {
    TCU_ERR("Failed to open /dev/nvidia-fs0: %s", strerror(errno));
    status.err = CU_FILE_NVFS_INTERNAL_DRIVER_ERROR;
    return status;
  }

  state_init();
  TCU_INFO("TinyCuFile initialized successfully.");
  status.err = CU_FILE_SUCCESS;
  return status;
}

CUfileError_t tiny_cu_cleanup(void) {
  CUfileError_t status;
  status.err = CU_FILE_SUCCESS;
  status.cu_err = 0; // CUDA_SUCCESS

  if (g_nvfs_fd >= 0) {
    state_cleanup();
    close(g_nvfs_fd);
    g_nvfs_fd = -1;
    TCU_INFO("TinyCuFile cleaned up.");
  }
  return status;
}

CUfileError_t tiny_cu_file_register(tcufile_handle_t *fh,
                                    tcufile_descr_t *descr) {
  tcufile_handle_internal_t *internal =
      malloc(sizeof(tcufile_handle_internal_t));

  internal->fd = descr->fd;
  os_get_bdev_maj_min(descr->fd, &internal->majdev, &internal->mindev);

  struct stat st;
  fstat(descr->fd, &st);
  internal->inum = st.st_ino;

  *fh = (tcufile_handle_t)internal;

  CUfileError_t status;
  status.err = CU_FILE_SUCCESS;
  status.cu_err = 0;
  return status;
}

void tiny_cu_file_dereg(tcufile_handle_t fh) {
  tcufile_handle_internal_t *internal = (tcufile_handle_internal_t *)fh;
  free(internal);
}

CUfileError_t tiny_cu_buf_register(const void *buf, size_t size) {
  CUfileError_t status;
  status.cu_err = 0;

  if (g_nvfs_fd < 0) {
    TCU_ERR("Library not initialized. Call tiny_cu_init() first.");
    status.err = CU_FILE_DRIVER_NOT_INITIALIZED;
    return status;
  }

  uint64_t pdevinfo;
  if (os_get_gpu_pdevinfo(buf, &pdevinfo) < 0) {
    status.err = CU_FILE_CUDA_POINTER_INVALID;
    return status;
  }

  // we split the total size into 16MB chunks, just in case we have more than
  // that
  int num_chunks = (size + MAX_CHUNK_GPU - 1) / MAX_CHUNK_GPU;
  tcufile_chunk_t *chunks = malloc(sizeof(tcufile_chunk_t) * num_chunks);
  for (int i = 0; i < num_chunks; i++) {
    size_t current_offs = i * MAX_CHUNK_GPU;
    size_t chunk_size = (size - current_offs < MAX_CHUNK_GPU)
                            ? (size - current_offs)
                            : MAX_CHUNK_GPU;
    // allocate shadow buffer
    chunks[i].length = chunk_size;
    void *shadow_buf = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, g_nvfs_fd, 0);
    if (shadow_buf == MAP_FAILED) {
      TCU_ERR("Failed to map shadow buffer: %s", strerror(errno));
      status.err = CU_FILE_NVFS_INTERNAL_DRIVER_ERROR;
      return status;
    }
    chunks[i].shadow_buf = shadow_buf;
    // allocate fence Page

    volatile uint64_t *fence_page;
    if (cudaMallocHost((void **)&fence_page, 4096) != cudaSuccess) {
      TCU_ERR("Failed to allocate pinned host memory for fence_page");
      munmap(shadow_buf, chunk_size);
      status.err = CU_FILE_CUDA_DRIVER_ERROR;
      status.cu_err = 2; // CUDA_ERROR_OUT_OF_MEMORY (approx)
      return status;
    }
    memset((void *)fence_page, 0, 4096);
    chunks[i].fence_page = fence_page;

    // prepare and fire MAP ioctl

    union nvfs_ioctl_param_u param;
    memset(&param, 0, sizeof(param));
    param.map_args.size = chunk_size;
    param.map_args.pdevinfo = pdevinfo;
    param.map_args.cpuvaddr = (uint64_t)shadow_buf;
    param.map_args.gpuvaddr = (uint64_t)((char *)buf + current_offs);
    param.map_args.end_fence_addr = (uint64_t)fence_page;
    param.map_args.is_bounce_buffer = 0;
    param.map_args.sbuf_block = chunk_size / 4096;

    if (ioctl(g_nvfs_fd, NVFS_IOCTL_MAP, &param) < 0) {
      TCU_ERR("NVFS_IOCTL_MAP failed: %s", strerror(errno));
      cudaFreeHost((void *)fence_page);
      munmap(shadow_buf, chunk_size);
      status.err = CU_FILE_NVFS_DRIVER_ERROR;
      return status;
    }
  }

  // 4. Save to State Manager
  if (state_add_buf(buf, size, chunks, num_chunks, pdevinfo) < 0) {

    for (int i = 0; i < num_chunks; i++) {
      munmap(chunks[i].shadow_buf, chunks[i].length);
      cudaFreeHost((void *)chunks[i].fence_page);
    }
    status.err = CU_FILE_INTERNAL_ERROR;
    return status;
  }

  TCU_DEBUG("Registered buf %p (size %zu)", buf, size);
  status.err = CU_FILE_SUCCESS;
  return status;
}

CUfileError_t tiny_cu_buf_dereg(const void *buf) {
  CUfileError_t status;
  status.cu_err = 0;

  tcufile_buf_t *found = state_find_buf(buf);
  if (!found) {
    TCU_ERR("Cannot deregister: Buffer %p not found.", buf);
    status.err = CU_FILE_MEMORY_NOT_REGISTERED;
    return status;
  }

  // Cleanup hardware mappings
  for (int i = 0; i < found->num_chunks; i++) {
    munmap(found->chunks[i].shadow_buf, found->chunks[i].length);
    cudaFreeHost((void *)found->chunks[i].fence_page);
  }

  // Remove from our state tracker
  state_remove_buf(buf);

  TCU_DEBUG("Deregistered buf %p", buf);
  status.err = CU_FILE_SUCCESS;
  return status;
}

ssize_t tiny_cu_write(tcufile_handle_t fh, const void *buf, size_t size,
                      off_t offset, off_t devPtr_offs) {
  if (g_nvfs_fd < 0)
    return -CU_FILE_DRIVER_NOT_INITIALIZED;

  tcufile_buf_t *buf_info = state_find_buf(buf);
  if (!buf_info) {
    TCU_ERR("Write failed: GPU buffer %p is not registered.", buf);
    return -CU_FILE_MEMORY_NOT_REGISTERED;
  }
  // tracking variables for the buffer
  size_t remaining = size;
  size_t current_file_offset = offset;
  size_t current_dev_offset = devPtr_offs;
  ssize_t bytes_transfered = 0;
  tcufile_handle_internal_t *internal = (tcufile_handle_internal_t *)fh;

  while (remaining > 0) {
    size_t curr_chunk = current_dev_offset / MAX_CHUNK_GPU;
    size_t curr_chunk_offs = current_dev_offset % MAX_CHUNK_GPU;

    size_t io_size = remaining;
    if (io_size > MAX_CHUNK_GPU - curr_chunk_offs)
      io_size = MAX_CHUNK_GPU - curr_chunk_offs;

    // now ready for ioctl
    *buf_info->chunks[curr_chunk].fence_page = 0;

    union nvfs_ioctl_param_u param;
    memset(&param, 0, sizeof(param));

    param.ioargs.cpuvaddr =
        (uint64_t)buf_info->chunks[curr_chunk].shadow_buf + curr_chunk_offs;
    param.ioargs.offset = current_file_offset;
    param.ioargs.size = io_size;
    param.ioargs.fd = internal->fd;
    param.ioargs.end_fence_value = 1;

    param.ioargs.file_args.inum = internal->inum;
    param.ioargs.file_args.majdev = internal->majdev;
    param.ioargs.file_args.mindev = internal->mindev;

    param.ioargs.optype = 1; // 1 = WRITE
    param.ioargs.sync = 1;   // Synchronous wait

    TCU_DEBUG("Firing NVFS_IOCTL_WRITE for fd=%d, buf=%p", internal->fd,
              buf_info->chunks[curr_chunk].shadow_buf);
    int ret = ioctl(g_nvfs_fd, NVFS_IOCTL_WRITE, &param);

    if (ret < 0) {
      TCU_ERR("Write IOCTL failed: %s (nvidia_fs code: %ld)", strerror(errno),
              param.ioargs.ioctl_return);
      return (bytes_transfered > 0 ? bytes_transfered : ret);
    }

    remaining -= io_size;
    current_file_offset += io_size;
    current_dev_offset += io_size;
    bytes_transfered += io_size;
  }
  return bytes_transfered;
}

ssize_t tiny_cu_read(tcufile_handle_t fh, void *buf, size_t size, off_t offset,
                     off_t devPtr_offs) {
  if (g_nvfs_fd < 0)
    return -CU_FILE_DRIVER_NOT_INITIALIZED;

  tcufile_buf_t *buf_info = state_find_buf(buf);
  if (!buf_info) {
    TCU_ERR("Read failed: GPU buffer %p is not registered.", buf);
    return -CU_FILE_MEMORY_NOT_REGISTERED;
  }

  tcufile_handle_internal_t *internal = (tcufile_handle_internal_t *)fh;

  size_t remaining = size;
  size_t current_file_offset = offset;
  size_t current_dev_offset = devPtr_offs;
  ssize_t bytes_transfered = 0;

  while (remaining > 0) {
    size_t curr_chunk = current_dev_offset / MAX_CHUNK_GPU;
    size_t curr_chunk_offs = current_dev_offset % MAX_CHUNK_GPU;

    size_t io_size = remaining;
    if (io_size > MAX_CHUNK_GPU - curr_chunk_offs)
      io_size = MAX_CHUNK_GPU - curr_chunk_offs;

    // now ready for ioctl
    *buf_info->chunks[curr_chunk].fence_page = 0;

    union nvfs_ioctl_param_u param;
    memset(&param, 0, sizeof(param));

    param.ioargs.cpuvaddr =
        (uint64_t)buf_info->chunks[curr_chunk].shadow_buf + curr_chunk_offs;
    param.ioargs.offset = current_file_offset;
    param.ioargs.size = io_size;
    param.ioargs.fd = internal->fd;
    param.ioargs.end_fence_value = 2;

    param.ioargs.file_args.inum = internal->inum;
    param.ioargs.file_args.majdev = internal->majdev;
    param.ioargs.file_args.mindev = internal->mindev;

    param.ioargs.optype = 0; // 0 = READ
    param.ioargs.sync = 1;   // Synchronous wait

    TCU_DEBUG("Firing NVFS_IOCTL_READ for fd=%d, buf=%p", internal->fd,
              buf_info->chunks[curr_chunk].shadow_buf);
    int ret = ioctl(g_nvfs_fd, NVFS_IOCTL_READ, &param);

    if (ret < 0) {
      TCU_ERR("Write IOCTL failed: %s (nvidia_fs code: %ld)", strerror(errno),
              param.ioargs.ioctl_return);
      return (bytes_transfered > 0 ? bytes_transfered : ret);
    }

    remaining -= io_size;
    current_file_offset += io_size;
    current_dev_offset += io_size;
    bytes_transfered += io_size;
  }
  return bytes_transfered;
}
