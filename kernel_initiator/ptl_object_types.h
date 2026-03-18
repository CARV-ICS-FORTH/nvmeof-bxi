#include "ptl_connection.h"
#ifndef PTL_OBJECT_TYPES_H
#define PTL_OBJECT_TYPES_H 1
#include <linux/delay.h>
#define PTL_VERSION 1

#define PTL_MAGIC_FAKE_MR_KEY 0x12345678
#define PTL_FATAL_RATELIMIT_PERIOD HZ
#define PTL_FATAL_RATELIMIT_BURST 10


//og
// #define PTL_RMA_ME_OPTS (PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_EVENT_UNLINK_DISABLE | PTL_ME_EVENT_COMM_DISABLE)
#define PTL_RMA_LE_OPTS (PTL_LE_OP_PUT | PTL_LE_OP_GET | PTL_LE_EVENT_LINK_DISABLE | PTL_LE_EVENT_UNLINK_DISABLE)

/* Fatal error: log, delay a bit so it reaches logs, then BUG */
#define PTL_FATAL(fmt, ...)                                                     \
  do {                                                                          \
    pr_err("[%s:%s:%d]PTL_FATAL " fmt "\n", __FILE__, __func__, __LINE__,       \
           ##__VA_ARGS__);                                                      \
    msleep(1000); /* sleep 1 second to avoid losing the log */                  \
    BUG();                                                                      \
  } while (0)

#define PTL_DEBUG(fmt, ...)                                                    \
  do {                                                                         \
    pr_warn("[%s:%s:%d]PTL_DEBUG " fmt "\n", __FILE__, __func__, __LINE__,     \
            ##__VA_ARGS__);                                                    \
  } while (0)

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
