#ifndef PTL_OBJECT_TYPES_H
#define PTL_OBJECT_TYPES_H 1
#define PTL_VERSION 1

#define PTL_FATAL_RATELIMIT_PERIOD HZ
#define PTL_FATAL_RATELIMIT_BURST 10
#define PTL_FATAL(fmt, ...)                                                    \
  do {                                                                         \
    pr_warn("[%s:%s:%d]PTL_FATAL " fmt "\n", __FILE__, __func__, __LINE__,     \
            ##__VA_ARGS__);                                                    \
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

#define PTL_WARN(fmt, ...)                                                     \
  do {                                                                         \
    pr_warn("\033[0;31m[%s:%s:%d]PTL_WARN " fmt "\033[0m\n", __FILE__,         \
            __func__, __LINE__, ##__VA_ARGS__);                                \
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
  PTL_BXIV3_DEVICE
} ptl_obj_type_e;
#endif
