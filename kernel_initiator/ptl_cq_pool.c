#include "ptl_cq_pool.h"
#include "asm-generic/errno-base.h"
#include "asm-generic/errno.h"
#include "linux/err.h"
#include "portals4.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cq.h"
#include "ptl_object_types.h"
#include <linux/slab.h>

struct ptl_cq_pool *ptl_cq_pool_create(struct ptl_bxiv3_device *bxiv3_dev)
{
	struct ptl_cq_pool *pool;

	/* Sanity checks */
	if (!bxiv3_dev) {
		PTL_WARN("NULL device pointer");
		return ERR_PTR(-EINVAL);
	}

	/* Allocate pool */
	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		PTL_FATAL("Out of memory: failed to allocate pool");
		return ERR_PTR(-ENOMEM);
	}
	pool->bxiv3_device = bxiv3_dev;
	INIT_LIST_HEAD(&pool->ptl_cq_list_free);
	INIT_LIST_HEAD(&pool->ptl_cq_list_reserved);
	spin_lock_init(&pool->cq_list_lock);
	return pool;
}

int ptl_cq_pool_destroy(struct ptl_cq_pool *ptl_cq_pool)
{
	struct ptl_cq *cq, *next_cq;
	int ret;

	/* Sanity checks */
	if (!ptl_cq_pool) {
		PTL_WARN("ptl_cq_pool_destroy: NULL pool pointer");
		return -EINVAL;
	}

	if (IS_ERR(ptl_cq_pool)) {
		PTL_WARN("ptl_cq_pool_destroy: error pointer %ld", PTR_ERR(ptl_cq_pool));
		return -EINVAL;
	}

	/* Iterate through the list and destroy all CQs */
	spin_lock(&ptl_cq_pool->cq_list_lock);
	if (ptl_cq_pool->num_reserved_cqs > 0) {
		PTL_WARN("Sorry ptl_cqs still in use");
		ret = -EBUSY;
		goto exit;
	}
	list_for_each_entry_safe(cq, next_cq, &ptl_cq_pool->ptl_cq_list_free, head) {
		/* Remove from list */
		list_del(&cq->head);
		/* Release lock before destroying CQ to avoid sleeping in atomic context */
		spin_unlock(&ptl_cq_pool->cq_list_lock);
		/* Destroy the CQ (this should also free the associated PTE) */
		ptl_cq_destroy(cq);
		/* Re-acquire lock for next iteration */
		spin_lock(&ptl_cq_pool->cq_list_lock);
	}
	spin_unlock(&ptl_cq_pool->cq_list_lock);

	/* Free the pool itself */
	kfree(ptl_cq_pool);
	PTL_INFO("ptl_cq_pool_destroy: pool destroyed");
	return 0;
exit:
	spin_unlock(&ptl_cq_pool->cq_list_lock);
	return ret;
}

struct ptl_cq *ptl_cq_pool_get(struct ptl_cq_pool *ptl_cq_pool, int nr_cqes,
                               enum ib_poll_context poll_ctx)
{
	struct ptl_cq *tmp_cq, *ptl_cq = NULL;
	ptl_pt_index_t pte;

	if (poll_ctx != IB_POLL_SOFTIRQ) {
		PTL_FATAL("Sorry! Currently BXIv3 NVMe-oF initiator supports only "
		          "IB_POLL_SOFTIRQ mode");
		return ERR_PTR(-EINVAL);
	}

	/* Sanity checks */
	if (!ptl_cq_pool) {
		PTL_FATAL("NULL pool pointer?");
		return ERR_PTR(-EINVAL);
	}

	if (nr_cqes <= 0) {
		PTL_FATAL("invalid nr_cqes %d", nr_cqes);
		return ERR_PTR(-EINVAL);
	}

	if (nr_cqes > PTL_CQ_POOL_MAX_SIZE) {
		PTL_FATAL("nr_cqes: %d larger than maximum: %d", nr_cqes,
		          PTL_CQ_POOL_MAX_SIZE);
		return ERR_PTR(-EINVAL);
	}

	/* Search the free list for a CQ with sufficient entries */
	spin_lock(&ptl_cq_pool->cq_list_lock);
	list_for_each_entry(tmp_cq, &ptl_cq_pool->ptl_cq_list_free, head) {
		if (tmp_cq->nr_cqes < nr_cqes)
			continue;
		ptl_cq = tmp_cq;
		list_del(&ptl_cq->head);
		list_add(&ptl_cq->head, &ptl_cq_pool->ptl_cq_list_reserved);
		ptl_cq_pool->num_free_cqs--;
		ptl_cq_pool->num_reserved_cqs++;
		PTL_DEBUG("Resuing existing CQ with %d number of entries", ptl_cq->nr_cqes);
		spin_unlock(&ptl_cq_pool->cq_list_lock);
		return ptl_cq;
	}
	spin_unlock(&ptl_cq_pool->cq_list_lock);

	pte = ptl_bxiv3_dev_alloc_pte(ptl_cq_pool->bxiv3_device);
	if (-1 == pte) {
		PTL_WARN("No more ptes sorry!");
		return ERR_PTR(-EBUSY);
	}
	ptl_cq = ptl_cq_create(ptl_cq_pool, ptl_cq_pool->bxiv3_device, nr_cqes, pte, poll_ctx);
	if (IS_ERR(ptl_cq)) {
		PTL_FATAL("ptl_cq_pool_get: failed to create new CQ");
		return ptl_cq;
	}

	spin_lock(&ptl_cq_pool->cq_list_lock);
	list_add(&ptl_cq->head, &ptl_cq_pool->ptl_cq_list_reserved);
	ptl_cq_pool->num_reserved_cqs++;
	PTL_DEBUG("Created a new ptl_cq with %d number of cq entries.",
	          ptl_cq->nr_cqes);
	spin_unlock(&ptl_cq_pool->cq_list_lock);
	return ptl_cq;
}

int ptl_cq_pool_put(struct ptl_cq_pool *ptl_cq_pool, struct ptl_cq *ptl_cq)
{
	/* Sanity checks */
	if (!ptl_cq_pool) {
		PTL_WARN("ptl_cq_pool_put: NULL pool pointer");
		return -EINVAL;
	}

	if (!ptl_cq) {
		PTL_WARN("ptl_cq_pool_put: NULL cq pointer");
		return -EINVAL;
	}

	spin_lock(&ptl_cq_pool->cq_list_lock);

	/* Remove from reserved list and add to free list */
	list_del(&ptl_cq->head);
	list_add(&ptl_cq->head, &ptl_cq_pool->ptl_cq_list_free);
	ptl_cq_pool->num_reserved_cqs--;
	ptl_cq_pool->num_free_cqs++;

	spin_unlock(&ptl_cq_pool->cq_list_lock);
	PTL_DEBUG("ptl_cq_pool_put: returned CQ with %d entries to free list",
	          ptl_cq->nr_cqes);
	return 0;
}
