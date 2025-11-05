#ifndef MR_PORTALS_POOL_H
#define MR_PORTALS_POOL_H 1
#include <rdma/mr_pool.h>

struct ib_mr *ib_portals_mr_pool_get(struct ib_qp *qp, struct list_head *list);
void ib_portals_mr_pool_put(struct ib_qp *qp, struct list_head *list, struct ib_mr *mr);

int ib_portals_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
		enum ib_mr_type type, u32 max_num_sg, u32 max_num_meta_sg);
void ib_portals_mr_pool_destroy(struct ib_qp *qp, struct list_head *list);

#endif
