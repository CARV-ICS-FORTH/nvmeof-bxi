#ifndef PTL_CQ_H
#define PTL_CQ_H
#include "ptl_object_types.h"
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <portals4.h>
#include <portals4_bxiext.h>
#include <rdma/ib_verbs.h>
struct ptl_bxiv3_device;
struct ptl_cq {
	ptl_obj_type_e object_type;
	/*Backpointer of where this ptl_cq belongs to */
	struct ptl_cq_pool *cq_pool;
	struct ptl_bxiv3_device *bxiv3_dev;
	/* We keep a one-on-one mapping: Each CQ is bound to a single PTE. */
	ptl_pt_index_t pte;
	/*Warning in the kernel cq_id = pte*/
	int ptl_cq_id;
	/* How many completion queue entries does this cq has */
	int nr_cqes;
	ptl_handle_eq_t eq;
	struct list_head head;
	struct ib_cq fake_cq;
	enum ib_poll_context poll_ctx;
	/* The NIC interrupt is shared and coalesces, so a lone admin completion
	 * (keep-alive, reconnect Connect capsule) can sit undrained at idle.
	 * poll_work drains this EQ on a timer as a fallback; drain_lock
	 * serializes the interrupt path against the poller. */
	struct delayed_work poll_work;
	spinlock_t drain_lock;
};

/**
 * ptl_cq_create - Create a new Portals completion queue object
 * @cq_pool: Pointer to the completion queue pool that will manage this CQ
 * @nr_cqes: Number of completion queue entries to allocate for this CQ
 * @pte: The portals table entry which this ptl_cq will be associated with
 *
 * Creates and initializes a new ptl_cq (Portals Completion Queue) object.
 * This function only creates the ptl_cq object and initializes its internal
 * state. The created CQ is NOT automatically added to the pool - the pool
 * management layer is responsible for adding the CQ to the pool after creation.
 * The CQ maintains a backpointer to its parent cq_pool. Each CQ has a
 * one-to-one mapping with a Portal Table Entry (PTE).
 *
 * Return: Pointer to the newly created ptl_cq object, or NULL on failure.
 */
struct ptl_cq *ptl_cq_create(struct ptl_cq_pool *cq_pool,
                             struct ptl_bxiv3_device *bxiv3_dev, int nr_cqes,
                             ptl_pt_index_t pte, enum ib_poll_context poll_ctx);

/**
 * ptl_cq_destroy - Destroy a Portals completion queue object
 * @ptl_cq: Pointer to the completion queue object to destroy
 *
 * Destroys a ptl_cq object and releases its resources. Returns the Portal
 * Table Entry index that was associated with the destroyed CQ, allowing the
 * caller to perform any necessary cleanup or reassignment of the PTE.
 * The caller is responsible for removing the CQ from the pool before calling
 * this function (if applicable).
 *
 * Return: The Portal Table Entry index (ptl_pt_index_t) that was associated
 *         with the destroyed CQ.
 */
ptl_pt_index_t ptl_cq_destroy(struct ptl_cq *ptl_cq);
#endif
