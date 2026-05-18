#ifndef PTL_UUID_H
#define PTL_UUID_H

#include <assert.h>
#include <endian.h>
#include <stdint.h>

/* Map Linux kernel types to standard types */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Endianness helpers for user-space */
#define le16_to_cpu(x) le16toh(x)
#define cpu_to_le16(x) htole16(x)

/* In a nutshell 1 is reserved for target srq and 2 for RMA operations to the
 * initiator*/
#define PTL_UUID_TARGET_SRQ_MATCH_BITS 0x0001000000000000UL
#define PTL_UUID_RMA_MASK 0x0002000000000000UL
#define PTL_UUID_TARGET_COMPLETION_QUEUE_ID 0

#define PTL_UUID_SEND_RECV_MASK 0x4000000000000000UL

/* We use the 2 most significant bytes for match bits */
#define PTL_UUID_IGNORE_MASK 0x0000FFFFFFFFFFFFUL

/* User-space equivalent of kernel WARN_ON_ONCE */
#define WARN_ON_ONCE(condition)                                                \
  ({                                                                           \
    int __ret = !!(condition);                                                 \
    if (__ret)                                                                 \
      fprintf(stderr, "WARNING: %s at %s:%d\n", #condition, __FILE__,          \
              __LINE__);                                                       \
    __ret;                                                                     \
  })

/* User-space equivalent of GENMASK */
#define GENMASK(h, l) (((~0ULL) << (l)) & (~0ULL >> (64 - 1 - (h))))

#define PTL_SET_NVME_CID(X, Y) ((X)->tso.mss = Y)
#define PTL_GET_NVME_CID(X) ((X)->tso.mss)

#define PTL_SET_NVME_CID_PARTS_NO(X, Y) ((X)->tso.hdr_sz = Y)
#define PTL_GET_NVME_CID_PARTS_NO(X) ((X)->tso.hdr_sz)

#define PTL_OP_META_SET(X, Y) ((X)->tso.hdr = (void *)Y)
#define PTL_OP_META_GET(X) ((X)->tso.hdr)
#define PTL_OP_META_RESET(X) ((X)->tso.hdr = (void *)0)
#define PTL_OP_META_IS_SET(X) (X->tso.hdr != NULL)

struct ptl_uuid_nvmeof_cmd {
  u16 target_qp_num;
  u16 initiator_qp_num;
} __attribute((packed));

struct ptl_uuid_nvmeof_cpl {
  u16 cid;
  u16 total_parts;
} __attribute((packed));

struct ptl_uuid_nvmeof_rma {
  u16 cid;
  u16 future_extension_1;
} __attribute((packed));

struct ptl_uuid_open_conn {
  u16 future_extension_1;
  u16 future_extension_2;
} __attribute((packed));

struct ptl_uuid_open_conn_rep {
  u16 future_extension_1;
  u16 future_extension_2;
} __attribute((packed));

struct ptl_uuid_close_conn {
  u16 future_extension_1;
  u16 future_extension_2;
} __attribute((packed));

struct ptl_uuid_close_conn_rep {
  u16 future_extension_1;
  u16 future_extension_2;
} __attribute((packed));
/**
 * @brief Portals4 UUID Layout (8 Bytes / Little-Endian)
 */
typedef struct {
  u16 msg_type;
  u16 cq_id;
  union {
    struct ptl_uuid_nvmeof_cmd uuid_nvmeof_cmd;
    struct ptl_uuid_nvmeof_cpl uuid_nvmeof_cpl;
    struct ptl_uuid_nvmeof_rma uuid_nvmeof_rma;
    struct ptl_uuid_open_conn uuid_open_conn;
    struct ptl_uuid_open_conn_rep uuid_open_conn_rep;
    struct ptl_uuid_close_conn uuid_close_conn;
    struct ptl_uuid_close_conn_rep uuid_close_conn_rep;
  };
} __attribute__((packed)) ptl_uuid_t;

static_assert(sizeof(ptl_uuid_t) == 8, "ptl_uuid_t must be 8 bytes");

/* --- Zero-Copy Casting Helpers --- */

static inline const ptl_uuid_t *ptl_uuid_as_const_uuid(const u64 *raw) {
  return (const ptl_uuid_t *)raw;
}

static inline ptl_uuid_t *ptl_uuid_as_uuid(u64 *raw) {
  return (ptl_uuid_t *)raw;
}

u16 ptl_uuid_get_op_type(u64 *uuid);
void ptl_uuid_set_op_type(u64 *uuid, u16 op_type);

u16 ptl_uuid_get_nvme_cid(u64 *uuid);
void ptl_uuid_set_nvme_cid(u64 *uuid, u16 nvme_cid);

u16 ptl_uuid_get_total_parts(u64 *uuid);
void ptl_uuid_set_total_parts(u64 *uuid, u16 total_parts);

u16 ptl_uuid_get_cq_id(u64 *uuid);
void ptl_uuid_set_cq_id(u64 *uuid, u16 cq_id);

u16 ptl_uuid_get_target_qp_num(u64 *uuid);
void ptl_uuid_set_target_qp_num(u64 *uuid, u16 target_qp_num);

u16 ptl_uuid_get_initiator_qp_num(u64 *uuid);
void ptl_uuid_set_initiator_qp_num(u64 *uuid, u16 initiator_qp_num);

#endif /* PTL_UUID_H */
