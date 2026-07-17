#ifndef STATE_H
#define STATE_H

#include "uthash.h"
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
struct nvfs_ioctl_metapage;
typedef struct {
  void *shadow_buf;
  volatile struct nvfs_ioctl_metapage *fence_page;
  size_t length;
  // void *fence_alloc;
  pthread_mutex_t io_lock;
} tcufile_chunk_t;

/* A struct to hold everything we know about a registered GPU buffer */
typedef struct tcufile_buf_s {
  const void *devPtr_base; /* our key for the hash */
  size_t length;
  /* void *shadow_buf; */
  /* volatile uint64_t *fence_page; */
  uint64_t pdevinfo;

  tcufile_chunk_t *chunks; /* array of 16mb chunks */
  int num_chunks;

  UT_hash_handle hh;
} tcufile_buf_t;

int state_init(void);
int state_cleanup(void);

int state_add_buf(const void *devPtr, size_t length, tcufile_chunk_t *chunks,
                  int num_chunks, uint64_t pdevinfo);
int state_remove_buf(const void *devPtr);

tcufile_buf_t *state_find_buf(const void *devPtr);

#endif
