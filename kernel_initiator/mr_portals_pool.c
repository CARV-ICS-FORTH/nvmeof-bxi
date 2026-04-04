#include "mr_portals_pool.h"
#include "asm-generic/errno.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cm_id.h"
#include "ptl_object_types.h"
#include "ptl_qp.h"
#include <asm-generic/errno-base.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/gfp_types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <rdma/ib_verbs.h>

struct ptl_mr *ptl_mr_create(u32 max_num_sg, u32 max_num_meta_sg,
                             enum ib_mr_type type,
                             struct ptl_bxiv3_device *bxi3_device)
{
	struct ptl_mr *ptl_mr;
	ptl_mr = kzalloc(sizeof(*ptl_mr), GFP_KERNEL);
	if (!ptl_mr)
		return ERR_PTR(-ENOMEM);
	ptl_mr->object_type = PTL_MR;
	ptl_mr->max_num_sg = max_num_sg;
	ptl_mr->max_num_meta_sg = max_num_meta_sg;
	ptl_mr->type = type;
	ptl_mr->bxi3_device = NULL;
	ptl_mr->bxi3_device = bxi3_device;
	ptl_mr->fake_mr.rkey = PTL_MAGIC_FAKE_MR_KEY;
	ptl_mr->fake_mr.device = &ptl_mr->bxi3_device->fake_ib_dev;
	return ptl_mr;
}

void ptl_mr_destroy(struct ptl_mr *ptl_mr)
{
	if (!ptl_mr)
		return;
	kfree(ptl_mr);
}



struct ib_mr *ib_portals_mr_pool_get(struct ib_qp *qp, struct list_head *mr_list)
{

	struct ptl_mr *ptl_mr = NULL;
	unsigned long flags;
	struct ptl_qp *ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);

	/* Lock the MR pool */
	spin_lock_irqsave(&ptl_qp->ptl_mr_list_lock, flags);
	if (!list_empty(&ptl_qp->ptl_mr_list)) {
		/* Get the first ptl_mr that contains the 'entry' field at the head of the list */
		ptl_mr = list_first_entry(&ptl_qp->ptl_mr_list, struct ptl_mr, mr_entry);
		/* Remove it from the list so no one else takes it */
		list_del(&ptl_mr->mr_entry);
	}

	spin_unlock_irqrestore(&ptl_qp->ptl_mr_list_lock, flags);

	return ptl_mr ? &ptl_mr->fake_mr : NULL;
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_get);

void ib_portals_mr_pool_put(struct ib_qp *qp, struct list_head *list,
                            struct ib_mr *mr)
{
	struct ptl_mr *ptl_mr;
	struct ptl_qp *ptl_qp;
	unsigned long flags;

	if (!qp || !mr) {
		PTL_WARN("Invalid parameters: qp=%p, mr=%p", qp, mr);
		return;
	}

	ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);

	ptl_mr = container_of(mr, struct ptl_mr, fake_mr);
	PTL_CHECK(ptl_mr, PTL_MR);

	/* Lock the MR pool and add the MR back to the list */
	spin_lock_irqsave(&ptl_qp->ptl_mr_list_lock, flags);
	list_add(&ptl_mr->mr_entry, &ptl_qp->ptl_mr_list);
	spin_unlock_irqrestore(&ptl_qp->ptl_mr_list_lock, flags);

	PTL_DEBUG("Returned MR %p to pool", mr);
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_put);


int ib_portals_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
                            enum ib_mr_type type, u32 max_num_sg,
                            u32 max_num_meta_sg)
{
	struct ptl_mr *ptl_mr, *tmp;
	struct ptl_qp *ptl_qp;
	int i;
	int ret = 0;

	/* Sanity checks */
	if (!qp || !list || nr <= 0) {
		PTL_FATAL("Wrong params: qp is NULL? %s list is NULL? %s nr <= 0? %d", qp ? "NO" : "YES", list ? "NO" : "YES", nr);
		return -EINVAL;
	}

	ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);

	for (i = 0; i < nr; i++) {
		ptl_mr = ptl_mr_create(max_num_sg, max_num_meta_sg, type, ptl_qp->ptl_id->bxiv3_dev);
		if (IS_ERR(ptl_mr)) {
			ret = PTR_ERR(ptl_mr);
			goto rollback;
		}
		list_add(&ptl_mr->mr_entry, &ptl_qp->ptl_mr_list);
	}
	PTL_DEBUG("OK create the MR list for QP");
	return 0;

rollback:
	PTL_WARN("Rolling back");
	/* Clean up all successfully allocated entries */
	list_for_each_entry_safe(ptl_mr, tmp, &ptl_qp->ptl_mr_list, mr_entry) {
		list_del(&ptl_mr->mr_entry);
		ptl_mr_destroy(ptl_mr);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_init);

void ib_portals_mr_pool_destroy(struct ib_qp *qp, struct list_head *list)
{
	struct ptl_mr *ptl_mr, *tmp;
	struct ptl_qp *ptl_qp;
	unsigned long flags;

	/* Sanity checks */
	if (!qp || !list) {
		PTL_WARN("Invalid parameters: qp=%p, list=%p", qp, list);
		return;
	}

	ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);

	/* Lock the MR pool */
	spin_lock_irqsave(&ptl_qp->ptl_mr_list_lock, flags);

	/* Clean up all entries in the list */
	list_for_each_entry_safe(ptl_mr, tmp, &ptl_qp->ptl_mr_list, mr_entry) {
		list_del(&ptl_mr->mr_entry);
		spin_unlock_irqrestore(&ptl_qp->ptl_mr_list_lock, flags);
		ptl_mr_destroy(ptl_mr);
		spin_lock_irqsave(&ptl_qp->ptl_mr_list_lock, flags);
	}

	spin_unlock_irqrestore(&ptl_qp->ptl_mr_list_lock, flags);

	PTL_DEBUG("Destroyed MR pool for QP %p", qp);
}

EXPORT_SYMBOL_GPL(ib_portals_mr_pool_destroy);

