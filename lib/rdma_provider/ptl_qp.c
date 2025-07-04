#include "ptl_qp.h"
#include "ptl_config.h"
#include "ptl_connection.h"
#include "ptl_cm_id.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_pd.h"
#include "spdk/util.h"
#include <stdlib.h>

struct ptl_qp *ptl_qp_create(struct ptl_pd *ptl_pd, struct ptl_cq *send_queue,
			     struct ptl_cq *receive_queue, struct ptl_conn_comm_pair_info * comm_pair_info)
{
	assert(ptl_pd);
	assert(send_queue);
	assert(receive_queue);

	struct ptl_qp *ptl_qp = calloc(1UL, sizeof(*ptl_qp));
	if (NULL == ptl_qp) {
		SPDK_PTL_FATAL("Failed to allocate memory for portals queue pair");
	}
	ptl_qp->object_type = PTL_QP;
	/*pd related staff*/
	ptl_qp->ptl_pd = ptl_pd;
	ptl_qp->fake_qp.pd = ptl_pd_get_ibv_pd(ptl_pd);
	/*cq related staff*/
	ptl_qp->send_cq = send_queue;
	ptl_qp->fake_qp.send_cq = ptl_cq_get_ibv_cq(send_queue);
	ptl_qp->recv_cq = receive_queue;
	ptl_qp->fake_qp.recv_cq = ptl_cq_get_ibv_cq(receive_queue);

	ptl_qp->remote_nid = comm_pair_info->dst_nid;
	ptl_qp->remote_pid = comm_pair_info->dst_pid;
	ptl_qp->remote_pt_index = comm_pair_info->dst_pte;
	return ptl_qp;
}

struct ptl_qp *ptl_qp_get_from_ibv_qp(struct ibv_qp * ibv_qp)
{
	struct ptl_qp *ptl_qp = SPDK_CONTAINEROF(ibv_qp, struct ptl_qp, fake_qp);
	if (ptl_qp->object_type != PTL_QP) {
		SPDK_PTL_FATAL("FATAL Corrupted");
	}
	return ptl_qp;
}

