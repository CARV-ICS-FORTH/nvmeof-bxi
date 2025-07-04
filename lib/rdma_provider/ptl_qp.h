#ifndef PTL_QP_H
#define PTL_QP_H
#include "ptl_connection.h"
#include "ptl_object_types.h"
#include <infiniband/verbs.h>
#include <stdbool.h>
#include <stdint.h>
struct ptl_qp {
	ptl_obj_type_e object_type;
	struct ptl_cm_id *ptl_cm_id;
	struct ptl_pd *ptl_pd;
	struct ptl_cq *send_cq;
	struct ptl_cq *recv_cq;
	struct ibv_qp fake_qp;
	size_t remote_page_size;
	size_t remote_alignment_size;
	int remote_nid; /*node id for Portals*/
	int remote_pid; /* pid for Portals*/
	int remote_pt_index; /*Portals index for destination*/
};
struct ptl_qp *ptl_qp_create(struct ptl_pd *ptl_pd, struct ptl_cq *send_queue,
			     struct ptl_cq *receive_queue, struct ptl_conn_comm_pair_info *info);

static inline struct ibv_qp *ptl_qp_get_ibv_qp(struct ptl_qp *ptl_qp)
{
	return &ptl_qp->fake_qp;
}


struct ptl_qp *ptl_qp_get_from_ibv_qp(struct ibv_qp * ibv_qp);

static inline struct ptl_pd *ptl_qp_get_pd(struct ptl_qp * ptl_qp)
{
	return ptl_qp->ptl_pd;
}
#endif
