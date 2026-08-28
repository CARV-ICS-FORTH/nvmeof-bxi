#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h> /* BLKGETSIZE64 */
#endif

#define CRC_BLOCK_SIZE 8192 /* one CRC-stamped unit: 8 KB              */
#define MAGIC                                                                  \
  0xDEADBEEFu /* magic number stamped at start of each 8 KB block              \
               */
#define CRC_PAYLOAD_SIZE                                                       \
  (CRC_BLOCK_SIZE - sizeof(uint32_t)) /* 8184 bytes (excl. magic + crc) */
#define SG_SEGMENT_SIZE 4096 /* fixed 4 KB per SG segment               */
#define PROGRESS_INTERNVAL 1

/* ---- */
/* CRC32 (ISO 3309 / Ethernet polynomial, reflected)                   */
/* ---- */
static uint32_t crc32_table[256];

static void init_crc32_table(void) {
  uint32_t polynomial = 0xEDB88320u;
  for (int i = 0; i < 256; i++) {
    uint32_t c = (uint32_t)i;
    for (int j = 0; j < 8; j++)
      c = (c & 1) ? (polynomial ^ (c >> 1)) : (c >> 1);
    crc32_table[i] = c;
  }
}

static uint32_t calculate_crc32(const void *data, size_t length) {
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFu;
  while (length--)
    crc = crc32_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

/* ---- */
/* Little-endian 32-bit helpers                    */
/* ---- */
static void store_u32_le(void *dst, uint32_t value) {
  uint8_t *p = (uint8_t *)dst;
  p[0] = (uint8_t)(value & 0xFF);
  p[1] = (uint8_t)((value >> 8) & 0xFF);
  p[2] = (uint8_t)((value >> 16) & 0xFF);
  p[3] = (uint8_t)((value >> 24) & 0xFF);
}

/* ---- */
/* Random data                    */
/* ---- */
static void generate_random_data(void *data, size_t length) {
  store_u32_le(data, MAGIC);
  uint8_t *p = &((uint8_t *)data)[sizeof(uint32_t)];
  for (size_t i = sizeof(uint32_t); i < length; i++)
    p[i] = (uint8_t)(rand() % 256);
}

static uint32_t load_u32_le(const void *src) {
  const uint8_t *p = (const uint8_t *)src;
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/* ---- */
/* Fill buffer: magic in first 4 bytes, random payload in next         */
/* CRC_PAYLOAD_SIZE bytes, CRC32 of (magic + random) in last 4 bytes   */
/* ---- */
static void fill_8k_blocks_with_crc(void *data, size_t length) {
  uint8_t *p = (uint8_t *)data;
  size_t num_blocks = length / CRC_BLOCK_SIZE;
  for (size_t b = 0; b < num_blocks; b++) {
    uint8_t *block = p + b * CRC_BLOCK_SIZE;
    generate_random_data(block, CRC_PAYLOAD_SIZE);
    uint32_t crc = calculate_crc32(block, CRC_PAYLOAD_SIZE);
    store_u32_le(block + CRC_PAYLOAD_SIZE, crc);
  }
}

/* ---- */
/* Verify buffer: check CRC32 of each 8 KB block after read            */
/* Returns 1 if at least one block is corrupted, 0 otherwise.          */
/* ---- */
static int verify_8k_blocks_with_crc(const void *data, size_t length,
                                     off_t base_offset) {
  const uint8_t *p = (const uint8_t *)data;
  size_t num_blocks = length / CRC_BLOCK_SIZE;
  int corrupted = 0;

  for (size_t b = 0; b < num_blocks; b++) {
    const uint8_t *block = p + b * CRC_BLOCK_SIZE;
    uint32_t stored_crc = load_u32_le(block + CRC_PAYLOAD_SIZE);
    uint32_t calculated_crc = calculate_crc32(block, CRC_PAYLOAD_SIZE);
    int bad = (stored_crc != calculated_crc);

    printf("  8K block %4zu  offset %lld  : %s"
           "  (stored=0x%08X  calc=0x%08X)\n",
           b, (long long)(base_offset + (off_t)(b * CRC_BLOCK_SIZE)),
           bad ? "CORRUPTED" : "OK       ", stored_crc, calculated_crc);

    if (bad)
      corrupted = 1;
  }
  return corrupted;
}

/* ---- */
/* Misc helpers                    */
/* ---- */
static size_t pick_random_slot(size_t num_slots, ssize_t avoid1,
                               ssize_t avoid2) {
  if (num_slots == 1)
    return 0;
  size_t slot;
  do {
    slot = (size_t)(rand() % (int)num_slots);
  } while ((ssize_t)slot == avoid1 || (ssize_t)slot == avoid2);
  return slot;
}

static void usage(const char *prog) {
  fprintf(
      stderr,
      "Usage: %s <path> <size_in_mb> <io_size_in_kb> "
      "<num_operations> [-sg] [-arena_mb <size_mb>]\n"
      "\n"
      "  path            : /dev/nvme... (or any block device) for raw\n"
      "                    device I/O, or a directory path where\n"
      "                    test_file.dat will be created\n"
      "  size_in_mb      : test region size in MB (must be > 0);\n"
      "                    for block devices the value is capped to\n"
      "                    the actual device size\n"
      "  io_size_in_kb   : I/O size in KB (must be >= 8 and a multiple of 8)\n"
      "  num_operations  : number of random write+verify cycles\n"
      "  -sg             : scatter/gather mode — issues writev/readv\n"
      "                    using arena-backed SG segments\n"
      "  -arena_mb N     : mmap arena size in MB for random buffer\n"
      "                    placement (default: 256 MB)\n",
      prog);
  exit(EXIT_FAILURE);
}

static int is_device_path(const char *path) {
  return strncmp(path, "/dev/", 5) == 0;
}

/* ---- */
/* main                    */
/* ---- */
int main(int argc, char *argv[]) {
  if (argc < 5 || argc > 8)
    usage(argv[0]);

  char *directory = argv[1];
  long size_mb = atol(argv[2]);
  long io_size_kb = atol(argv[3]);
  long num_operations = atol(argv[4]);
  long arena_size_mb = 256;
  int sg_mode = 0;

  for (int argi = 5; argi < argc; argi++) {
    if (strcmp(argv[argi], "-sg") == 0) {
      sg_mode = 1;
    } else if (strcmp(argv[argi], "-arena_mb") == 0) {
      if (argi + 1 >= argc) {
        fprintf(stderr, "Missing value for -arena_mb\n");
        usage(argv[0]);
      }
      arena_size_mb = atol(argv[++argi]);
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[argi]);
      usage(argv[0]);
    }
  }

  if (size_mb <= 0) {
    fprintf(stderr, "Size must be a positive integer\n");
    exit(EXIT_FAILURE);
  }
  if (io_size_kb < 8 || (io_size_kb % 8) != 0) {
    fprintf(stderr, "I/O size must be >= 8 KB and a multiple of 8 KB\n");
    exit(EXIT_FAILURE);
  }
  if (num_operations <= 0) {
    fprintf(stderr, "Number of operations must be a positive integer\n");
    exit(EXIT_FAILURE);
  }
  if (arena_size_mb <= 0) {
    fprintf(stderr, "Arena size must be a positive integer\n");
    exit(EXIT_FAILURE);
  }

  init_crc32_table();
  srand((unsigned int)time(NULL));

  int raw_device = is_device_path(directory);
  char filepath[1024];
  int fd;

  size_t file_size = (size_t)size_mb * 1024 * 1024;
  size_t io_size = (size_t)io_size_kb * 1024;

  if (raw_device) {
    snprintf(filepath, sizeof(filepath), "%s", directory);
    fd = open(filepath, O_RDWR | O_DIRECT | O_SYNC);
    if (fd == -1) {
      perror("Error opening block device");
      exit(EXIT_FAILURE);
    }
#ifdef BLKGETSIZE64
    uint64_t dev_bytes = 0;
    if (ioctl(fd, BLKGETSIZE64, &dev_bytes) == 0) {
      if (file_size > dev_bytes) {
        fprintf(stderr,
                "Requested size (%zu MB) exceeds device size (%llu MB); "
                "capping to device size.\n",
                (size_t)size_mb,
                (unsigned long long)(dev_bytes / (1024 * 1024)));
        file_size = (size_t)dev_bytes;
      }
      printf("Device size     : %llu MB\n",
             (unsigned long long)(dev_bytes / (1024 * 1024)));
    } else {
      perror("Warning: could not query device size via BLKGETSIZE64");
      fprintf(stderr, "Proceeding with requested size of %ld MB.\n", size_mb);
    }
#else
    fprintf(stderr,
            "Warning: BLKGETSIZE64 not available; cannot verify device size.\n"
            "Proceeding with requested size of %ld MB.\n",
            size_mb);
#endif
  } else {
    snprintf(filepath, sizeof(filepath), "%s/test_file.dat", directory);
    struct stat st = {0};
    if (stat(directory, &st) == -1)
      mkdir(directory, 0700);

    fd = open(filepath, O_CREAT | O_RDWR | O_DIRECT | O_SYNC, 0644);
    if (fd == -1) {
      perror("Error opening file");
      exit(EXIT_FAILURE);
    }
    if (ftruncate(fd, (off_t)file_size) == -1) {
      perror("Error extending file");
      close(fd);
      exit(EXIT_FAILURE);
    }
  }

  /* io_size is a multiple of 8 KB; verify it also satisfies O_DIRECT alignment
   */
  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  if (io_size % page_size != 0) {
    fprintf(
        stderr,
        "I/O size (%zu bytes) is not aligned to system page size (%zu bytes)\n",
        io_size, page_size);
    close(fd);
    exit(EXIT_FAILURE);
  }

  if (io_size > file_size) {
    fprintf(
        stderr,
        "I/O size (%zu bytes) cannot be larger than file size (%zu bytes)\n",
        io_size, file_size);
    close(fd);
    exit(EXIT_FAILURE);
  }

  size_t arena_size = (size_t)arena_size_mb * 1024 * 1024;
  size_t arena_slots = arena_size / io_size;

  if (arena_slots < 2) {
    fprintf(stderr,
            "Arena size (%zu MB) must provide at least 2 I/O-sized slots "
            "(need >= %zu MB)\n",
            (size_t)arena_size_mb,
            (2 * io_size + (1024 * 1024 - 1)) / (1024 * 1024));
    close(fd);
    exit(EXIT_FAILURE);
  }

  arena_size = arena_slots * io_size; /* trim to exact multiple */

  void *arena = mmap(NULL, arena_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (arena == MAP_FAILED) {
    perror("Error mapping arena");
    close(fd);
    exit(EXIT_FAILURE);
  }

  int num_segs = sg_mode ? (int)(io_size / SG_SEGMENT_SIZE) : 1;
  struct iovec *wiov = NULL;
  struct iovec *riov = NULL;

  if (sg_mode) {
    wiov = malloc((size_t)num_segs * sizeof(struct iovec));
    riov = malloc((size_t)num_segs * sizeof(struct iovec));
    if (!wiov || !riov) {
      perror("Error allocating iovec arrays");
      free(wiov);
      free(riov);
      munmap(arena, arena_size);
      close(fd);
      exit(EXIT_FAILURE);
    }
    for (int s = 0; s < num_segs; s++) {
      wiov[s].iov_len = SG_SEGMENT_SIZE;
      riov[s].iov_len = SG_SEGMENT_SIZE;
    }
  }

  printf("Testing %s : %s\n", raw_device ? "device" : "file  ", filepath);
  printf("%s size    : %zu MB\n", raw_device ? "Test region" : "File       ",
         file_size / (1024 * 1024));
  printf("I/O size        : %zu KB (%zu bytes)\n", io_size / 1024, io_size);
  printf("8K blocks/IO    : %zu\n", io_size / CRC_BLOCK_SIZE);
  printf("Mode            : %s\n",
         sg_mode ? "scatter/gather (writev/readv)" : "normal (pwrite/pread)");
  printf("Arena           : %zu MB, %zu slots of %zu bytes\n",
         arena_size / (1024 * 1024), arena_slots, io_size);
  if (sg_mode)
    printf("SG segments     : %d x %d bytes\n", num_segs, SG_SEGMENT_SIZE);
  printf("Num operations  : %ld\n\n", num_operations);

  ssize_t last_write_slot = -1;
  ssize_t last_read_slot = -1;

  for (long i = 0; i < num_operations; i++) {
    off_t max_offset = (off_t)(file_size - io_size);
    off_t offset =
        (off_t)((rand() % (int)(max_offset / (off_t)io_size)) * (off_t)io_size);

    size_t wslot = pick_random_slot(arena_slots, last_read_slot, -1);
    size_t rslot =
        pick_random_slot(arena_slots, (ssize_t)wslot, last_write_slot);
    last_write_slot = (ssize_t)wslot;
    last_read_slot = (ssize_t)rslot;

    void *write_buf = (char *)arena + wslot * io_size;
    void *read_buf = (char *)arena + rslot * io_size;

    /* ---- fill: random payload + CRC32 in last 4 bytes of every 8 KB ---- */
    fill_8k_blocks_with_crc(write_buf, io_size);

    if (sg_mode) {
      /* point iovecs into the chosen arena slots */
      for (int s = 0; s < num_segs; s++) {
        wiov[s].iov_base = (char *)write_buf + s * SG_SEGMENT_SIZE;
        riov[s].iov_base = (char *)read_buf + s * SG_SEGMENT_SIZE;
      }

      /* writev */
      if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("lseek (write) failed");
        munmap(arena, arena_size);
        close(fd);
        exit(EXIT_FAILURE);
      }
      ssize_t bytes_written = writev(fd, wiov, num_segs);
      if (bytes_written != (ssize_t)io_size) {
        perror("writev failed");
        munmap(arena, arena_size);
        close(fd);
        exit(EXIT_FAILURE);
      }

      /* readv */
      if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("lseek (read) failed");
        munmap(arena, arena_size);
        close(fd);
        exit(EXIT_FAILURE);
      }
      ssize_t bytes_read = readv(fd, riov, num_segs);
      if (bytes_read != (ssize_t)io_size) {
        perror("readv failed");
        munmap(arena, arena_size);
        close(fd);
        exit(EXIT_FAILURE);
      }

    } else {
      // /* print CRC of each 8K block before pwrite */
      // printf("Op %ld  pre-write CRC check (%zu 8K block(s)):\n", i + 1,
      //        io_size / CRC_BLOCK_SIZE);
      // {
      //   const uint8_t *wp = (const uint8_t *)write_buf;
      //   size_t num_blocks = io_size / CRC_BLOCK_SIZE;
      //   for (size_t b = 0; b < num_blocks; b++) {
      //     const uint8_t *block = wp + b * CRC_BLOCK_SIZE;
      //     uint32_t crc = load_u32_le(block + CRC_PAYLOAD_SIZE);
      //     printf("  8K block %4zu  offset %lld  : CRC=0x%08X\n", b,
      //            (long long)(offset + (off_t)(b * CRC_BLOCK_SIZE)), crc);
      //   }
      // }

      /* pwrite */
      printf("Op %ld  write: buf=0x%lx  offset=%lld ...\n", i + 1,
             (unsigned long)(uintptr_t)write_buf, (long long)offset);
      ssize_t bytes_written = pwrite(fd, write_buf, io_size, offset);
      if (bytes_written != (ssize_t)io_size) {
        perror("pwrite failed");
        munmap(arena, arena_size);
        close(fd);
        exit(EXIT_FAILURE);
      }
      printf("Op %ld  write: buf=0x%lx  offset=%lld ... DONE\n", i + 1,
             (unsigned long)(uintptr_t)write_buf, (long long)offset);

      /* pread */
      printf("Op %ld  read : buf=0x%lx  offset=%lld ...\n", i + 1,
             (unsigned long)(uintptr_t)read_buf, (long long)offset);
      ssize_t bytes_read = pread(fd, read_buf, io_size, offset);
      if (bytes_read != (ssize_t)io_size) {
        perror("pread failed");
        munmap(arena, arena_size);
        close(fd);
        exit(EXIT_FAILURE);
      }
      printf("Op %ld  read : buf=0x%lx  offset=%lld ... DONE\n", i + 1,
             (unsigned long)(uintptr_t)read_buf, (long long)offset);
    }

    /* ---- verify: check CRC32 of every 8 KB block in the read buffer ---- */
    printf("Op %ld  verifying %zu 8K block(s) at file offset %lld:\n", i + 1,
           io_size / CRC_BLOCK_SIZE, (long long)offset);

    int has_corruption = verify_8k_blocks_with_crc(read_buf, io_size, offset);

    if (has_corruption) {
      printf("FATAL: corruption detected in op %ld at offset %lld successfull "
             "IOs are: %lu\n",
             i + 1, (long long)offset,
             num_operations == 0 ? 0 : num_operations - 1);
      if (sg_mode) {
        free(wiov);
        free(riov);
      }
      munmap(arena, arena_size);
      close(fd);
      exit(EXIT_FAILURE);
    }

    if ((i + 1) % PROGRESS_INTERNVAL == 0 || i == num_operations - 1)
      printf("Completed %ld/%ld operations\n\n", i + 1, num_operations);
  }

  printf("All I/O operations completed successfully!\n");

  if (sg_mode) {
    free(wiov);
    free(riov);
  }
  munmap(arena, arena_size);
  close(fd);
  return 0;
}
