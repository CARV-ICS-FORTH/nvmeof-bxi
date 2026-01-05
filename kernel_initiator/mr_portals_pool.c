#include "mr_portals_pool.h"
#include "asm-generic/errno-base.h"
#include "asm-generic/errno.h"
#include "linux/err.h"
#include "linux/gfp_types.h"
#include "linux/slab.h"
#include "ptl_object_types.h"
#include <linux/list.h>
#include <rdma/ib_verbs.h>

struct ptl_mr *ptl_mr_create(u32 max_num_sg, u32 max_num_meta_sg, enum ib_mr_type type)
{
	struct ptl_mr *ptl_mr;
	ptl_mr = kzalloc(sizeof(*ptl_mr), GFP_KERNEL);
	if (!ptl_mr)
		return ERR_PTR(-ENOMEM);
	ptl_mr->object_type = PTL_MR;
	ptl_mr->max_num_sg = max_num_sg;
	ptl_mr->max_num_meta_sg = max_num_meta_sg;
	ptl_mr->type = type;
	return ptl_mr;
}


void ptl_mr_destroy(struct ptl_mr *ptl_mr)
{
	if (!ptl_mr)
		return;
	kfree(ptl_mr);
}

struct ib_mr *ib_portals_mr_pool_get(struct ib_qp *qp, struct list_head *list)
{
	PTL_FATAL("Sorry! Unimplemented!");
	return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_get);

void ib_portals_mr_pool_put(struct ib_qp *qp, struct list_head *list,
                            struct ib_mr *mr)
{
	PTL_FATAL("Sorry!, Unimplemented!");
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_put);


int ib_portals_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
                            enum ib_mr_type type, u32 max_num_sg,
                            u32 max_num_meta_sg)
{
	struct ptl_mr *ptl_mr, *tmp;
	int i;
	int ret = 0;

	/* Sanity checks */
	if (!qp || !list || nr <= 0) {
		PTL_FATAL("Wrong params: qp is NULL? %s list is NULL? %s nr <= 0? %d", qp ? "NO" : "YES", list ? "NO" : "YES", nr);
		return -EINVAL;
	}

	for (i = 0; i < nr; i++) {
		ptl_mr = ptl_mr_create(max_num_sg, max_num_meta_sg, type);
		if (IS_ERR(ptl_mr)) {
			ret = PTR_ERR(ptl_mr);
			goto rollback;
		}
		list_add_tail(&ptl_mr->head, list);
	}
	PTL_DEBUG("OK with mr_pool init!");
	return 0;

rollback:
	PTL_WARN("Rolling back");
	/* Clean up all successfully allocated entries */
	list_for_each_entry_safe(ptl_mr, tmp, list, head) {
		list_del(&ptl_mr->head);
		ptl_mr_destroy(ptl_mr);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_init);

void ib_portals_mr_pool_destroy(struct ib_qp *qp, struct list_head *list)
{
	PTL_FATAL("Sorry! Unimplemented");
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_destroy);

