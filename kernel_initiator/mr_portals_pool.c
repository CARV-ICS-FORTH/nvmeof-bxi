#ifndef MR_PORTALS_POOL_H
#define MR_PORTALS_POOL_H 1
#include "mr_portals_pool.h"
#include "ptl_object_types.h"
#include <rdma/ib_verbs.h>

struct ib_mr *ib_portals_mr_pool_get(struct ib_qp *qp, struct list_head *list) {
  PTL_FATAL("Sorry!");
  return ERR_PTR(-EOPNOTSUPP);
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_get);

void ib_portals_mr_pool_put(struct ib_qp *qp, struct list_head *list,
                            struct ib_mr *mr) {
  PTL_FATAL("Sorry!");
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_put);

int ib_portals_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
                            enum ib_mr_type type, u32 max_num_sg,
                            u32 max_num_meta_sg) {
  PTL_FATAL("Sorry!");
  return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_init);

void ib_portals_mr_pool_destroy(struct ib_qp *qp, struct list_head *list) {
  PTL_FATAL("Sorry!");
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_destroy);

#endif
