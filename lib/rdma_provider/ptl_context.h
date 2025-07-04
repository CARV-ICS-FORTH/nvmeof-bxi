#ifndef PTL_CONTEXT_H
#define PTL_CONTEXT_H
#include "../../include/spdk/nvme_spec.h"
#include "portals4.h"
#include "ptl_object_types.h"
#include <infiniband/verbs.h>
#include <stdbool.h>
#define PTL_CONTEXT_SERVER_PID 0
#define PTL_IOVEC_SIZE 2
struct ptl_context;
struct ibv_context;
struct ibv_pd;
struct ptl_context_recv_op {
	ptl_obj_type_e obj_type;
	uint64_t wr_id;
	uint64_t bytes_received;
	int initiator_qp_num;
	int target_qp_num;
	/*In which cqid I wait for the receive event*/
	int cq_id;
	ptl_iovec_t io_vector[PTL_IOVEC_SIZE];
};

struct ptl_context_send_op {
	ptl_obj_type_e obj_type;
	uint64_t wr_id;
	int cq_id;
	int qp_num;
};



struct ptl_context {
	ptl_obj_type_e object_type;
	ptl_handle_ni_t ni_handle;
	ptl_pt_index_t portals_idx_send_recv;
	ptl_pt_index_t portals_idx_rma;
	/*gesalous, portals staff*/
	struct ptl_pd *ptl_pd;
	struct ibv_context fake_ibv_cnxt;
	struct ibv_cq fake_cq;
	struct spdk_rdma_provider_srq *srq;
	int pid;
	int nid;
	bool initialized;
};

struct ptl_context *ptl_cnxt_get(void);
struct ibv_context *ptl_cnxt_get_ibv_context(struct ptl_context *cnxt);
struct ptl_context *ptl_cnxt_get_from_ibcnxt(struct ibv_context *ib_cnxt);
struct ptl_context *ptl_cnxt_get_from_ibvpd(struct ibv_pd *ib_pd);


ptl_pt_index_t ptl_cnxt_get_portal_index(struct ptl_context *cnxt);
ptl_handle_ni_t ptl_cnxt_get_ni_handle(struct ptl_context *cnxt);

static inline int ptl_cnxt_get_nid(struct ptl_context *cnxt)
{
	return cnxt->nid;
}

static inline int ptl_cnxt_get_pid(struct ptl_context *cnxt)
{
	return cnxt->pid;
}




#endif

