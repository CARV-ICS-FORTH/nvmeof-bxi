#include "../cufile.h"
#include "../include/tiny_cufile.h"
#include <stddef.h>

CUfileError_t cuFileDriverOpen(void) { return tiny_cu_init(); }

CUfileError_t cuFileDriverClose(void) { return tiny_cu_cleanup(); }

CUfileError_t cuFileHandleRegister(CUfileHandle_t *fh, CUfileDescr_t *descr) {
  tcufile_descr_t t_descr;

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
  CUfileError_t status = tiny_cu_file_register(&t_fh, &t_descr);
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
