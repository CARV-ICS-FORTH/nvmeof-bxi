#ifndef MR_PORTALS_POOL_H
#define MR_PORTALS_POOL_H 1
#include <rdma/ib_verbs.h>
#include "mr_portals_pool.h"


#define GES_UNIMPL_RATELIMIT_PERIOD  HZ
#define GES_UNIMPL_RATELIMIT_BURST   10

static DEFINE_RATELIMIT_STATE(ges_unimpl_rs, GES_UNIMPL_RATELIMIT_PERIOD, GES_UNIMPL_RATELIMIT_BURST);

#define IB_PORTALS4_MR_POOL_UNIMPL(fmt, ...)    \
    do {    \
    if (__ratelimit(&ges_unimpl_rs))    \
    pr_warn("Portals4/ib_mr_pool: UNIMPLEMENTED: %s: " fmt "\n",    \
    __func__, ##__VA_ARGS__);    \
    } while (0)

#define IB_PORTALS4_MR_POOL_WARN_ONCE(fmt, ...)    \
    do {    \
    static bool __once;    \
    if (!__once) {    \
    __once = true;    \
    pr_warn("Portals4/ib_mr_pool: %s: " fmt "\n", __func__, ##__VA_ARGS__); \
    WARN_ON(1);    \
    }    \
    } while (0)


struct ib_mr *ib_portals_mr_pool_get(struct ib_qp *qp, struct list_head *list)
{
  IB_PORTALS4_MR_POOL_UNIMPL("Sorry!");
  return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_get);

void ib_portals_mr_pool_put(struct ib_qp *qp, struct list_head *list, struct ib_mr *mr)
{
  IB_PORTALS4_MR_POOL_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_put);

int ib_portals_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
		enum ib_mr_type type, u32 max_num_sg, u32 max_num_meta_sg)
{
  IB_PORTALS4_MR_POOL_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_init);

void ib_portals_mr_pool_destroy(struct ib_qp *qp, struct list_head *list)
{
  IB_PORTALS4_MR_POOL_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_destroy);

#endif
