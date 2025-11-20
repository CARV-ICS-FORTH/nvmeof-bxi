// SPDX-License-Identifier: GPL-2.0
/*
 * Fake RDMA-CM "portals" shim — no-op / fail-fast mock for NVMe-RDMA
 * experimentation
 */

#include "rdma_cm_portals.h"
#include "ptl_cm_id.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include "ptl_qp.h"
#include <asm-generic/errno-base.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/inet.h> //for printing sockaddr
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/socket.h> //for printing sockaddr
#include <net/sock.h>     //for printing sockaddr
#include <portals4.h>
#include <rdma/rdma_cm.h>
#include <rdma/rdma_user_cm.h>
/* --- Rate-limited "unimplemented" logging ---- */
#define GES_UNIMPL_RATELIMIT_PERIOD HZ
#define GES_UNIMPL_RATELIMIT_BURST 10
static DEFINE_RATELIMIT_STATE(ges_unimpl_rs, GES_UNIMPL_RATELIMIT_PERIOD,
                              GES_UNIMPL_RATELIMIT_BURST);

#define RDMACM_IB_UNIMPL(fmt, ...)                                             \
  do {                                                                         \
    if (__ratelimit(&ges_unimpl_rs))                                           \
      pr_warn("[%s:%s:%d]UNIMPLEMENTED " fmt "\n", __FILE__, __func__,         \
              __LINE__, ##__VA_ARGS__);                                        \
  } while (0)

/* --- Module‑param toggle for success/failure ---- */
static bool ges_fail_fast = true;
module_param_named(fail_fast, ges_fail_fast, bool, 0644);
MODULE_PARM_DESC(
    fail_fast,
    "If true, return -EOPNOTSUPP for all ops; if false, always succeed.");

/* --- Helpers ---- */
static inline int ges_ret(int fail_code) {
  (void)fail_code;
  return ges_fail_fast ? fail_code : 0;
}

/* --- API stubs ---- */

struct rdma_cm_id *__rdma_cm_portals_create_kernel_id(
    struct net *net, rdma_cm_event_handler event_handler, void *context,
    enum rdma_ucm_port_space ps, enum ib_qp_type qp_type, const char *caller) {
  struct ptl_cm_id *ptl_cm_id =
      ptl_cm_id_create(net, event_handler, context, ps, qp_type, caller);
  return &ptl_cm_id->fake_cm_id;
}

EXPORT_SYMBOL_GPL(__rdma_cm_portals_create_kernel_id);

int rdma_cm_portals_destroy_id(struct rdma_cm_id *id) {
  (void)id;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_destroy_id);

#include <linux/inet.h>
#include <linux/socket.h>
#include <net/sock.h>

/**
 * print_sockaddr - Print details of a struct sockaddr
 * @addr: Pointer to struct sockaddr to print
 * @addrlen: Length of the address structure
 */
// static void rdma_cm_portals_print_sockaddr(const struct sockaddr *addr, int
// addrlen)
// {
//     if (!addr) {
//         pr_info("sockaddr: NULL\n");
//         return;
//     }

//     pr_info("sockaddr family: %d, length: %d\n", addr->sa_family, addrlen);

//     switch (addr->sa_family) {
//     case AF_INET: {
//         struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
//         pr_info("  IPv4 Address: %pI4:%d\n",
//                 &addr_in->sin_addr.s_addr,
//                 ntohs(addr_in->sin_port));
//         break;
//     }
//     case AF_INET6: {
//         struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
//         pr_info("  IPv6 Address: %pI6c:%d\n",
//                 &addr_in6->sin6_addr,
//                 ntohs(addr_in6->sin6_port));
//         pr_info("  Flow info: 0x%08x, Scope ID: %d\n",
//                 addr_in6->sin6_flowinfo,
//                 addr_in6->sin6_scope_id);
//         break;
//     }
//     case AF_UNSPEC:
//         pr_info("  Unspecified address family\n");
//         break;
//     default:
//         pr_info("  Unknown/unsupported address family\n");
//         break;
//     }
// }

int rdma_cm_portals_resolve_addr(struct rdma_cm_id *id,
                                 struct sockaddr *src_addr,
                                 const struct sockaddr *dst_addr,
                                 unsigned long timeout_ms) {
  struct rdma_cm_event event = {0};
  struct ptl_cm_id *ptl_cm_id = container_of(id, struct ptl_cm_id, fake_cm_id);
  int ret;
  if (!id)
    return -EINVAL;

  if (!dst_addr)
    return -EINVAL;

  if (ptl_cm_id->object_type != PTL_CM_ID) {
    PTL_FATAL("Corrupted ptl_cm_id");
    return -EINVAL;
  }

  ret = ptl_cm_id_resolve_addr(ptl_cm_id, src_addr, dst_addr, timeout_ms);
  if (ret) {
    event.event = RDMA_CM_EVENT_ADDR_ERROR;
    event.status = -ENETUNREACH;
  } else {
    event.event = RDMA_CM_EVENT_ADDR_RESOLVED;
  }
  return ptl_cm_id->event_handler(&ptl_cm_id->fake_cm_id, &event);
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_resolve_addr);

int rdma_cm_portals_resolve_route(struct rdma_cm_id *id,
                                  unsigned long timeout_ms) {
  (void)id;
  (void)timeout_ms;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_resolve_route);

int rdma_cm_portals_connect_locked(struct rdma_cm_id *id,
                                   struct rdma_conn_param *param) {
  (void)id;
  (void)param;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_connect_locked);

int rdma_cm_portals_disconnect(struct rdma_cm_id *id) {
  (void)id;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_disconnect);

int rdma_cm_portals_create_qp(struct rdma_cm_id *id, struct ib_pd *pd,
                              struct ib_qp_init_attr *attr) {
  struct ptl_cm_id *ptl_id;
  struct ptl_pd *ptl_pd;
  ptl_id = container_of(id, struct ptl_cm_id, fake_cm_id);
  if (PTL_CM_ID != ptl_id->object_type) {
    PTL_FATAL("Corrupted ptl_cm_id!");
    return -EINVAL;
  }

  ptl_pd = container_of(pd, struct ptl_pd, fake_pd);
  if (PTL_PD != ptl_pd->object_type) {
    PTL_FATAL("Corrupted ptl_pd!");
    return -EINVAL;
  }
  ptl_id->ptl_qp = ptl_qp_create(ptl_id, ptl_pd, attr);
  PTL_DEBUG("Created QP successfully");
  return IS_ERR(ptl_id->ptl_qp) ? -EINVAL : 0;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_create_qp);

int rdma_cm_portals_destroy_qp(struct rdma_cm_id *id) {
  (void)id;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_destroy_qp);

/* --- Helpers that return strings/data ---- */

const char *rdma_cm_portals_event_msg(int ev) {
  (void)ev;
  RDMACM_IB_UNIMPL("Sorry");
  return "RDMACM-IB-unimplemented";
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_event_msg);

const void *rdma_cm_portals_reject_msg(struct rdma_cm_id *id, int status) {
  (void)id;
  (void)status;
  RDMACM_IB_UNIMPL("Sorry");
  return ERR_PTR(-EOPNOTSUPP);
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_reject_msg);

const void *rdma_cm_portals_consumer_reject_data(struct rdma_cm_id *id,
                                                 struct rdma_cm_event *ev,
                                                 u8 *data_len) {
  (void)id;
  (void)ev;
  (void)data_len;
  RDMACM_IB_UNIMPL("Sorry");
  return ERR_PTR(-EOPNOTSUPP);
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_consumer_reject_data);

int rdma_cm_portals_set_service_type(struct rdma_cm_id *id, u8 tos) {
  (void)id;
  (void)tos;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_set_service_type);

int rdma_cm_portals_connect(struct rdma_cm_id *id,
                            struct rdma_conn_param *conn_param) {
  (void)id;
  (void)conn_param;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_connect);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(
    "RDMACM implementation over Portals4 shim (no-op / fail-fast)");
MODULE_AUTHOR("Giorgis Saloustris (Chatzis) gesalous@ics.forth.gr");
