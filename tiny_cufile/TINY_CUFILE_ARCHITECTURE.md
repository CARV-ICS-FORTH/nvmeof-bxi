# Tiny CuFile: Architecture & Implementation

`tiny_cufile` is a custom, lightweight, open-source user-space implementation of NVIDIA's proprietary GPUDirect Storage (GDS) library (`libcufile.so`). 

It communicates directly with the `nvidia-fs.ko` Linux kernel module via `ioctl()` system calls to enable Peer-to-Peer (P2P) Direct Memory Access (DMA) between NVMe block storage devices and NVIDIA GPU VRAM, entirely bypassing the CPU bounce buffers.

Furthermore, `tiny_cufile` is designed to be compiled as an `LD_PRELOAD` shared library (`libcufile.so`), allowing unmodified GDS-enabled applications (like PyTorch, DeepSpeed, or custom HPC binaries) to transparently execute their GPU I/O through this custom implementation.

---

## 1. Core Architecture

The library is built around three primary lifecycle phases: **Initialization**, **Registration (Mapping)**, and **Execution (DMA I/O)**.

### 1.1 Initialization (`tiny_cu_init`)
The library opens the character device `/dev/nvidia-fs0`. This file descriptor (`g_nvfs_fd`) is the conduit for all subsequent `ioctl()` commands sent to the NVIDIA kernel module. It also initializes a thread-safe state manager backed by `uthash` to track mapped memory regions.

### 1.2 Buffer Registration & Chunking (`tiny_cu_buf_register`)
This is the most critical and mathematically complex phase. 

**The Limitation:** The Linux NVMe driver and the `nvidia-fs` driver impose strict limits on the maximum size of a contiguous memory mapping and the maximum length of a Scatter-Gather List (SGL) used for DMA. This is typically limited by `max_direct_io_size` (usually 16MB). If a user attempts to map a 100MB GPU buffer, the kernel will reject it.

**The Solution:** `tiny_cufile` implements an automatic "chunking" architecture. When a user requests to register a buffer of size `S`:
1. The library splits `S` into chunks of `MAX_CHUNK_GPU` (16MB).
2. For each chunk, it calls `mmap()` to allocate a CPU shadow buffer (virtual address space).
3. It allocates a 4KB pinned host memory page (`cudaMallocHost`) to act as a **Mailbox** (`fence_page`).
4. It fires the `NVFS_IOCTL_MAP` command. The kernel locks the physical GPU memory pages, maps them to the CPU shadow buffer, and wires the `fence_page` to the NVMe DMA engine.
5. All chunks are bundled into a `tcufile_chunk_t` array and saved to the thread-safe State Manager, keyed by the original base device pointer (`devPtr_base`).

### 1.3 Direct Execution (`tiny_cu_write` & `tiny_cu_read`)
When a read or write is requested, the library queries the State Manager to retrieve the chunk array for the target pointer.

Because the buffer was mapped in 16MB slices, the I/O request must also be sliced. The execution loop:
1. Calculates the starting chunk: `curr_chunk = devPtr_offset / 16MB`.
2. Calculates the offset inside that specific chunk: `curr_chunk_offs = devPtr_offset % 16MB`.
3. Determines if the remaining I/O size spills over the boundary of the current 16MB chunk. If so, it truncates the `io_size` to the chunk boundary.
4. Resets the mailbox: `*fence_page = 0`.
5. Constructs the `nvfs_ioctl_ioargs` struct with the calculated offsets and the target file's inode (`inum`) and device mappings (`majdev`/`mindev`).
6. Fires `NVFS_IOCTL_WRITE` or `NVFS_IOCTL_READ`. The NVMe controller executes the DMA transfer and flips the `fence_page` to `1` or `2` upon completion.
7. The CPU advances the tracking pointers and loops until all bytes are transferred.

---

## 2. LD_PRELOAD Interceptor Design

The library is distributed with `src/preload.c`, which mirrors the exact C signatures defined in NVIDIA's official `cufile.h`.

When compiled as `libcufile.so` and injected via `LD_PRELOAD`, the Linux dynamic linker routes all `cuFile...` calls from the host application into our wrappers. 
Because `tiny_cufile` natively handles and returns `CUfileError_t` structs and identical opaque pointers (`void*`), the host application maintains perfect state compatibility and is unaware that it is interacting with a reverse-engineered kernel wrapper.

---

## 3. Current Limitations & Missing Features

While functionally robust for local block storage, `tiny_cufile` currently lacks several advanced GPUDirect Storage capabilities:

### 3.1 Synchronous Execution Only
Currently, the I/O struct is hardcoded with `param.ioargs.sync = 1`. This forces the kernel to block the calling CPU thread until the DMA transfer finishes. 
* **Missing:** Asynchronous CUDA stream integration (`cuFileWriteAsync`). True async would set `sync = 0` and allow the GPU's command queue to natively wait on the `fence_page` memory address without blocking the CPU.

### 3.2 No Batch I/O Support
* **Missing:** `NVFS_IOCTL_BATCH_IO` support. Currently, highly fragmented random I/O requires a standard `while` loop, executing an individual `ioctl()` system call for every slice. This incurs significant CPU context-switching overhead. The official driver supports batching hundreds of SGL requests into a single kernel transition to achieve extreme IOPS.

### 3.3 No RDMA / Network Filesystem Support
* **Missing:** `CU_FILE_RDMA_REGISTER` support. The library assumes local NVMe block storage via `OPAQUE_FD`. It does not support firing the `NVFS_IOCTL_SET_RDMA_REG_INFO` commands required to register GPU memory with InfiniBand/RoCE Network Interface Cards (NICs) for distributed storage clusters like WekaFS or Lustre.

### 3.4 Hardcoded Constraints
* **Missing:** Dynamic hardware limit detection. The library assumes a strict 16MB chunk limit. It does not invoke `cuFileDriverGetProperties` to dynamically read the PCI bridge and NVMe DMA SGL limits, which on some modern architectures can exceed 32MB.
