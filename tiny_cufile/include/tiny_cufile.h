#ifndef TINY_CUFILE_H
#define TINY_CUFILE_H

#include "../cufile.h" // this later is going to be including the native cufile.h of each system
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#define NVFS_MAGIC 't'
#define NVFS_IOCTL_MAP _IOW(NVFS_MAGIC, 3, int)
#define NVFS_IOCTL_WRITE _IOW(NVFS_MAGIC, 4, int)
#define NVFS_IOCTL_READ _IOW(NVFS_MAGIC, 2, int)

typedef void *tcufile_handle_t;

typedef struct {
  int fd;
} tcufile_descr_t;

ssize_t tiny_cu_read(tcufile_handle_t fh, void *buf, size_t size, off_t offset,
                     off_t devPtr_offs);
ssize_t tiny_cu_write(tcufile_handle_t fh, const void *buf, size_t size,
                      off_t offset, off_t devPtr_offs);

CUfileError_t tiny_cu_buf_register(const void *buf, size_t size);
CUfileError_t tiny_cu_buf_dereg(const void *buf);
CUfileError_t tiny_cu_file_register(tcufile_handle_t *fh,
                                    tcufile_descr_t *descr);
void tiny_cu_file_dereg(tcufile_handle_t fh);
CUfileError_t tiny_cu_init(void);
CUfileError_t tiny_cu_cleanup(void);

#endif
