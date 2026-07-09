#include "state.h"
#include "log.h"
#include "nvfs_ioctl.h"
#include "uthash.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static int verify_buf(const void *devPtr, size_t length, void *shadow_buf,
                      volatile struct nvfs_ioctl_metapage *fence_page,
                      uint64_t pdevinfo) {
  (void)pdevinfo;
  if (!devPtr || length == 0 || !shadow_buf || !fence_page)
    return 0;
  return 1;
}

static pthread_rwlock_t lock;
static struct tcufile_buf_s *table;

int state_init() {
  table = NULL;
  pthread_rwlock_init(&lock, NULL);
  TCU_DEBUG("State Manager initialized.");
  return 0;
}
int state_cleanup(void) {
  tcufile_buf_t *current, *tmp;
  pthread_rwlock_wrlock(&lock);

  /* Safely iterate and free the entire hash table */
  HASH_ITER(hh, table, current, tmp) {
    HASH_DEL(table, current);
    free(current->chunks);
    free(current);
  }

  pthread_rwlock_unlock(&lock);
  pthread_rwlock_destroy(&lock);
  TCU_DEBUG("State Manager cleaned up and destroyed.");
  return 0;
}

int state_add_buf(const void *devPtr, size_t length, tcufile_chunk_t *chunks,
                  int num_chunks, uint64_t pdevinfo) {

  if (!chunks) {
    TCU_ERR("[State] GPU chunks cannot be empty");
    return -1;
  }
  struct tcufile_buf_s *new =
      (struct tcufile_buf_s *)malloc(sizeof(struct tcufile_buf_s));
  if (!new)
    return -1;

  for (int i = 0; i < num_chunks; i++) {
    if (!verify_buf(devPtr, length, chunks[i].shadow_buf, chunks[i].fence_page,
                    pdevinfo)) {
      TCU_ERR("Cannot add buffer, not verified");
      return -1;
    }
    new->length = length;
    new->chunks = chunks;
    new->devPtr_base = devPtr;
    new->pdevinfo = pdevinfo;
    new->num_chunks = num_chunks;
  }

  pthread_rwlock_wrlock(&lock);

  HASH_ADD_PTR(table, devPtr_base, new);

  pthread_rwlock_unlock(&lock);
  TCU_DEBUG(
      "Successfully registered buffer devPtr=%p, length=%zu, num_chunks=%d",
      devPtr, length, num_chunks);
  for (int i = 0; i < num_chunks; i++) {
    TCU_DEBUG("  -> Chunk %d: shadow_buf=%p, length=%zu", i,
              chunks[i].shadow_buf, chunks[i].length);
  }
  return 0;
}

struct tcufile_buf_s *state_find_buf(const void *devPtr_base) {
  struct tcufile_buf_s *found = NULL;
  pthread_rwlock_rdlock(&lock);
  HASH_FIND_PTR(table, &devPtr_base, found);
  pthread_rwlock_unlock(&lock);

  if (found) {
    TCU_DEBUG("Lookup success for devPtr=%p", devPtr_base);
  } else {
    TCU_DEBUG("Lookup failed for devPtr=%p", devPtr_base);
  }

  return found;
}

int state_remove_buf(const void *devPtr) {
  tcufile_buf_t *found = NULL;
  int ret = -1;

  pthread_rwlock_wrlock(&lock);
  HASH_FIND_PTR(table, &devPtr, found);
  if (found) {
    free(found->chunks);
    HASH_DEL(table, found);
    free(found);
    ret = 0;
  }

  pthread_rwlock_unlock(&lock);
  if (ret == 0) {
    TCU_DEBUG("Successfully removed buffer devPtr=%p", devPtr);
  } else {
    TCU_DEBUG("Failed to remove buffer devPtr=%p (not found)", devPtr);
  }
  return ret;
}
