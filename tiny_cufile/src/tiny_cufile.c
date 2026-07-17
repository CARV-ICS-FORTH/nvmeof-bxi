#include "../include/tiny_cufile.h"
#include "log.h"
#include "nvfs_ioctl.h"
#include "os_utils.h"
#include "state.h"

#include <bits/pthreadtypes.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Per-chunk registration size. Smaller chunks => more nvidia-fs mgroups per
 * registered buffer => concurrent I/O to different offsets of that buffer runs
 * in parallel (nvidia-fs serializes to one in-flight I/O per mgroup). Larger
 * chunks cut per-I/O ioctl overhead for single big transfers. The default
 * favors concurrency; override it (in MiB) with TINY_CU_MAX_CHUNK_MB. Resolved
 * at init by resolve_max_chunk_gpu(); this initializer is only a safe fallback
 * for any path that reads it before init. */
/* Compile-time default, overridable per build with
 * -DTCU_DEFAULT_MAX_CHUNK_MB=N; the TINY_CU_MAX_CHUNK_MB env var still overrides
 * it at runtime. */
#ifndef TCU_DEFAULT_MAX_CHUNK_MB
#define TCU_DEFAULT_MAX_CHUNK_MB 2
#endif
#define TCU_ENV_MAX_CHUNK "TINY_CU_MAX_CHUNK_MB"

static size_t max_chunk_gpu = (size_t)TCU_DEFAULT_MAX_CHUNK_MB * 1024 * 1024;
typedef struct {
  int fd;
  uint32_t majdev;
  uint32_t mindev;
  unsigned long inum;
} tcufile_handle_internal_t;

static int g_nvfs_fd = -1;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int init_ref = 0;
static CUfileError_t init_status = {.err = CU_FILE_SUCCESS};

/* nvidia-fs GPU_PAGE_SIZE. nvfs_io_init rejects an I/O whose GPU va_offset
 * is not GPU-page aligned unless it fits in the remainder of that page, so
 * unaligned starts must be sliced at the GPU-page boundary. */
#define TCU_GPU_PAGE_SIZE (64UL * 1024)

static size_t tcu_max_io_at(const void *base, size_t chunk_offs,
                            size_t io_size) {
  size_t va_page_offs =
      (((uintptr_t)base & (TCU_GPU_PAGE_SIZE - 1)) + chunk_offs) &
      (TCU_GPU_PAGE_SIZE - 1);
  if (va_page_offs && io_size > TCU_GPU_PAGE_SIZE - va_page_offs)
    io_size = TCU_GPU_PAGE_SIZE - va_page_offs;
  return io_size;
}

/* Resolve the per-chunk registration size (see TCU_DEFAULT_MAX_CHUNK_MB). Order:
 * env override TINY_CU_MAX_CHUNK_MB (MiB) if valid, else the compiled default;
 * the result is clamped to the largest mapping the driver accepts (probed) and
 * aligned down to the GPU page (min one page). Called once from do_tcu_init. */
static size_t resolve_max_chunk_gpu(int nvfs_fd) {
  size_t driver_max = os_probe_nvfs_chunk_limit(nvfs_fd);
  size_t chosen = (size_t)TCU_DEFAULT_MAX_CHUNK_MB * 1024 * 1024;

  const char *env = getenv(TCU_ENV_MAX_CHUNK);
  if (env != NULL && env[0] != '\0') {
    char *end = NULL;
    unsigned long mb = strtoul(env, &end, 10);
    if (end != env && *end == '\0' && mb > 0) {
      chosen = (size_t)mb * 1024 * 1024;
    } else {
      TCU_ERR("Ignoring invalid %s='%s' (expected a positive integer in MiB)",
              TCU_ENV_MAX_CHUNK, env);
    }
  }

  if (chosen > driver_max) {
    TCU_INFO("Clamping max_chunk_gpu %zu MiB -> driver limit %zu MiB",
             chosen / (1024 * 1024), driver_max / (1024 * 1024));
    chosen = driver_max;
  }

  chosen &= ~(TCU_GPU_PAGE_SIZE - 1); /* keep GPU-page aligned */
  if (chosen == 0)
    chosen = TCU_GPU_PAGE_SIZE;

  TCU_DEBUG("max_chunk_gpu = %zu KiB", chosen / 1024);
  return chosen;
}

static void do_tcu_init(void) {
  g_nvfs_fd = open("/dev/nvidia-fs0", O_RDWR);
  if (g_nvfs_fd < 0) {
    TCU_ERR("Failed to open /dev/nvidia-fs0: %s", strerror(errno));
    init_status.err = CU_FILE_NVFS_INTERNAL_DRIVER_ERROR;
    return;
  }

  max_chunk_gpu = resolve_max_chunk_gpu(g_nvfs_fd);
  state_init();

  TCU_INFO("TinyCuFile initialized successfully.");
}

CUfileError_t tiny_cu_init(void) {
  CUfileError_t status;

  pthread_mutex_lock(&init_mutex);
  if (init_ref == 0) {
    do_tcu_init();
  }

  if (g_nvfs_fd >= 0) {
    init_ref++;
    status.err = CU_FILE_SUCCESS;
  } else {
    status = init_status;
  }
  pthread_mutex_unlock(&init_mutex);

  return status;
}

CUfileError_t tiny_cu_cleanup(void) {
  CUfileError_t status;
  status.err = CU_FILE_SUCCESS;
  status.cu_err = 0; /* CUDA_SUCCESS */
  pthread_mutex_lock(&init_mutex);
  if (init_ref > 0) {
    init_ref--;
    if (init_ref == 0 && g_nvfs_fd >= 0) {
      state_cleanup();
      close(g_nvfs_fd);
      g_nvfs_fd = -1;
      TCU_INFO("TinyCuFile cleaned up.");
    }
  }
  pthread_mutex_unlock(&init_mutex);

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

  /* we split the total size into 16MB chunks, just in case we have more than */
  /* that */
  int num_chunks = (size + max_chunk_gpu - 1) / max_chunk_gpu;
  tcufile_chunk_t *chunks = malloc(sizeof(tcufile_chunk_t) * num_chunks);
  if (!chunks) {
    TCU_ERR("Could not allocate chunks, exiting");
    status.err = CU_FILE_NOMEM_ERROR;
    return status;
  }
  for (int i = 0; i < num_chunks; i++) {
    size_t current_offs = i * max_chunk_gpu;
    size_t chunk_size = (size - current_offs < max_chunk_gpu)
                            ? (size - current_offs)
                            : max_chunk_gpu;
    /* allocate shadow buffer */
    chunks[i].length = chunk_size;
    void *shadow_buf = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, g_nvfs_fd, 0);
    if (shadow_buf == MAP_FAILED) {
      TCU_ERR("Failed to map shadow buffer: %s", strerror(errno));
      status.err = CU_FILE_NVFS_INTERNAL_DRIVER_ERROR;
      /* free every shadow_buf and fence page allocated up to this point */
      for (int j = i - 1; j >= 0; j--) {
        munmap(chunks[j].shadow_buf, chunks[j].length);
        cudaHostUnregister((void *)chunks[j].fence_page);
        free((void *)chunks[j].fence_page);
      }
      free(chunks);
      return status;
    }
    chunks[i].shadow_buf = shadow_buf;
    /* allocate and map fence page */
    void *p = NULL;

    if (posix_memalign(&p, 4096, 4096) != 0 ||
        cudaHostRegister(p, 4096, cudaHostRegisterDefault) != cudaSuccess) {
      TCU_ERR("Failed to allocate pinned host memory for fence_page");
      if (p != NULL)
        free(p);
      munmap(shadow_buf, chunk_size);
      status.err = CU_FILE_CUDA_DRIVER_ERROR;
      /* free every shadow_buf allocated up to this point */
      for (int j = i - 1; j >= 0; j--) {
        munmap(chunks[j].shadow_buf, chunks[j].length);
        cudaHostUnregister((void *)chunks[j].fence_page);
        free((void *)chunks[j].fence_page);
      }
      free(chunks);
      status.cu_err = 2; /* CUDA_ERROR_OUT_OF_MEMORY (approx) */
      return status;
    }
    memset((void *)p, 0, 4096);
    chunks[i].fence_page = p;

    /* prepare and fire MAP ioctl */
    union nvfs_ioctl_param_u param;
    memset(&param, 0, sizeof(param));
    param.map_args.size = chunk_size;
    param.map_args.pdevinfo = pdevinfo;
    param.map_args.cpuvaddr = (uint64_t)shadow_buf;
    param.map_args.gpuvaddr = (uint64_t)((char *)buf + current_offs);
    param.map_args.end_fence_addr = (uint64_t)chunks[i].fence_page;
    param.map_args.is_bounce_buffer = 0;
    param.map_args.sbuf_block = chunk_size / 4096;

    if (ioctl(g_nvfs_fd, NVFS_IOCTL_MAP, &param) < 0) {
      TCU_ERR("NVFS_IOCTL_MAP failed: %s", strerror(errno));
      /* free every shadow_buf allocated up to this point */
      for (int j = i; j >= 0; j--) {
        munmap(chunks[j].shadow_buf, chunks[j].length);
        cudaHostUnregister((void *)chunks[j].fence_page);
        free((void *)chunks[j].fence_page);
      }
      free(chunks);
      status.err = CU_FILE_NVFS_DRIVER_ERROR;
      return status;
    }
    pthread_mutex_init(&chunks[i].io_lock, NULL);
  }

  /* 4. Save to State Manager */
  if (state_add_buf(buf, size, chunks, num_chunks, pdevinfo) < 0) {

    for (int i = 0; i < num_chunks; i++) {
      munmap(chunks[i].shadow_buf, chunks[i].length);
      cudaHostUnregister((void *)chunks[i].fence_page);
      free((void *)chunks[i].fence_page);
    }
    free(chunks);
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

  /* Cleanup hardware mappings */
  for (int i = 0; i < found->num_chunks; i++) {
    munmap(found->chunks[i].shadow_buf, found->chunks[i].length);
    cudaHostUnregister((void *)found->chunks[i].fence_page);
    free((void *)found->chunks[i].fence_page);
    pthread_mutex_destroy(&found->chunks[i].io_lock);
  }

  /* Remove from our state tracker */
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
  /* tracking variables for the buffer */
  size_t remaining = size;
  size_t current_file_offset = offset;
  size_t current_dev_offset = devPtr_offs;
  ssize_t bytes_transfered = 0;
  tcufile_handle_internal_t *internal = (tcufile_handle_internal_t *)fh;

  while (remaining > 0) {
    size_t curr_chunk = current_dev_offset / max_chunk_gpu;
    size_t curr_chunk_offs = current_dev_offset % max_chunk_gpu;

    size_t io_size = remaining;
    if (io_size > max_chunk_gpu - curr_chunk_offs)
      io_size = max_chunk_gpu - curr_chunk_offs;
    io_size = tcu_max_io_at(buf, curr_chunk_offs, io_size);

    /* now ready for ioctl */
    pthread_mutex_lock(&buf_info->chunks[curr_chunk].io_lock);
    buf_info->chunks[curr_chunk].fence_page->end_fence_val = 0;

    union nvfs_ioctl_param_u param;
    memset(&param, 0, sizeof(param));

    param.ioargs.cpuvaddr = (uint64_t)buf_info->chunks[curr_chunk].shadow_buf;
    param.ioargs.offset = current_file_offset;
    param.ioargs.size = io_size;
    param.ioargs.fd = internal->fd;
    param.ioargs.end_fence_value = 1;

    param.ioargs.file_args.inum = internal->inum;
    param.ioargs.file_args.majdev = internal->majdev;
    param.ioargs.file_args.mindev = internal->mindev;
    param.ioargs.file_args.devptroff = curr_chunk_offs;

    param.ioargs.optype = 1; /* 1 = WRITE */
    param.ioargs.hipri = 1;
    param.ioargs.sync = 1; /* Synchronous wait */
    TCU_DEBUG("Firing NVFS_IOCTL_WRITE for fd=%d, buf=%p", internal->fd,
              buf_info->chunks[curr_chunk].shadow_buf);
    int ret = ioctl(g_nvfs_fd, NVFS_IOCTL_WRITE, &param);
    int64_t done = param.ioargs.ioctl_return;
    pthread_mutex_unlock(&buf_info->chunks[curr_chunk].io_lock);
    TCU_DEBUG("Bytes transferred: %ld", done);
    /* Syscall-level failure. */
    if (ret < 0) {
      TCU_ERR("Write IOCTL failed: %s (nvidia_fs code: %ld)", strerror(errno),
              done);
      return (bytes_transfered > 0 ? bytes_transfered : ret);
    }
    /* Driver-level error carried in the payload (-errno), even when the ioctl
     * syscall itself succeeded. Do NOT trust io_size in this case. */
    if (done < 0) {
      TCU_ERR("Write completed with driver error (nvidia_fs code: %ld)", done);
      return (bytes_transfered > 0 ? bytes_transfered : (ssize_t)done);
    }
    /* Advance by what the driver actually moved, not what we requested. */
    bytes_transfered += done;
    remaining -= done;
    current_file_offset += done;
    current_dev_offset += done;
    /* Short transfer: the driver moved fewer bytes than asked. Stop rather than
     * blindly continuing past a partial op and reporting false success. */
    if ((size_t)done < io_size) {
      TCU_ERR("Short write: requested %zu, driver moved %ld", io_size, done);
      return bytes_transfered;
    }
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
    size_t curr_chunk = current_dev_offset / max_chunk_gpu;
    size_t curr_chunk_offs = current_dev_offset % max_chunk_gpu;

    size_t io_size = remaining;
    if (io_size > max_chunk_gpu - curr_chunk_offs)
      io_size = max_chunk_gpu - curr_chunk_offs;
    io_size = tcu_max_io_at(buf, curr_chunk_offs, io_size);
    /* now ready for ioctl */
    pthread_mutex_lock(&buf_info->chunks[curr_chunk].io_lock);
    buf_info->chunks[curr_chunk].fence_page->end_fence_val = 0;

    union nvfs_ioctl_param_u param;
    memset(&param, 0, sizeof(param));

    param.ioargs.cpuvaddr = (uint64_t)buf_info->chunks[curr_chunk].shadow_buf;
    param.ioargs.offset = current_file_offset;
    param.ioargs.size = io_size;
    param.ioargs.fd = internal->fd;
    param.ioargs.end_fence_value = 2;

    param.ioargs.file_args.inum = internal->inum;
    param.ioargs.file_args.majdev = internal->majdev;
    param.ioargs.file_args.mindev = internal->mindev;
    param.ioargs.file_args.devptroff = curr_chunk_offs;
    param.ioargs.optype = 0; /* 0 = READ */
    param.ioargs.sync = 1;   /* Synchronous wait */
    param.ioargs.hipri = 1;
    TCU_DEBUG("Firing NVFS_IOCTL_READ for fd=%d, buf=%p", internal->fd,
              buf_info->chunks[curr_chunk].shadow_buf);
    int ret = ioctl(g_nvfs_fd, NVFS_IOCTL_READ, &param);

    int64_t done = param.ioargs.ioctl_return;
    pthread_mutex_unlock(&buf_info->chunks[curr_chunk].io_lock);
    TCU_DEBUG("Bytes transferred: %ld", done);
    /* Syscall-level failure. */
    if (ret < 0) {
      TCU_ERR("Read IOCTL failed: %s (nvidia_fs code: %ld)", strerror(errno),
              done);
      return (bytes_transfered > 0 ? bytes_transfered : ret);
    }
    /* Driver-level error carried in the payload (-errno), even when the ioctl
     * syscall itself succeeded. Do NOT trust io_size in this case. */
    if (done < 0) {
      TCU_ERR("Read completed with driver error (nvidia_fs code: %ld)", done);
      return (bytes_transfered > 0 ? bytes_transfered : (ssize_t)done);
    }
    /* Advance by what the driver actually moved, not what we requested. */
    bytes_transfered += done;
    remaining -= done;
    current_file_offset += done;
    current_dev_offset += done;
    /* Short transfer: the driver moved fewer bytes than asked. Stop rather than
     * blindly continuing past a partial op and reporting false success. */
    if ((size_t)done < io_size) {
      TCU_ERR("Short read: requested %zu, driver moved %ld", io_size, done);
      return bytes_transfered;
    }
  }
  return bytes_transfered;
}

/* ---------------------------------------------------------------------- */
/* HARDWARE POLLING ASYNC ARCHITECTURE */
/* ---------------------------------------------------------------------- */
/* Context struct to harvest the final result asynchronously */
typedef struct {
  volatile struct nvfs_ioctl_metapage *fence;
  ssize_t *user_bytes_p;
} result_ctx_t;

/* The INSTANT callback. No while loops. No CPU blocking. */
static uint64_t global_ticket = 500;

static void CUDART_CB finalize_io_callback(void *data) {
  result_ctx_t *ctx = (result_ctx_t *)data;
  if (ctx->user_bytes_p != NULL) {
    /* The driver writes 'result' as a SIGNED value: >=0 is bytes transferred,
     */
    /* <0 is a -errno (see nvfs-core.c nvfs_io_complete). Read it signed. */
    int64_t res = (int64_t)ctx->fence->result;
    /* finalize callbacks are host funcs on one stream, so they run serialized
     */
    /* -> no race on the accumulator. */
    if (res < 0) {
      /* This chunk failed. Surface the -errno as a negative byte count */
      /* (POSIX-like) and make it sticky: an error on any chunk overrides a */
      /* partial-success total from earlier chunks. */
      *(ctx->user_bytes_p) = (ssize_t)res;
    } else if (*(ctx->user_bytes_p) >= 0) {
      /* Only accumulate while no prior chunk has failed. */
      *(ctx->user_bytes_p) += (ssize_t)res;
    }
  }
  free(ctx);
}

typedef struct {
  tcufile_handle_internal_t *internal;
  tcufile_buf_t *buf_info;
  size_t curr_chunk;
  size_t curr_chunk_offs;
  size_t io_size;
  size_t current_file_offset;
  uint64_t ticket_id;
  int optype;
} ioctl_submit_ctx_t;

static void CUDART_CB submit_ioctl_callback(void *data) {
  ioctl_submit_ctx_t *ctx = (ioctl_submit_ctx_t *)data;

  union nvfs_ioctl_param_u param;
  memset(&param, 0, sizeof(param));
  param.ioargs.cpuvaddr =
      (uint64_t)ctx->buf_info->chunks[ctx->curr_chunk].shadow_buf;
  param.ioargs.offset = ctx->current_file_offset;
  param.ioargs.size = ctx->io_size;
  param.ioargs.fd = ctx->internal->fd;
  param.ioargs.file_args.devptroff = ctx->curr_chunk_offs;

  /* crucial in order to keep gpu from unblocking due to a stale value for the
   */
  /* ticket */
  param.ioargs.end_fence_value = 0;
  __sync_synchronize();

  param.ioargs.end_fence_value = ctx->ticket_id;
  param.ioargs.file_args.inum = ctx->internal->inum;
  param.ioargs.file_args.majdev = ctx->internal->majdev;
  param.ioargs.file_args.mindev = ctx->internal->mindev;

  param.ioargs.hipri =
      1; /* crucial! we need to enable hipri to enable the nvme polling path. */
  param.ioargs.optype = ctx->optype;
  param.ioargs.sync = 0; /* async! */
  TCU_DEBUG("Stream Callback: Firing ASYNC NVFS_IOCTL for fd=%d optype=%d",
            ctx->internal->fd, ctx->optype);
  int ret = ioctl(
      g_nvfs_fd, ctx->optype == 1 ? NVFS_IOCTL_WRITE : NVFS_IOCTL_READ, &param);
  if (ret < 0 || param.ioargs.ioctl_return < 0) {
    int64_t err = (ret < 0) ? -errno : (int64_t)param.ioargs.ioctl_return;
    if (err >= 0)
      err = -EIO; /* guarantee a negative code even if errno was clear */
    TCU_ERR("Async IOCTL submit failed (fd=%d optype=%d): %s",
            ctx->internal->fd, ctx->optype, strerror(errno));
    /* A rejected submission means the driver will NEVER write the fence, so the
     */
    /* GPU's cuStreamWaitValue64 on this ticket would hang forever. Publish the
     */
    /* fence ourselves (result then end_fence_val, mirroring the driver's wmb */
    /* ordering in nvfs_io_complete) so the wait releases and
     * finalize_io_callback */
    /* observes the error instead of the stream deadlocking. */
    volatile struct nvfs_ioctl_metapage *fence =
        ctx->buf_info->chunks[ctx->curr_chunk].fence_page;
    fence->result = (uint64_t)err;
    __sync_synchronize();
    fence->end_fence_val = ctx->ticket_id;
  }
  free(ctx);
}

CUfileError_t tiny_cu_write_async(tcufile_handle_t fh, void *bufPtr_base,
                                  size_t *size_p, off_t *file_offset_p,
                                  off_t *bufPtr_offset_p,
                                  ssize_t *bytes_written_p, CUstream stream) {
  CUfileError_t status;
  status.cu_err = CUDA_SUCCESS;

  if (g_nvfs_fd < 0) {
    status.err = CU_FILE_DRIVER_NOT_INITIALIZED;
    return status;
  }

  tcufile_buf_t *buf_info = state_find_buf(bufPtr_base);
  if (!buf_info) {
    TCU_ERR("Async write failed: GPU buffer is not registered.");
    status.err = CU_FILE_MEMORY_NOT_REGISTERED;
    return status;
  }

  tcufile_handle_internal_t *internal = (tcufile_handle_internal_t *)fh;

  if (bytes_written_p)
    *bytes_written_p = 0; /* reset before per-chunk accumulation */
  size_t remaining = *size_p;
  size_t current_file_offset = *file_offset_p;
  size_t current_dev_offset = *bufPtr_offset_p;

  while (remaining > 0) {
    size_t curr_chunk = current_dev_offset / max_chunk_gpu;
    size_t curr_chunk_offs = current_dev_offset % max_chunk_gpu;
    size_t io_size = remaining;
    if (io_size > max_chunk_gpu - curr_chunk_offs)
      io_size = max_chunk_gpu - curr_chunk_offs;

    io_size = tcu_max_io_at(bufPtr_base, curr_chunk_offs, io_size);
    /* 1. Reset the fence via the new struct */
    volatile struct nvfs_ioctl_metapage *fence =
        buf_info->chunks[curr_chunk].fence_page;

    /* Unique ticket id */
    uint64_t ticket_id = __sync_fetch_and_add(&global_ticket, 1);

    /* 2. Queue IOCTL submission in the stream */
    ioctl_submit_ctx_t *submit_ctx = malloc(sizeof(ioctl_submit_ctx_t));
    submit_ctx->internal = internal;
    submit_ctx->buf_info = buf_info;
    submit_ctx->curr_chunk = curr_chunk;
    submit_ctx->curr_chunk_offs = curr_chunk_offs;
    submit_ctx->io_size = io_size;
    submit_ctx->current_file_offset = current_file_offset;
    submit_ctx->ticket_id = ticket_id;
    submit_ctx->optype = 1; /* 1 = WRITE */
    cudaLaunchHostFunc(stream, submit_ioctl_callback, (void *)submit_ctx);

    /* 3. TELL GPU HARDWARE TO WAIT */
    /* TCU_DEBUG("telling now gpu to wait"); */
    CUdeviceptr fence_ptr = (CUdeviceptr)&fence->end_fence_val;
    cuStreamWaitValue64(stream, fence_ptr, ticket_id, CU_STREAM_WAIT_VALUE_EQ);

    /* TCU_DEBUG("gpu is now waiting for the io %d to finish", ticket_id); */
    /* 4. QUEUE THE CALLBACK TO HARVEST BYTES WRITTEN */
    result_ctx_t *ctx = malloc(sizeof(result_ctx_t));
    ctx->fence = fence;
    ctx->user_bytes_p = bytes_written_p;
    cudaLaunchHostFunc(stream, finalize_io_callback, (void *)ctx);

    remaining -= io_size;
    current_file_offset += io_size;
    current_dev_offset += io_size;
  }

  status.err = CU_FILE_SUCCESS;
  return status;
}

CUfileError_t tiny_cu_read_async(tcufile_handle_t fh, void *bufPtr_base,
                                 size_t *size_p, off_t *file_offset_p,
                                 off_t *bufPtr_offset_p, ssize_t *bytes_read_p,
                                 CUstream stream) {
  CUfileError_t status;
  status.cu_err = CUDA_SUCCESS;

  if (g_nvfs_fd < 0) {
    status.err = CU_FILE_DRIVER_NOT_INITIALIZED;
    return status;
  }

  tcufile_buf_t *buf_info = state_find_buf(bufPtr_base);
  if (!buf_info) {
    TCU_ERR("Async read failed: GPU buffer is not registered.");
    status.err = CU_FILE_MEMORY_NOT_REGISTERED;
    return status;
  }

  tcufile_handle_internal_t *internal = (tcufile_handle_internal_t *)fh;

  if (bytes_read_p)
    *bytes_read_p = 0; /* reset before per-chunk accumulation */
  size_t remaining = *size_p;
  size_t current_file_offset = *file_offset_p;
  size_t current_dev_offset = *bufPtr_offset_p;

  while (remaining > 0) {
    size_t curr_chunk = current_dev_offset / max_chunk_gpu;
    size_t curr_chunk_offs = current_dev_offset % max_chunk_gpu;
    size_t io_size = remaining;
    if (io_size > max_chunk_gpu - curr_chunk_offs)
      io_size = max_chunk_gpu - curr_chunk_offs;

    io_size = tcu_max_io_at(bufPtr_base, curr_chunk_offs, io_size);
    /* 1. Reset the fence via the new struct */
    volatile struct nvfs_ioctl_metapage *fence =
        buf_info->chunks[curr_chunk].fence_page;

    /* Unique ticket id */
    uint64_t ticket_id = __sync_fetch_and_add(&global_ticket, 1);

    /* 2. Queue IOCTL submission in the stream */
    ioctl_submit_ctx_t *submit_ctx = malloc(sizeof(ioctl_submit_ctx_t));
    submit_ctx->internal = internal;
    submit_ctx->buf_info = buf_info;
    submit_ctx->curr_chunk = curr_chunk;
    submit_ctx->curr_chunk_offs = curr_chunk_offs;
    submit_ctx->io_size = io_size;
    submit_ctx->current_file_offset = current_file_offset;
    submit_ctx->ticket_id = ticket_id;
    submit_ctx->optype = 0; /* 0 = READ */
    cudaLaunchHostFunc(stream, submit_ioctl_callback, (void *)submit_ctx);

    /* 3. TELL GPU HARDWARE TO WAIT */
    /* TCU_DEBUG("telling now gpu to wait"); */
    CUdeviceptr fence_ptr = (CUdeviceptr)&fence->end_fence_val;
    cuStreamWaitValue64(stream, fence_ptr, ticket_id, CU_STREAM_WAIT_VALUE_EQ);
    /* TCU_DEBUG("gpu is now waiting for the io %d to finish", ticket_id); */
    /* 4. QUEUE THE CALLBACK TO HARVEST BYTES READ */
    result_ctx_t *ctx = malloc(sizeof(result_ctx_t));
    ctx->fence = fence;
    ctx->user_bytes_p = bytes_read_p;
    cudaLaunchHostFunc(stream, finalize_io_callback, (void *)ctx);

    remaining -= io_size;
    current_file_offset += io_size;
    current_dev_offset += io_size;
  }

  status.err = CU_FILE_SUCCESS;
  return status;
}
