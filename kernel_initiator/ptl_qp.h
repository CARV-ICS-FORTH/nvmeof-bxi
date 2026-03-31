#ifndef PTL_QP_H
#define PTL_QP_H
#include "ptl_object_types.h"
#include "rdma/ib_verbs.h"
#include <portals4.h>
struct ptl_cm_id;
struct ptl_qp {
	ptl_obj_type_e object_type;
	struct ptl_cm_id *ptl_id;
	struct ptl_pd *ptl_pd;
	struct ptl_cq *send_cq;
	struct ptl_cq *recv_cq;
	struct list_head ptl_mr_list;
	spinlock_t ptl_mr_list_lock;
	/*<gesalous> non-matching feat*/
	/*The one and only list entry for all data plus nvme completions*/
	ptl_le_t rma_le;
	ptl_handle_le_t rma_leh;
	/*For each nvme_cpl we keep metadata*/
	struct ptl_recv_op *recv_op_meta;
	size_t recv_op_meta_size;

	struct ib_qp fake_qp;
	int qpn;
};

struct ptl_qp *ptl_qp_create(struct ptl_cm_id *ptl_id, struct ptl_pd *ptl_pd,
                             struct ib_qp_init_attr *attr);
#endif
