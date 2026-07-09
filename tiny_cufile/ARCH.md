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

The NVMe and `nvidia-fs` paths cap a single mapping / DMA request at
`max_chunk_gpu`. This limit is probed at init by `os_probe_nvfs_chunk_limit`
(the largest `mmap` size the driver accepts, from a descending candidate list,
falling back to a conservative value). Larger buffers are split into
`max_chunk_gpu`-sized chunks. For each chunk the library:

1. `mmap()`s a CPU shadow buffer against `g_nvfs_fd`.
2. Allocates a 4 KB page-locked host page (`cudaMallocHost`) as the completion
   **fence** (`fence_page`, a `struct nvfs_ioctl_metapage`). Being pinned, its
   address is device-accessible through unified addressing, so a GPU stream can
   poll it over PCIe.
3. Resolves the GPU's PCI address to a `pdevinfo` word
   (`os_get_gpu_pdevinfo`).
4. Fires `NVFS_IOCTL_MAP`. The driver pins the GPU pages
   (`nvidia_p2p_get_pages`), associates them with the shadow buffer, and wires
   the fence page for completion signalling.

The per-chunk records (`tcufile_chunk_t`: shadow buffer, fence page, length)
are stored in the registry keyed by the base device pointer. `cuFileBufRegister`
maps to this.

### 1.4 Synchronous I/O (`tiny_cu_read` / `tiny_cu_write`)

Used by `cuFileRead` / `cuFileWrite`. The request is sliced along the same
16 MB chunk boundaries. Per slice the library computes the chunk index and
in-chunk offset, fills `nvfs_ioctl_ioargs` (shadow address, file offset, size,
inode, maj/min), sets `sync = 1`, and issues `NVFS_IOCTL_READ` / `WRITE`. The
`ioctl` blocks until the DMA completes; the loop advances until all bytes are
transferred and returns the byte count (or a negative error).

---

## 2. Asynchronous I/O (hardware-polling)

Used by `cuFileReadAsync` / `cuFileWriteAsync`
(`tiny_cu_read_async` / `tiny_cu_write_async`). This is the default and only
async implementation. It orders storage I/O against a CUDA stream using a
completion fence that the GPU waits on directly, rather than blocking a CPU
thread on the result.

The request is sliced into the same 16 MB chunks. For each chunk, three items
are enqueued on the stream, in order:

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
