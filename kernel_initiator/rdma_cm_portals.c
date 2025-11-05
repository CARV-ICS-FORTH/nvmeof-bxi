// SPDX-License-Identifier: GPL-2.0
/*
 * Fake RDMA-CM "portals" shim — no-op / fail-fast mock for NVMe-RDMA experimentation
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/ratelimit.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <rdma/rdma_cm.h>
#include <rdma/rdma_user_cm.h>
/* --- Rate-limited "unimplemented" logging ---- */
#define GES_UNIMPL_RATELIMIT_PERIOD  HZ
#define GES_UNIMPL_RATELIMIT_BURST   10
static DEFINE_RATELIMIT_STATE(ges_unimpl_rs, GES_UNIMPL_RATELIMIT_PERIOD,
    GES_UNIMPL_RATELIMIT_BURST);

#define RDMACM_IB_UNIMPL(fmt, ...)    \
    do {    \
    if (__ratelimit(&ges_unimpl_rs))    \
    pr_warn("rdma_cm GES: UNIMPLEMENTED: %s: " fmt "\n",    \
    __func__, ##__VA_ARGS__);    \
    } while (0)


/* --- Module‑param toggle for success/failure ---- */
static bool ges_fail_fast = true;
module_param_named(fail_fast, ges_fail_fast, bool, 0644);
MODULE_PARM_DESC(fail_fast,
    "If true, return -EOPNOTSUPP for all ops; if false, always succeed.");

/* --- Helpers ---- */
static inline int ges_ret(int fail_code)
{
    (void)fail_code;
    return ges_fail_fast ? fail_code : 0;
}

/* --- API stubs ---- */

struct rdma_cm_id *
__rdma_cm_portals_create_kernel_id(struct net *net, rdma_cm_event_handler event_handler,
			void *context, enum rdma_ucm_port_space ps,
			enum ib_qp_type qp_type, const char *caller)
{
    (void)net;
    (void)event_handler;
    (void)context;
    (void)ps;
    RDMACM_IB_UNIMPL("Sorry");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(__rdma_cm_portals_create_kernel_id);

int rdma_cm_portals_destroy_id(struct rdma_cm_id *id)
{
    (void)id;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_destroy_id);

int rdma_cm_portals_resolve_addr(struct rdma_cm_id *id,
    const void *src, const void *dst,
    unsigned long timeout_ms)
{
    (void)id;
    (void)src;
    (void)dst;
    (void)timeout_ms;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_resolve_addr);

int rdma_cm_portals_resolve_route(struct rdma_cm_id *id,
    unsigned long timeout_ms)
{
    (void)id;
    (void)timeout_ms;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_resolve_route);

int rdma_cm_portals_connect_locked(struct rdma_cm_id *id,
    struct rdma_conn_param *param)
{
    (void)id;
    (void)param;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_connect_locked);

int rdma_cm_portals_disconnect(struct rdma_cm_id *id)
{
    (void)id;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_disconnect);

int rdma_cm_portals_create_qp(struct rdma_cm_id *id,
    struct ib_pd *pd,
    struct ib_qp_init_attr *attr)
{
    (void)id;
    (void)pd;
    (void)attr;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_create_qp);

int rdma_cm_portals_destroy_qp(struct rdma_cm_id *id)
{
    (void)id;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_destroy_qp);

/* --- Helpers that return strings/data ---- */

const char *rdma_cm_portals_event_msg(int ev)
{
    (void)ev;
    RDMACM_IB_UNIMPL("Sorry");
    return "RDMACM-IB-unimplemented";
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_event_msg);

const void *rdma_cm_portals_reject_msg(struct rdma_cm_id *id, int status)
{
    (void)id;
    (void)status;
    RDMACM_IB_UNIMPL("Sorry");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_reject_msg);


const void *rdma_cm_portals_consumer_reject_data(struct rdma_cm_id *id, struct rdma_cm_event *ev, u8 *data_len)
{
    (void)id;
    (void)ev;
    (void)data_len;
    RDMACM_IB_UNIMPL("Sorry");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_consumer_reject_data);

int rdma_cm_portals_set_service_type(struct rdma_cm_id *id, u8 tos)
{
    (void)id;
    (void)tos;
    RDMACM_IB_UNIMPL("Sorry");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_set_service_type);


int rdma_cm_portals_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
  (void)id;
  (void)conn_param;
  RDMACM_IB_UNIMPL("Sorry");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_connect);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RDMACM implementation over Portals4 shim (no-op / fail-fast)");
MODULE_AUTHOR("Giorgis Saloustris (Chatzis) gesalous@ics.forth.gr");
