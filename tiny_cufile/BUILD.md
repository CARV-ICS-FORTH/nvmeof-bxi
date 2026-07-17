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
make            # release build (optimized): the two libraries
make tests      # additionally build the test/bench harnesses
```

`make` (release) produces:
- `libtiny_cufile.so` — the library (includes the `cuFile*` shim symbols).
- `ldp_libcufile.so` — the same objects packaged for `LD_PRELOAD` interception.

`make tests` also builds the harnesses in `test/` (`simple_sync`,
`simple_async`, `stress_threads`, `bench_write`), each linked against
`libtiny_cufile.so` with an `$ORIGIN` rpath so they run from any directory. Build
one by name with e.g. `make stress_threads`.

The libraries link **both** CUDA libraries: `-lcudart` (runtime) and `-lcuda`
(driver API, for `cuStreamWaitValue64`). A missing `-lcuda` shows up as an
unresolved `cuStreamWaitValue64` at link time.

## 3. Build profiles and options

### Profiles

Objects are cached per-profile under `build/<profile>/`, so switching profiles
does not trigger a full rebuild. The `debug`/`asan`/`tsan` targets build the
libraries **and** the harnesses in that profile.

| command      | flags                          | use                                          |
| ------------ | ------------------------------ | -------------------------------------------- |
| `make`       | `-O2 -g -DNDEBUG`              | release (default); benchmarking              |
| `make debug` | `-O0 -g3 -DTINY_CU_DEBUG`      | gdb; enables `TCU_DEBUG`/`TCU_INFO` tracing  |
| `make asan`  | + AddressSanitizer + UBSan     | host memory / undefined-behavior bugs        |
| `make tsan`  | + ThreadSanitizer              | data races (validates the per-chunk `io_lock`) |

> **Sanitizers vs CUDA.** ASan and TSan reserve large fixed regions of the
> process address space and collide with the CUDA runtime's mappings by default;
> each needs a workaround to run a CUDA process:
> - **ASan** — otherwise fails at `cudaMalloc`. Free up the shadow gap:
>   ```
>   sudo ASAN_OPTIONS=protect_shadow_gap=0:replace_intrin=0 \
>        ./stress_threads /mnt/nvme_test/gds_stress.dat 16 16 200
>   ```
> - **TSan** — may fail intermittently with "unexpected memory mapping" (an ASLR
>   interaction). Disable randomization with `setarch -R`:
>   ```
>   sudo setarch -R ./stress_threads /mnt/nvme_test/gds_stress.dat 16 16 200
>   ```
>
> For GPU-side checking use NVIDIA's `compute-sanitizer` instead.

### Options (override on the command line)

- `STRICT=1` — add `-Wconversion -Wsign-conversion -Wcast-qual -Werror` (CI / cleanup).
- `CUDA_HOME=/opt/cuda` — non-standard CUDA toolkit prefix.
- `CUDA_ARCH=x86_64-linux` — toolkit target triple; locates `targets/<arch>/lib`,
  where `libcufile` lives for the `*_real` twins (§4).
- `CC=clang` — override the compiler (defaults to `gcc`).
- `EXTRA_CFLAGS=-DTCU_DEFAULT_MAX_CHUNK_MB=4` — append extra defines, e.g. to bake
  a different default registration chunk size (§4.1).
- `make clean` — remove `build/` and all outputs.
- `make help` — list targets.

---

## 4. Running the tests

The harnesses are linked with an `$ORIGIN` rpath, so they find
`libtiny_cufile.so` beside them and run from any directory. They need
`/dev/nvidia-fs0`, a GPU, and an `O_DIRECT` target file. Each accepts
`-h`/`--help`.

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
- **`stress_threads [path] [size_MB] [threads] [iters] [mode]`** — multi-threaded
  concurrency stressor. Threads hit disjoint sub-ranges of the same chunk (with
  data verification) to contend on one mgroup and exercise the per-chunk
  `io_lock`. `mode` is `sync` (default) or `async`. Exit `0` = clean, `2` =
  failure:
  ```
  ./stress_threads /mnt/nvme_test/gds_stress.dat 16 16 200 sync
  ```
- **`bench_write [size_MB] [iters] [path]`** — single-thread sync
  latency/throughput A/B harness (min/median/mean/p99 + GB/s):
  ```
  ./bench_write 32 200 /mnt/nvme_test/gds_test.dat
  ```

### A/B against the real libcufile

Any harness has a `*_real` twin that links the genuine NVIDIA `libcufile`
instead of tiny_cufile, for side-by-side comparison of the identical workload:

```
make stress_threads_real
./stress_threads_real /mnt/nvme_test/gds_stress.dat 16 16 200
```

`libcufile` lives under `$CUDA_HOME/targets/<arch>/lib`; set `CUDA_ARCH` if the
default `x86_64-linux` is wrong for your toolkit.

### 4.1 Registration chunk size (`TINY_CU_MAX_CHUNK_MB`)

`tiny_cu_buf_register` splits a registered buffer into `max_chunk_gpu`-sized
chunks, one `nvidia-fs` mgroup each. The driver allows only one in-flight I/O per
mgroup, so **smaller chunks = more mgroups = more concurrent same-buffer I/O**.
The size defaults to 2 MiB and can be changed at runtime (in MiB); it is clamped
to the largest mapping the driver accepts:

```
TINY_CU_MAX_CHUNK_MB=4 ./stress_threads /mnt/nvme_test/gds_stress.dat 16 16 200
```

Under `sudo`, pass it as an explicit assignment (`sudo TINY_CU_MAX_CHUNK_MB=4
./…`) since sudo scrubs the environment. Bake a different default into a build
with `make EXTRA_CFLAGS=-DTCU_DEFAULT_MAX_CHUNK_MB=4`.

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
