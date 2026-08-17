#ifndef PTL_OBJECT_TYPES_H
#define PTL_OBJECT_TYPES_H 1
#include <linux/delay.h>
#include <linux/ratelimit.h>
#define PTL_VERSION 1

#define PTL_MAGIC_FAKE_MR_KEY 0x12345678
#define PTL_FATAL_RATELIMIT_PERIOD HZ
#define PTL_FATAL_RATELIMIT_BURST 10


//og
// #define PTL_RMA_ME_OPTS (PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_EVENT_UNLINK_DISABLE | PTL_ME_EVENT_COMM_DISABLE)
#define PTL_RMA_LE_OPTS (PTL_LE_OP_PUT | PTL_LE_OP_GET | PTL_LE_EVENT_LINK_DISABLE | PTL_LE_EVENT_UNLINK_DISABLE)

/* Fatal error: log, delay so it reaches the logs, then BUG. The delay must not
 * sleep - most callers run from ptl_eq_drain() under drain_lock in a threaded
 * IRQ, where msleep() panics with "scheduling while atomic" first. */
#define PTL_FATAL(fmt, ...)                                                     \
  do {                                                                          \
    pr_err("[%s:%s:%d]PTL_FATAL " fmt "\n", __FILE__, __func__, __LINE__,       \
           ##__VA_ARGS__);                                                      \
    mdelay(20); /* non-sleeping: safe under drain_lock */                       \
    BUG();                                                                      \
  } while (0)

/* Rate-limited warning, for wire-driven conditions that must never be fatal.
 * Reuses the PTL_FATAL_RATELIMIT_* constants already defined above. */
#define PTL_WARN_RL(fmt, ...)                                                   \
  do {                                                                          \
    static DEFINE_RATELIMIT_STATE(_ptl_rs, PTL_FATAL_RATELIMIT_PERIOD,          \
                                  PTL_FATAL_RATELIMIT_BURST);                   \
    if (__ratelimit(&_ptl_rs))                                                  \
      pr_warn("[%s:%s:%d]PTL_WARN " fmt "\n", __FILE__, __func__, __LINE__,     \
              ##__VA_ARGS__);                                                   \
  } while (0)


#ifdef PTL_RELEASE
#define PTL_DEBUG(fmt, ...) do { } while (0)
#else
#define PTL_DEBUG(fmt, ...)                    \
  do {                    \
    pr_warn("[%s:%s:%d]PTL_DEBUG " fmt "\n", __FILE__, __func__, __LINE__,     \
            ##__VA_ARGS__);                    \
  } while (0)
#endif


#define PTL_INFO(fmt, ...)                                                     \
  do {                                                                         \
    pr_warn("[%s:%s:%d]PTL_INFO " fmt "\n", __FILE__, __func__, __LINE__,      \
            ##__VA_ARGS__);                                                    \
  } while (0)

#define PTL_WARN(fmt, ...)                                                    \
  do {                                                                         \
    pr_warn("[%s:%s:%d]PTL_WARN " fmt "\n", __FILE__, __func__, __LINE__,     \
            ##__VA_ARGS__);                                                    \
  } while (0)

#define PTL_CHECK(ptr, value) \
    do { \
        if ((ptr)->object_type != (value)) { \
            PTL_FATAL("Corrupted type"); \
        } \
    } while (0)

#define PTL_CHECK_NVME_CID(event, ptl_qp, nvme_cid) do { \
    uint16_t calculated_cid = (((u64)event->start - (u64)ptl_qp->ptl_id->nvme_cpl_start) / sizeof(struct nvme_completion)); \
    if (calculated_cid != (nvme_cid)) { \
        PTL_FATAL("Corrupted nvme_cid value: %u calculated: %u",nvme_cid,calculated_cid); \
    } \
} while (0)

typedef enum ptl_obj_type {
	PTL_RECV_OP = 100,
	PTL_SEND_OP,
	PTL_RDMA_WRITE_OP,
	PTL_RDMA_READ_OP,
	PTL_CONTEXT,
	PTL_PD,
	PTL_QP,
	PTL_STATIC_CQ,
	PTL_CQ,
	PTL_CM_ID,
	PTL_SRQ,
	PTL_MEM_DESC_LOCAL,
	PTL_MEM_DESC_REMOTE,
	PTL_CQ_POOL,
	PTL_BXIV3_DEVICE,
	PTL_CONN_RECV_BUFFER,
	PTL_CONN_SEND_BUFFER,
	PTL_MR
} ptl_obj_type_e;

struct ptl_obj_conn_params {
	u64 nvme_cpl_start_dma_addr;
	size_t queue_size;
};
#endif
