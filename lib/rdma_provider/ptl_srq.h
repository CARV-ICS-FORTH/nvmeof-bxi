#ifndef PTL_SRQ_H
#define PTL_SRQ_H
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include <infiniband/verbs.h>
struct ptl_srq {
	enum ptl_obj_type obj_type;
	struct ibv_srq fake_srq;
	/*
	 * In IB/verbs each queue pair has a pointer to:
	 *   a) a completion queue where it will report its receive and send
	 * events b) a receive queue from where it will consume buffers for
	 * receive. The whole model is centered around the queue pair.
	 *
	 * In Portals there is a fundamental difference. Everything is centered
	 * around the memory. Each MD (send buffers) is associated with a send
	 * queue in which it reports its events and each ME or LE is associated
	 * with a PTE which in turn is associated with an EventQueue.
	 *
	 * To emulate this behavior we basically associate a ptl_srq with a
	 * ptl_cq from where we consume buffers. The setup of the ptl_cq proper
	 * pointer takes place in rdma_create_qp.
	 */
	struct ptl_cq *ptl_cq;
};


struct ptl_srq *ptl_srq_create(struct ptl_pd *pd,
			       struct ibv_srq_init_attr *srq_init_attr);
struct ptl_srq *ptl_srq_get_from_ibv_srq(struct ibv_srq *ibv_srq);

#endif
