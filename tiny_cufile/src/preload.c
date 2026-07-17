#include "../cufile.h"
#include "../include/tiny_cufile.h"
#include <pthread.h>
#include <stddef.h>

/* The real libcufile initializes itself lazily on the first API call, and
 * some consumers rely on that (e.g. LMCache's CuFileMemoryAllocator calls
 * cuFileBufRegister before cuFileDriverOpen). We mirror that behavior: the
 * registration entry points ensure the driver is up before proceeding.
 * tiny_cu_init() is refcounted, so the single extra reference taken here is
 * only released at process teardown -- same lifetime the real library gives
 * its implicit initialization. */
static pthread_mutex_t g_lazy_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_lazy_inited;

static CUfileError_t ensure_driver(void) {
  CUfileError_t status;
  status.err = CU_FILE_SUCCESS;
  status.cu_err = CUDA_SUCCESS;

  pthread_mutex_lock(&g_lazy_lock);
  if (!g_lazy_inited) {
    status = tiny_cu_init();
    if (!IS_CUFILE_ERR(status.err))
      g_lazy_inited = 1;
  }
  pthread_mutex_unlock(&g_lazy_lock);
  return status;
}

CUfileError_t cuFileDriverOpen(void) { return tiny_cu_init(); }

/* cufile.h renames cuFileDriverClose to cuFileDriverClose_v2 via a macro,
 * so this definition actually exports the _v2 symbol (matching apps compiled
 * against the header). */
CUfileError_t cuFileDriverClose(void) { return tiny_cu_cleanup(); }

/* The real libcufile exports BOTH names; dlsym-based consumers (e.g. Python
 * ctypes bindings) look up the unversioned one, so export it too. */
#undef cuFileDriverClose
CUfileError_t cuFileDriverClose(void) { return tiny_cu_cleanup(); }

CUfileError_t cuFileHandleRegister(CUfileHandle_t *fh, CUfileDescr_t *descr) {
  tcufile_descr_t t_descr;

  CUfileError_t status = ensure_driver();
  if (IS_CUFILE_ERR(status.err))
    return status;

  /* We only support Linux FDs in our implementation */
  if (descr->type == CU_FILE_HANDLE_TYPE_OPAQUE_FD) {
    t_descr.fd = descr->handle.fd;
  } else {
    CUfileError_t ret;
    ret.err = CU_FILE_INVALID_VALUE;
    ret.cu_err = CUDA_SUCCESS;
    return ret;
  }

  tcufile_handle_t t_fh;
  status = tiny_cu_file_register(&t_fh, &t_descr);
  if (!IS_CUFILE_ERR(status.err)) {
    *fh = (CUfileHandle_t)t_fh;
  }
  return status;
}

void cuFileHandleDeregister(CUfileHandle_t fh) {
  tiny_cu_file_dereg((tcufile_handle_t)fh);
}

CUfileError_t cuFileBufRegister(const void *bufPtr_base, size_t length,
                                int flags) {
  (void)flags; /* Ignore flags for now since we only support local NVMe */

  CUfileError_t status = ensure_driver();
  if (IS_CUFILE_ERR(status.err))
    return status;

  return tiny_cu_buf_register(bufPtr_base, length);
}

CUfileError_t cuFileBufDeregister(const void *bufPtr_base) {
  return tiny_cu_buf_dereg(bufPtr_base);
}

ssize_t cuFileRead(CUfileHandle_t fh, void *bufPtr_base, size_t size,
                   off_t file_offset, off_t bufPtr_offset) {
  return tiny_cu_read((tcufile_handle_t)fh, bufPtr_base, size, file_offset,
                      bufPtr_offset);
}

ssize_t cuFileWrite(CUfileHandle_t fh, const void *bufPtr_base, size_t size,
                    off_t file_offset, off_t bufPtr_offset) {
  return tiny_cu_write((tcufile_handle_t)fh, bufPtr_base, size, file_offset,
                       bufPtr_offset);
}

CUfileError_t cuFileReadAsync(CUfileHandle_t fh, void *devPtr_base,
                              size_t *size, off_t *file_offset,
                              off_t *devPtr_offset, ssize_t *bytes_read,
                              CUstream stream) {
  return tiny_cu_read_async((tcufile_handle_t)fh, devPtr_base, size,
                            file_offset, devPtr_offset, bytes_read, stream);
}

CUfileError_t cuFileWriteAsync(CUfileHandle_t fh, void *devPtr_base,
                               size_t *size, off_t *file_offset,
                               off_t *devPtr_offset, ssize_t *bytes_written,
                               CUstream stream) {
  return tiny_cu_write_async((tcufile_handle_t)fh, devPtr_base, size,
                             file_offset, devPtr_offset, bytes_written, stream);
}
