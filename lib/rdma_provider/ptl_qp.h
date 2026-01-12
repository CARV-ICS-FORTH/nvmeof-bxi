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
	/**
	* Portals table entry for sending nvme-cmd (initiator) nvme-cpl target
	* */
	int remote_msg_pte;
	/**
	 * Portals table entry for performing rma operations (target->initiator).
	 * In the initiator to target case it will be -1 no rma allowed
	 * */
	int remote_rma_pte;
#if PTL_USE_MATCHING
	/**
	 * In the case of matching we also specify which match bits are valid.
	 * All proper values are set during the connection process.
	 */
	/*Where the remote peer has MEs for recv*/
	uint64_t recv_match_bits;
	/*Where the remote peer has MEs for RMA operations*/
	uint64_t rma_match_bits;
#endif
	int local_msg_pte;
	int local_rma_pte;
};
struct ptl_qp *ptl_qp_create(struct ptl_pd *ptl_pd, struct ptl_cq *send_queue,
			     struct ptl_cq *receive_queue, int nid, int pid, int local_msg_pte, int local_rma_pte);

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
