#include "ptl_qp.h"
#include "asm-generic/errno-base.h"
#include "linux/err.h"
#include "linux/spinlock.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cm_id.h"
#include "ptl_cq.h"
#include "ptl_object_types.h"
#include <linux/container_of.h>
#include <linux/kernel.h>  /* pr_err, pr_info, etc. */
#include <linux/module.h>  /* MODULE_* macros if this is a module */
#include <linux/slab.h>    /* kzalloc, kfree */
#include <rdma/ib_verbs.h> /* struct ib_device, ib_alloc_pd, CQ, QP, etc. */
#include <rdma/rdma_cm.h>  /* struct rdma_cm_id, rdma_create_qp, events */

static atomic_t ptl_id_counter = ATOMIC_INIT(3);

struct ptl_qp *ptl_qp_create(struct ptl_cm_id *ptl_id, struct ptl_pd *ptl_pd,
                             struct ib_qp_init_attr *attr)
{
	struct ptl_qp *ptl_qp;
	struct ptl_bxiv3_qp_map_entry *qp_map_entry;

	if (!ptl_id) {
		PTL_FATAL("ptl_qp_create: invalid ptl_id or cm_id");
		return ERR_PTR(-EINVAL);
	}

	/* Ensure the cm_id is bound to a device */
	if (!ptl_id->bxiv3_dev) {
		PTL_FATAL("ptl_qp_create: cm_id not bound to device");
		return ERR_PTR(-EINVAL);
	}

	ptl_qp = kzalloc(sizeof(*ptl_qp), GFP_KERNEL);
	if (!ptl_qp) {
		PTL_FATAL("ptl_qp_create: failed to allocate qp struct");
		return ERR_PTR(-ENOMEM);
	}
	ptl_qp->object_type = PTL_QP;

	if (attr->send_cq) {
		ptl_qp->send_cq = container_of(attr->send_cq, struct ptl_cq, fake_cq);
		PTL_CHECK(ptl_qp->send_cq, PTL_CQ);
	}

	if (attr->recv_cq) {
		ptl_qp->recv_cq = container_of(attr->recv_cq, struct ptl_cq, fake_cq);
		PTL_CHECK(ptl_qp->recv_cq, PTL_CQ);
	}

	ptl_qp->ptl_id = ptl_id;
	ptl_qp->qpn = atomic_inc_return(&ptl_id_counter);
	spin_lock_init(&ptl_qp->ptl_mr_list_lock);
	/*appropriate wiring needed*/
	INIT_LIST_HEAD(&ptl_qp->fake_qp.rdma_mrs);
	INIT_LIST_HEAD(&ptl_qp->fake_qp.sig_mrs);
	INIT_LIST_HEAD(&ptl_qp->ptl_mr_list);

	ptl_qp->fake_qp.send_cq = &ptl_qp->send_cq->fake_cq;
	ptl_qp->fake_qp.recv_cq = &ptl_qp->recv_cq->fake_cq;
	ptl_qp->fake_qp.qp_context = attr->qp_context;
	ptl_qp->fake_qp.qp_num = ptl_qp->qpn;
	/*Add queue pair in the queue pair map of the device*/
	qp_map_entry = kzalloc(sizeof(*qp_map_entry), GFP_KERNEL);
	if (!qp_map_entry) {
		return ERR_PTR(-ENOMEM);
	}

	qp_map_entry->key = ptl_qp->qpn;
	qp_map_entry->ptl_qp  = ptl_qp;
	spin_lock(&ptl_qp->ptl_id->bxiv3_dev->qp_map_lock);
	hash_add(ptl_qp->ptl_id->bxiv3_dev->qp_map, &qp_map_entry->node, ptl_qp->qpn);
	spin_unlock(&ptl_id->bxiv3_dev->qp_map_lock);

	PTL_DEBUG("Added Queue Pair Number: %d in the Queue pair map successfully", ptl_qp->qpn);
	return ptl_qp;

error:
	kfree(ptl_qp);
	return ERR_PTR(-EINVAL);
}
