#ifndef PTL_CM_ID_H
#define PTL_CM_ID_H
#include "ptl_connection.h"
#include "ptl_context.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include "ptl_qp.h"
#include "spdk/util.h"
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <sys/socket.h>

typedef enum {
	PTL_CM_DISCONNECTING = 0,
	PTL_CM_DISCONNECTED,
	PTL_CM_CONNECTING,
	PTL_CM_CONNECTED,
	PTL_CM_UNCONNECTED,
	PTL_CM_GUARD
} ptl_cm_id_e;

struct ptl_cm_id {
	ptl_obj_type_e object_type;
	struct ptl_conn_msg conn_msg;
	struct rdma_cm_ptl_event_channel *ptl_channel;
	//This info is already kept in fake_cm_id set up during rdma_resolve().
	// struct sockaddr src_addr;
	// struct sockaddr dest_addr;
	struct rdma_cm_id fake_cm_id;
	/*As in verbs, each ptl_cm_id associates with a single ptl_pd*/
	struct ptl_pd *ptl_pd;
	struct ptl_qp *ptl_qp;
	struct ptl_context *ptl_context;
	uint64_t uuid;
	/*Where the remote peer has MEs for recv*/
	uint64_t recv_match_bits;
	/*Where the remote peer has MEs for RMA operations*/
	uint64_t rma_match_bits;
	/**
	* CQ id of the remote peer where it has subscribed for events.
	* Its purpose is to encoded in the match bits in the PtlPut operations
	*/
	int remote_cq_id;
	/*Which are MY match bits for recv operations*/
	uint64_t my_match_bits;
	ptl_cm_id_e cm_id_state;
	int ptl_qp_num;
	struct ptl_cq *cq;
	bool is_listen_id;
	struct rdma_conn_param conn_param;
	//needed for connection setup and shit
	const void *fake_data;
};

void ptl_cm_id_set_recv_cq(struct ptl_cm_id *ptl_id, struct ptl_cq *recv_cq);

void ptl_cm_id_set_send_cq(struct ptl_cm_id *ptl_id, struct ptl_cq *send_cq);

struct ptl_cm_id *ptl_cm_id_create(struct rdma_cm_ptl_event_channel * event_channel, void *context);

static inline struct ptl_cm_id *ptl_cm_id_get(struct rdma_cm_id *id)
{
	struct ptl_cm_id *ptl_id =
		SPDK_CONTAINEROF(id, struct ptl_cm_id, fake_cm_id);
	if (PTL_CM_ID != ptl_id->object_type) {
		SPDK_PTL_FATAL("Corrupted PTL ID");
	}
	return ptl_id;
}
struct rdma_cm_event *ptl_cm_id_create_event(struct ptl_cm_id *ptl_id, struct ptl_cm_id *listen_id,
		enum rdma_cm_event_type event_type);


void ptl_cm_id_add_event(struct ptl_cm_id *ptl_id,
			 struct rdma_cm_event *event);

// static inline struct sockaddr *
// rdma_cm_ptl_id_get_src_addr(struct ptl_cm_id *ptl_id)
// {
// 	return &ptl_id->src_addr;
// }

void ptl_cm_id_set_fake_data(struct ptl_cm_id *ptl_id, const void *fake_data);

static inline void ptl_cm_id_set_ptl_qp(struct ptl_cm_id *ptl_id, struct ptl_qp *ptl_qp)
{
	if (ptl_id->ptl_qp) {
		SPDK_PTL_FATAL("PTL QP already set");
	}
	ptl_id->ptl_qp = ptl_qp;
	ptl_id->fake_cm_id.qp = ptl_qp_get_ibv_qp(ptl_qp);
}


static inline void ptl_cm_id_set_ptl_pd(struct ptl_cm_id *ptl_id, struct ptl_pd *ptl_pd)
{
	if (ptl_id->ptl_pd) {
		SPDK_PTL_FATAL("PTL PD already set");
	}
	ptl_id->ptl_pd = ptl_pd;
	ptl_id->fake_cm_id.pd = ptl_pd_get_ibv_pd(ptl_pd);
	/*set also context as in the verbs case*/
	ptl_id->ptl_context = ptl_pd_get_cnxt(ptl_pd);
	ptl_id->fake_cm_id.context = ptl_cnxt_get_ibv_context(ptl_pd_get_cnxt(ptl_pd));
}
#endif
