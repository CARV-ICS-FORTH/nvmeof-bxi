# tiny_cufile: Architecture & Implementation

`tiny_cufile` is a small user-space implementation of a subset of NVIDIA's
GPUDirect Storage (GDS) library (`libcufile.so`). It talks directly to the
`nvidia-fs` kernel module through `ioctl()` calls to move data between an NVMe
block device and GPU memory over a peer-to-peer DMA path, without staging the
data through a CPU bounce buffer.

It can be built as an `LD_PRELOAD` shim so that an existing GDS application runs
its `cuFile*` calls through this implementation without being recompiled.

The public surface is defined in `cufile.h`; the shim that maps it onto the
internal API lives in `src/preload.c`.

---

## 1. Lifecycle

The library has three phases: **initialization**, **registration (mapping)**,
and **I/O execution** (synchronous or asynchronous).

### 1.1 Initialization (`tiny_cu_init`)

Opens the character device `/dev/nvidia-fs0`. The resulting descriptor
(`g_nvfs_fd`) carries every subsequent `ioctl()` to the kernel module. Init is
reference-counted under a mutex, and a `uthash`-backed registry (`state.c`) is
started to track registered buffers. `cuFileDriverOpen` maps to this.

### 1.2 File registration (`tiny_cu_file_register`)

Records the target file's `fd`, inode number (`fstat`), and the block-device
major/minor (`os_get_bdev_maj_min`, which uses `st_rdev` for a raw block device
and `st_dev` for a regular file). These identify the file to the driver.
`cuFileHandleRegister` maps to this and accepts `CU_FILE_HANDLE_TYPE_OPAQUE_FD`
only.

### 1.3 Buffer registration & chunking (`tiny_cu_buf_register`)

The `nvidia-fs` path caps a single mapping / DMA request at `max_chunk_gpu`, and
larger buffers are split into `max_chunk_gpu`-sized chunks. The chunk size is
resolved once at init by `resolve_max_chunk_gpu`: it defaults to
`TCU_DEFAULT_MAX_CHUNK_MB` (2 MiB, overridable at build time with
`-DTCU_DEFAULT_MAX_CHUNK_MB=N`), can be overridden at runtime by the
`TINY_CU_MAX_CHUNK_MB` environment variable (in MiB), is clamped to the largest
`mmap` the driver accepts (probed by `os_probe_nvfs_chunk_limit`), and is
GPU-page aligned. Smaller chunks create **more `nvidia-fs` mgroups** per
registered buffer, which lets concurrent I/O to different offsets of that buffer
run in parallel; the 2 MiB default favors concurrency (see §4). For each chunk
the library:

1. `mmap()`s a CPU shadow buffer against `g_nvfs_fd`.
2. Allocates a 4 KiB page-locked host page as the completion **fence**
   (`fence_page`, a `struct nvfs_ioctl_metapage`) via `posix_memalign` +
   `cudaHostRegister`. It must be exactly 4 KiB-aligned — the driver rejects an
   unaligned `end_fence` with `end_fence address not aligned` — and pinned so a
   GPU stream can poll it over PCIe through unified addressing.
3. Resolves the GPU's PCI address to a `pdevinfo` word
   (`os_get_gpu_pdevinfo`).
4. Fires `NVFS_IOCTL_MAP`. The driver pins the GPU pages
   (`nvidia_p2p_get_pages`), associates them with the shadow buffer, and wires
   the fence page for completion signalling.

The per-chunk records (`tcufile_chunk_t`: shadow buffer, fence page, length, and
a per-chunk `io_lock`) are stored in the registry keyed by the base device
pointer. `cuFileBufRegister` maps to this.

### 1.4 Synchronous I/O (`tiny_cu_read` / `tiny_cu_write`)

Used by `cuFileRead` / `cuFileWrite`. The request is sliced along the
`max_chunk_gpu` chunk boundaries; each slice is additionally capped so it never
crosses a 64 KiB GPU-page boundary (`tcu_max_io_at`), because `nvfs_io_init`
rejects an I/O whose GPU `va_offset` is not GPU-page aligned unless it fits in
the remainder of that page. Per slice the library fills `nvfs_ioctl_ioargs`:
`cpuvaddr` is the chunk's shadow-buffer **base** — not base + offset, which
`nvidia-fs` rejects as a shadow-buffer address mismatch — and the intra-chunk
offset goes in `file_args.devptroff` (plus file offset, size, inode, maj/min).
It sets `sync = 1` and issues `NVFS_IOCTL_READ` / `WRITE` while holding the
chunk's `io_lock` around the fence reset + `ioctl`, so concurrent same-chunk I/O
is serialized (`nvidia-fs` allows only one in-flight I/O per mgroup; see §4). The
`ioctl` blocks until the DMA completes; the loop advances until all bytes are
transferred and returns the byte count (or a negative error).

---

## 2. Asynchronous I/O (hardware-polling)

Used by `cuFileReadAsync` / `cuFileWriteAsync`
(`tiny_cu_read_async` / `tiny_cu_write_async`). This is the default async implementation. 
It orders storage I/O against a CUDA stream using a completion fence that the GPU waits on 
directly, rather than blocking a CPU thread on the result.

The request is sliced into the same `max_chunk_gpu` chunks. For each chunk,
three items are enqueued on the stream, in order:

1. **Submit** — a host callback (`cudaLaunchHostFunc` → `submit_ioctl_callback`)
   issues `NVFS_IOCTL_{READ,WRITE}` with `sync = 0`. Each chunk gets a unique,
   monotonically increasing **ticket** (`end_fence_value`). The driver queues
   the I/O and returns; on completion (later, from its bio-completion path) it
   writes `fence->result` and then `fence->end_fence_val = ticket`.

2. **Wait** — `cuStreamWaitValue64(stream, &fence->end_fence_val, ticket, EQ)`.
   The GPU stalls this stream until the fence reaches the ticket value, i.e.
   until the storage completion actually lands. This is what orders later stream
   work (kernels, copies) after the I/O.

3. **Finalize** — a host callback (`finalize_io_callback`) reads
   `fence->result` and folds it into the caller's byte count.

### 2.1 Completion is genuinely deferred

The driver writes the fence from its asynchronous completion path, *after* the
submit `ioctl` returns (verified: the fence still holds the previous value the
instant the `ioctl` returns). So the `cuStreamWaitValue64` wait is real — the
GPU waits on an event that has not happened yet at submit time — and the caller
thread is not blocked on the transfer.

### 2.2 Submit cost

The submit `ioctl` itself is not free: it performs synchronous per-chunk setup
(shadow/mpage preparation and GPU DMA mapping) before handing the transfer off
asynchronously. Measured at roughly the transfer time for a 16 MB chunk. This
setup runs on the CUDA host-callback thread and is serialized across chunks, so
it is the current throughput ceiling. `sync` / `hipri` do not affect it — they
govern the completion wait, which is already deferred. Removing this cost (a
dedicated submit thread pool so setup overlaps and does not sit on the callback
thread) is future work.

### 2.3 Error handling

`fence->result` is a signed value: `>= 0` is bytes transferred, `< 0` is a
`-errno`. `finalize_io_callback` reads it signed and makes an error sticky —
once any chunk fails, the caller's byte count holds that negative value instead
of a partial-success total. Callers see POSIX-like semantics: a non-negative
byte count, or a negative `-errno`.

If a submission is rejected synchronously (the `ioctl` returns an error), the
driver never writes the fence, which would leave `cuStreamWaitValue64` waiting
forever. To avoid that deadlock, `submit_ioctl_callback` publishes the fence
itself on failure — `result = -errno`, a barrier, then
`end_fence_val = ticket` — matching the driver's write ordering. The wait
releases and `finalize_io_callback` surfaces the error.

---

## 3. LD_PRELOAD shim

`src/preload.c` implements the `cuFile*` entry points from `cufile.h` and
forwards them to the internal API, translating opaque handles and
`CUfileError_t` results. Built as a preload object (`ldp_libcufile.so`) and
injected with `LD_PRELOAD`, it lets an unmodified GDS application run its I/O
through `tiny_cufile`. The mapping is:

| Public (`cuFile*`)                     | Internal                         |
| -------------------------------------- | -------------------------------- |
| `cuFileDriverOpen` / `Close`           | `tiny_cu_init` / `tiny_cu_cleanup` |
| `cuFileHandleRegister` / `Deregister`  | `tiny_cu_file_register` / `_dereg` |
| `cuFileBufRegister` / `Deregister`     | `tiny_cu_buf_register` / `_dereg`  |
| `cuFileRead` / `Write`                 | `tiny_cu_read` / `tiny_cu_write`   |
| `cuFileReadAsync` / `WriteAsync`       | `tiny_cu_read_async` / `_write_async` |

---

## 4. Known limitations

- **One in-flight I/O per chunk (mgroup).** `nvidia-fs` embeds a single I/O
  context (`nvfsio`) in each mapping group, so it rejects a second concurrent
  `NVFS_IOCTL_{READ,WRITE}` against the same chunk with `-EBUSY` (-16). The
  **synchronous** path serializes this with a per-chunk mutex (`io_lock`), which
  is correct but means concurrent same-chunk requests (e.g. LMCache issuing
  several overlapping stores) run one at a time.
- **The async path is unsafe for concurrent same-chunk I/O.** It does *not* take
  `io_lock`, and two async ops on one chunk share that chunk's single
  (MAP-time-bound) fence page while waiting on it with an exact-match
  `cuStreamWaitValue64`. The shared `end_fence_val` gets overwritten between the
  moment it holds a waiter's target ticket and the moment that waiter's GPU poll
  observes it, so a waiter misses its value and **deadlocks** — on top of the
  `-EBUSY` from the mgroup. Async is therefore only safe when concurrent ops
  target *distinct* chunks (small registration chunks make that the common case;
  see §1.3). The fix is to serialize submissions per mgroup (defer-don't-block,
  completion-driven), which also composes with the future submit thread pool.
- **Throughput depends on the underlying transport, not the cufile layer.** On a
  real local NVMe (regular file on ext4/xfs, IOMMU in passthrough) the
  synchronous path reaches ~2.3 GB/s on a single mgroup and ~4 GB/s at 16
  threads with the 2 MiB default chunk size — on par with or ahead of NVIDIA's
  `libcufile` on the same workload. Registration granularity is the scaling
  factor: more, smaller mgroups → more concurrent I/O (§1.3). Correctness on that
  system was independently validated — AddressSanitizer/UBSan clean (no leaks),
  ThreadSanitizer race-clean, and a multi-threaded stress test with per-slice
  data verification. Over the project's BXI portals4 NVMe-oF link to a remote
  ramdisk the *same* code measures only ~0.6 MB/s, but that reflects the
  (currently software-emulated) transport, not `tiny_cufile`; it is
  expected to scale with real BXI hardware, with no code changes.
- **Submit is synchronous and serialized** (§2.2). No submit thread pool yet, so
  async throughput is bounded by per-chunk setup on the callback thread.
- **O_DIRECT alignment.** The direct path requires block-aligned (typically
  4 KB) size and offset. An unaligned request is rejected by the driver; there
  is no POSIX / bounce-buffer compatibility fallback (which is how the stock
  library keeps such transfers working). Unaligned I/O therefore surfaces as an
  error rather than being transparently handled.
- **GPU pin cleanup on abnormal exit.** `tiny_cu_buf_dereg` releases the shadow
  mapping and fence page but does not issue an explicit unmap ioctl, and there
  is no signal/`atexit` handler. On `Ctrl+C` the `NVFS_IOCTL_MAP` pin is torn
  down only by kernel process teardown and the asynchronous `nvidia_p2p` free
  callback, so GPU memory can appear still reserved in `nvidia-smi` for a while
  after exit.
- **No batch I/O.** Only single read/write (sync) and stream async are
  implemented; there is no batch submit/reap path.
- **No RDMA / network filesystem support.** Local NVMe (`OPAQUE_FD`) only; the
  RDMA registration ioctls are not wired.
- **Stream memory ops required.** `cuStreamWaitValue64` depends on CUDA stream
  memory operations being available (see BUILD.md).
