/*
 * stress_threads -- multi-threaded, single-process concurrency stressor for the
 * synchronous GPUDirect path.
 *
 * One process registers ONE GPU buffer, then N threads hammer the SAME 16 MB
 * chunk concurrently (disjoint byte sub-ranges) with cuFileWrite/cuFileRead.
 * Because nvidia-fs allows only one in-flight I/O per mgroup (per chunk), this
 * is a direct regression test for tiny_cufile's per-chunk io_lock: with the
 * mutex the run is clean; without it, concurrent same-chunk submissions return
 * -EBUSY (-16) and/or corrupt data.
 *
 * Each thread owns a disjoint sub-range of the shared buffer, so data
 * verification is well-defined (no last-writer-wins race) while every thread
 * still contends on the single chunk-0 mgroup/mutex. A pthread_barrier releases
 * all threads into the I/O loop simultaneously to maximize overlap.
 *
 * Usage: stress_threads [path] [size_MB] [threads] [iters]   (see -h)
 * Exit: 0 = clean, 2 = concurrency/data failure, 1 = setup error.
 */
#define _GNU_SOURCE
#include "../cufile.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Which cuFile implementation this binary is linked against, for A/B logs.
 * The real-libcufile build overrides it via -DTCU_LIBLABEL="real". */
#ifndef TCU_LIBLABEL
#define TCU_LIBLABEL "tiny"
#endif

#define MB (1024UL * 1024UL)
#define DIO_ALIGN 4096UL
#define MAX_CHUNK_MB 16 /* nvidia-fs mgroup / tiny_cufile chunk size */
#define DEFAULT_PATH "/mnt/nvme_test/gds_stress.dat"
#define DEFAULT_MB 8
#define DEFAULT_THREADS 8
#define DEFAULT_ITERS 200

/* Offset-dependent byte so any misplacement/truncation shows up at a specific
 * global offset. Matches the scheme used by simple_sync. */
static inline unsigned char patbyte(size_t i) {
  return (unsigned char)(((uint32_t)i * 2654435761u) >> 24);
}

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

typedef struct {
  int tid;
  CUfileHandle_t h;
  void *d_buf;              /* shared registered base */
  const unsigned char *pat; /* full host pattern, thread reads its slice */
  size_t off;               /* this thread's byte offset into buf/file */
  size_t seg;               /* this thread's sub-range length */
  int iters;
  pthread_barrier_t *barrier;
  /* results (each thread writes only its own struct) */
  long ops;             /* write+read ops attempted */
  long neg;             /* negative returns (-EBUSY etc.) */
  long shorts;          /* 0 <= ret < seg */
  long mismatch;        /* data verification failures */
  long cuda_err;        /* local CUDA failures */
  ssize_t last_neg;     /* last negative return value seen */
  size_t first_bad_off; /* first mismatching global offset */
} thread_ctx_t;

/* Compare this thread's slice against the known pattern; record the first
 * mismatch. Each thread owns a disjoint range, so no locking is needed. */
static void verify_slice(thread_ctx_t *c, const unsigned char *h_ver) {
  for (size_t i = 0; i < c->seg; i++) {
    if (h_ver[i] != c->pat[c->off + i]) {
      if (c->mismatch == 0)
        c->first_bad_off = c->off + i;
      c->mismatch++;
      return; /* one report per iteration is enough */
    }
  }
}

/* Synchronous path: blocking cuFileWrite/cuFileRead (goes through the per-chunk
 * io_lock in tiny_cufile). */
static void *worker(void *arg) {
  thread_ctx_t *c = (thread_ctx_t *)arg;
  c->first_bad_off = (size_t)-1;
  unsigned char *h_ver = (unsigned char *)malloc(c->seg);
  if (h_ver == NULL) {
    c->cuda_err++;
    return NULL;
  }
  unsigned char *dev = (unsigned char *)c->d_buf + c->off;

  pthread_barrier_wait(c->barrier);
  for (int it = 0; it < c->iters; it++) {
    /* refill this thread's slice with the known pattern */
    if (cudaMemcpy(dev, c->pat + c->off, c->seg, cudaMemcpyHostToDevice) !=
        cudaSuccess) {
      c->cuda_err++;
      break;
    }
    ssize_t wb = cuFileWrite(c->h, c->d_buf, c->seg, (off_t)c->off, (off_t)c->off);
    c->ops++;
    if (wb < 0) {
      c->neg++;
      c->last_neg = wb;
    } else if ((size_t)wb != c->seg) {
      c->shorts++;
    }
    /* sabotage so the read must repopulate from disk */
    if (cudaMemset(dev, 0xFF, c->seg) != cudaSuccess) {
      c->cuda_err++;
      break;
    }
    ssize_t rb = cuFileRead(c->h, c->d_buf, c->seg, (off_t)c->off, (off_t)c->off);
    c->ops++;
    if (rb < 0) {
      c->neg++;
      c->last_neg = rb;
    } else if ((size_t)rb != c->seg) {
      c->shorts++;
    }
    /* verify round-trip */
    if (cudaMemcpy(h_ver, dev, c->seg, cudaMemcpyDeviceToHost) != cudaSuccess) {
      c->cuda_err++;
      break;
    }
    verify_slice(c, h_ver);
  }
  free(h_ver);
  return NULL;
}

/* Asynchronous path: cuFileWriteAsync/cuFileReadAsync stream-ordered on a
 * per-thread CUDA stream. NOTE: tiny_cufile's async submit path does NOT take
 * the per-chunk io_lock, so concurrent same-chunk (same-mgroup) submits can
 * surface -EBUSY here where the sync path would serialize. One stream sync per
 * iteration; the ordered chain is refill -> write -> sabotage -> read -> copy
 * back. Per-op size/offset args live on the stack until the sync drains. */
static void *worker_async(void *arg) {
  thread_ctx_t *c = (thread_ctx_t *)arg;
  c->first_bad_off = (size_t)-1;
  unsigned char *h_ver = (unsigned char *)malloc(c->seg);
  cudaStream_t stream;
  if (h_ver == NULL || cudaStreamCreate(&stream) != cudaSuccess) {
    c->cuda_err++;
    free(h_ver);
    return NULL;
  }
  unsigned char *dev = (unsigned char *)c->d_buf + c->off;

  pthread_barrier_wait(c->barrier);
  for (int it = 0; it < c->iters; it++) {
    size_t wsz = c->seg, rsz = c->seg;
    off_t woff = (off_t)c->off, roff = (off_t)c->off;
    off_t wdoff = (off_t)c->off, rdoff = (off_t)c->off;
    ssize_t wbytes = 0, rbytes = 0;

    cudaMemcpyAsync(dev, c->pat + c->off, c->seg, cudaMemcpyHostToDevice, stream);
    CUfileError_t ws = cuFileWriteAsync(c->h, c->d_buf, &wsz, &woff, &wdoff,
                                        &wbytes, (CUstream)stream);
    cudaMemsetAsync(dev, 0xFF, c->seg, stream);
    CUfileError_t rs = cuFileReadAsync(c->h, c->d_buf, &rsz, &roff, &rdoff,
                                       &rbytes, (CUstream)stream);
    cudaMemcpyAsync(h_ver, dev, c->seg, cudaMemcpyDeviceToHost, stream);

    if (cudaStreamSynchronize(stream) != cudaSuccess) {
      c->cuda_err++;
      break;
    }
    /* submission errors surface as a non-SUCCESS status or a negative byte
     * count once the stream drains. */
    c->ops += 2;
    if (ws.err != CU_FILE_SUCCESS || wbytes < 0) {
      c->neg++;
      c->last_neg = (wbytes < 0) ? wbytes : -(ssize_t)ws.err;
    } else if ((size_t)wbytes != c->seg) {
      c->shorts++;
    }
    if (rs.err != CU_FILE_SUCCESS || rbytes < 0) {
      c->neg++;
      c->last_neg = (rbytes < 0) ? rbytes : -(ssize_t)rs.err;
    } else if ((size_t)rbytes != c->seg) {
      c->shorts++;
    }
    verify_slice(c, h_ver);
  }
  cudaStreamDestroy(stream);
  free(h_ver);
  return NULL;
}

static void usage(const char *prog) {
  fprintf(stderr,
          "Multi-threaded same-chunk concurrency stressor for the GPUDirect "
          "sync/async paths.\n\n"
          "Usage: %s [path] [size_MB] [threads] [iters] [mode]\n"
          "  path       O_DIRECT-capable target on real NVMe (default: %s)\n"
          "  size_MB    shared buffer size, keep <= %d so all I/O is one chunk "
          "(default: %d)\n"
          "  threads    concurrent worker threads (default: %d)\n"
          "  iters      write+read cycles per thread (default: %d)\n"
          "  mode       'sync' (default) or 'async' (per-thread CUDA stream)\n"
          "  -h,--help  show this help\n\n"
          "Threads hit disjoint sub-ranges of the SAME chunk, so they contend "
          "on one\nmgroup while data stays verifiable. The sync path serializes "
          "on tiny's\nper-chunk io_lock; the async path does NOT, so async may "
          "surface -EBUSY under\nsame-chunk contention. Exit: 0 clean, 2 "
          "failure, 1 setup.\n",
          prog, DEFAULT_PATH, MAX_CHUNK_MB, DEFAULT_MB, DEFAULT_THREADS,
          DEFAULT_ITERS);
}

int main(int argc, char **argv) {
  const char *path = DEFAULT_PATH;
  size_t size_mb = DEFAULT_MB;
  int threads = DEFAULT_THREADS;
  int iters = DEFAULT_ITERS;
  int async = 0;

  int pos = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n\n", argv[i]);
      usage(argv[0]);
      return 1;
    }
    switch (pos) {
    case 0:
      path = argv[i];
      break;
    case 1:
      size_mb = strtoull(argv[i], NULL, 10);
      break;
    case 2:
      threads = atoi(argv[i]);
      break;
    case 3:
      iters = atoi(argv[i]);
      break;
    case 4:
      if (!strcmp(argv[i], "async")) {
        async = 1;
      } else if (!strcmp(argv[i], "sync")) {
        async = 0;
      } else {
        fprintf(stderr, "mode must be 'sync' or 'async', got '%s'.\n\n", argv[i]);
        usage(argv[0]);
        return 1;
      }
      break;
    default:
      fprintf(stderr, "Too many arguments.\n\n");
      usage(argv[0]);
      return 1;
    }
    pos++;
  }
  if (size_mb == 0 || threads < 1 || iters < 1) {
    fprintf(stderr, "size_MB, threads, iters must all be > 0.\n\n");
    usage(argv[0]);
    return 1;
  }
  if (size_mb > (size_t)MAX_CHUNK_MB)
    fprintf(stderr,
            "[!] size_MB=%zu > %d: buffer spans multiple chunks, so same-chunk "
            "contention is NOT guaranteed.\n",
            size_mb, MAX_CHUNK_MB);

  size_t SZ = size_mb * MB;
  /* per-thread slice, aligned down for O_DIRECT */
  size_t seg = (SZ / (size_t)threads) & ~(DIO_ALIGN - 1);
  if (seg == 0) {
    fprintf(stderr,
            "size_MB too small for %d threads (need >= %zu MB so each thread "
            "gets a 4 KiB-aligned slice).\n",
            threads, ((DIO_ALIGN * (size_t)threads) + MB - 1) / MB);
    return 1;
  }
  size_t covered = seg * (size_t)threads;
  printf("[*] lib=%s  mode=%s  path=%s  buf=%zu MB  threads=%d  iters=%d  "
         "seg=%zu KiB  covered=%zu MB (chunk 0)\n",
         TCU_LIBLABEL, async ? "async" : "sync", path, size_mb, threads, iters,
         seg / 1024, covered / MB);

  CUfileError_t st = cuFileDriverOpen();
  if (st.err != CU_FILE_SUCCESS) {
    fprintf(stderr, "cuFileDriverOpen failed (err=%d cu_err=%d)\n", st.err,
            st.cu_err);
    return 1;
  }

  int fd = open(path, O_CREAT | O_RDWR | O_DIRECT, 0666);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  if (ftruncate(fd, (off_t)SZ) != 0)
    perror("ftruncate (continuing)");

  CUfileDescr_t descr;
  memset(&descr, 0, sizeof(descr));
  descr.handle.fd = fd;
  descr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
  CUfileHandle_t h;
  st = cuFileHandleRegister(&h, &descr);
  if (st.err != CU_FILE_SUCCESS) {
    fprintf(stderr, "cuFileHandleRegister failed (err=%d)\n", st.err);
    return 1;
  }

  unsigned char *pat = (unsigned char *)malloc(covered);
  if (pat == NULL) {
    fprintf(stderr, "host pattern alloc failed\n");
    return 1;
  }
  for (size_t i = 0; i < covered; i++)
    pat[i] = patbyte(i);

  void *d_buf = NULL;
  if (cudaMalloc(&d_buf, SZ) != cudaSuccess) {
    fprintf(stderr, "cudaMalloc failed\n");
    return 1;
  }
  st = cuFileBufRegister(d_buf, SZ, 0);
  if (st.err != CU_FILE_SUCCESS) {
    fprintf(stderr, "cuFileBufRegister failed (err=%d)\n", st.err);
    return 1;
  }

  thread_ctx_t *ctx = (thread_ctx_t *)calloc((size_t)threads, sizeof(*ctx));
  pthread_t *tids = (pthread_t *)calloc((size_t)threads, sizeof(*tids));
  if (ctx == NULL || tids == NULL) {
    fprintf(stderr, "thread bookkeeping alloc failed\n");
    return 1;
  }

  /* barrier includes main (+1) so we can time just the I/O phase */
  pthread_barrier_t barrier;
  pthread_barrier_init(&barrier, NULL, (unsigned)threads + 1);

  void *(*worker_fn)(void *) = async ? worker_async : worker;
  for (int t = 0; t < threads; t++) {
    ctx[t].tid = t;
    ctx[t].h = h;
    ctx[t].d_buf = d_buf;
    ctx[t].pat = pat;
    ctx[t].off = (size_t)t * seg;
    ctx[t].seg = seg;
    ctx[t].iters = iters;
    ctx[t].barrier = &barrier;
    if (pthread_create(&tids[t], NULL, worker_fn, &ctx[t]) != 0) {
      fprintf(stderr, "pthread_create failed for thread %d\n", t);
      return 1;
    }
  }

  pthread_barrier_wait(&barrier); /* release all workers */
  double t0 = now_sec();
  for (int t = 0; t < threads; t++)
    pthread_join(tids[t], NULL);
  double elapsed = now_sec() - t0;

  /* aggregate */
  long ops = 0, neg = 0, shorts = 0, mismatch = 0, cuda_err = 0;
  ssize_t last_neg = 0;
  size_t first_bad = (size_t)-1;
  for (int t = 0; t < threads; t++) {
    ops += ctx[t].ops;
    neg += ctx[t].neg;
    shorts += ctx[t].shorts;
    mismatch += ctx[t].mismatch;
    cuda_err += ctx[t].cuda_err;
    if (ctx[t].last_neg != 0)
      last_neg = ctx[t].last_neg;
    if (ctx[t].first_bad_off != (size_t)-1 && ctx[t].first_bad_off < first_bad)
      first_bad = ctx[t].first_bad_off;
  }
  /* bytes moved = ops (each op is one seg-sized transfer) */
  double mbps = elapsed > 0 ? ((double)ops * (double)seg / MB) / elapsed : 0.0;

  printf("=====================================================\n");
  printf("%-8s %-10s %-8s %-8s %-10s %-10s\n", "threads", "ops", "neg", "short",
         "mismatch", "MB/s");
  printf("%-8d %-10ld %-8ld %-8ld %-10ld %-10.1f\n", threads, ops, neg, shorts,
         mismatch, mbps);
  if (neg > 0)
    printf("  [neg] last negative return = %zd%s\n", last_neg,
           last_neg == -16 ? " (-EBUSY: concurrent same-mgroup I/O)" : "");
  if (mismatch > 0)
    printf("  [mismatch] first bad global offset = %zu\n", first_bad);
  if (cuda_err > 0)
    printf("  [cuda] %ld CUDA errors (setup/copy)\n", cuda_err);

  int rc;
  if (neg == 0 && shorts == 0 && mismatch == 0 && cuda_err == 0) {
    printf("[SUCCESS] %d threads x %d iters clean on one chunk.\n", threads,
           iters);
    rc = 0;
  } else {
    printf("[FAILURE] concurrency/data errors detected.\n");
    rc = (cuda_err > 0 && neg == 0 && shorts == 0 && mismatch == 0) ? 1 : 2;
  }
  printf("=====================================================\n");

  pthread_barrier_destroy(&barrier);
  cuFileBufDeregister(d_buf);
  cudaFree(d_buf);
  cuFileHandleDeregister(h);
  close(fd);
  cuFileDriverClose();
  free(pat);
  free(ctx);
  free(tids);
  return rc;
}
