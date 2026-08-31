#ifndef TINY_CUFILE_H
#define TINY_CUFILE_H

#include "../cufile.h" /* this later is going to be including the native cufile.h of each system */
#include "../src/nvfs_ioctl.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

typedef void *tcufile_handle_t;

typedef struct {
  int fd;
} tcufile_descr_t;

ssize_t tiny_cu_read(tcufile_handle_t fh, void *bufPtr_base, size_t size,
                     off_t offset, off_t devPtr_offs);
ssize_t tiny_cu_write(tcufile_handle_t fh, const void *buf, size_t size,
                      off_t offset, off_t devPtr_offs);

CUfileError_t tiny_cu_buf_register(const void *buf, size_t size);
CUfileError_t tiny_cu_buf_dereg(const void *buf);
CUfileError_t tiny_cu_file_register(tcufile_handle_t *fh,
                                    tcufile_descr_t *descr);

CUfileError_t tiny_cu_read_async(tcufile_handle_t fh, void *bufPtr_base,
                                 size_t *size_p, off_t *file_offset_p,
                                 off_t *bufPtr_offset_p, ssize_t *bytes_read_p,
                                 CUstream stream);

CUfileError_t tiny_cu_write_async(tcufile_handle_t fh, void *bufPtr_base,
                                  size_t *size_p, off_t *file_offset_p,
                                  off_t *bufPtr_offset_p,
                                  ssize_t *bytes_written_p, CUstream stream);

void tiny_cu_file_dereg(tcufile_handle_t fh);
CUfileError_t tiny_cu_init(void);
CUfileError_t tiny_cu_cleanup(void);

#endif
