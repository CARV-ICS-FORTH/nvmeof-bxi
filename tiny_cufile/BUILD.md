# Building and running tiny_cufile

This covers prerequisites, the build, the build options, and how to run the
tests and the `LD_PRELOAD` shim. See `ARCH.md` for how the
library works.

---

## 1. Prerequisites

### Hardware
- An NVIDIA GPU that supports GPUDirect Storage / peer-to-peer DMA.
- A local NVMe device.

### Kernel / driver
- **NVIDIA driver** providing `libcuda.so` (the CUDA driver API). Check with
  `nvidia-smi`.
- **`nvidia-fs` kernel module** loaded, exposing the character device the
  library opens:
  ```
  lsmod | grep nvidia_fs
  ls -l /dev/nvidia-fs0
  ```
  If `/dev/nvidia-fs0` is missing, load the module (`modprobe nvidia_fs`, or
  build/insert it from the `gds-nvidia-fs` tree). The library opens this device
  by a fixed path in `tiny_cu_init`.
- **CUDA stream memory operations** must be usable — the async path relies on
  `cuStreamWaitValue64`. On some driver configurations this is gated by a module
  parameter (historically `NVreg_EnableStreamMemOPs=1`). Note that the query
  attribute `CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS_V1` can report `0` on
  recent drivers even when the operation works; verify by function, not by the
  attribute.

### Toolchain / libraries
- `gcc` and `make`.
- **CUDA toolkit** headers and libraries: `cuda.h`, `cuda_runtime.h`,
  `libcudart`, and `libcuda`. The Makefile defaults to `CUDA_HOME=/usr/local/cuda`.

### Storage / filesystem
- The target file must be opened with `O_DIRECT`, on a filesystem/device that
  supports it (a regular file on an NVMe-backed ext4/xfs mount, or a raw block
  device).
- Direct I/O requires **block-aligned** (typically 4 KB) size and offset.
  Unaligned requests are rejected by the driver (there is no POSIX fallback).
- The bundled tests default to files under `/mnt/nvme_test/`. Create/point this
  at a real NVMe mount, or pass a path argument (see §4).

---

## 2. Build

From this directory:

```
make
```

Outputs:
- `libtiny_cufile.so` — the library (includes the `cuFile*` shim symbols).
- `ldp_libcufile.so` — the same objects packaged for `LD_PRELOAD` interception.
- `simple_write`, `simple_async`, `verify_v2`, `verify_v2_err` — test/example
  binaries, linked against `libtiny_cufile.so` with `-rpath=.`.

The async path links **both** CUDA libraries: `-lcudart` (runtime) and
`-lcuda` (driver API, for `cuStreamWaitValue64`). A missing `-lcuda` shows up as
an unresolved `cuStreamWaitValue64` at link time.

## 3. Build options

- **Debug logging** — off by default. Enable the `TCU_DEBUG` / `TCU_INFO`
  tracing:
  ```
  make DEBUG=1
  ```
- **Non-standard CUDA location** — override the toolkit prefix:
  ```
  make CUDA_HOME=/opt/cuda
  ```
- **Clean**:
  ```
  make clean
  ```

---

## 4. Running the tests

All test binaries use `-rpath=.`, so run them from this directory (or set
`LD_LIBRARY_PATH=.`). They need `/dev/nvidia-fs0`, a GPU, and an `O_DIRECT`
target file. Each accepts `-h`/`--help` and takes its important parameters on
the command line.

- **`simple_sync [path] [size_MB]`** — synchronous round-trip verifier
  (`cuFileWrite`/`cuFileRead`): write a known pattern GPU→disk, wipe the buffer,
  read it back, compare byte-for-byte. Exit `0` = verified, `2` = mismatch:
  ```
  ./simple_sync /mnt/nvme_test/gds_sync.dat 32
  ```
- **`simple_async [path] [size_MB]`** — asynchronous round-trip verifier
  (`cuFileWriteAsync`/`cuFileReadAsync`), including multi-chunk byte accounting:
  ```
  ./simple_async /mnt/nvme_test/gds_async.dat 32
  ```
- **`simple_async -e [path]`** — same binary, error/no-deadlock negative test:
  issues a deliberately misaligned (rejected) request and checks it (a) does not
  deadlock — a 10 s watchdog guards this — and (b) surfaces a negative byte
  count. Exit `0` = pass, `2` = wrong result, `3` = watchdog/deadlock:
  ```
  ./simple_async --error /mnt/nvme_test/gds_async.dat
  ```

---

## 5. Using the LD_PRELOAD shim

To route an existing GDS application's `cuFile*` calls through tiny_cufile
without recompiling it:

```
LD_PRELOAD=/path/to/ldp_libcufile.so ./your_gds_app
```

The dynamic linker resolves the app's `cuFile*` symbols to the shim, which
forwards them to the internal API. The app must still meet the runtime
prerequisites above (nvidia-fs device, O_DIRECT, alignment, stream memory ops).

---

## 6. Troubleshooting

- **Link error: undefined `cuStreamWaitValue64`** — `-lcuda` missing; use the
  provided Makefile / a correct `CUDA_HOME`.
- **`Failed to open /dev/nvidia-fs0`** — the `nvidia-fs` module is not loaded,
  or permissions block the device.
- **`Operation not permitted` / `Invalid argument` on submit** — usually an
  unaligned or otherwise ineligible O_DIRECT request; the async path returns a
  negative byte count (it does not fall back to buffered I/O).
- **`nvidia-smi` still shows GPU memory after `Ctrl+C`** — expected with the
  current cleanup path; the `NVFS_IOCTL_MAP` pin is reclaimed by kernel teardown
  and the asynchronous `nvidia_p2p` free callback rather than an explicit unmap.
