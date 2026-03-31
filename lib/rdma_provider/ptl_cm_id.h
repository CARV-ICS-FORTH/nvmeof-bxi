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
	/*<gesalous>, FIFO ordering feat*/
	/*Encoded the initiator qp num and target*/
	uint16_t initiator_qp_num;
	uint16_t target_qp_num;
	/*In case of no matching this field is ignored*/
	uint64_t recv_match_bits;
	/**
	* Where the remote peer has MEs for RMA operations.
	* In case of no matching this field is ignored
	* */
	uint64_t rma_match_bits;
	/**
	* CQ id of the remote peer where it has subscribed for events.
	* Its purpose is to encoded in the match bits in the PtlPut operations
	*/
	int remote_cq_id;
#if PTL_USE_MATCHING
	uint64_t my_match_bits;
#endif
	ptl_cm_id_e cm_id_state;
	int ptl_qp_num;
	struct ptl_cq *cq;
	bool is_listen_id;
	struct rdma_conn_param conn_param;
	int remote_nid; /*node id for Portals*/
	int remote_pid; /* pid for Portals*/
	/**
	* Portals table entry for sending nvme-cmd (initiator) nvme-cpl (target)
	* */
	int remote_msg_pte;
	/**
	 * Portals table entry for performing rma operations (target->initiator).
	 * In the target case it will be -1 (no rma allowed).
	 * */
	int remote_rma_pte;
	int local_rma_pte;
	/*extensions for nvme_cpl*/
	uint64_t remote_nvme_cpl_start_addr;
	size_t nvme_cpl_queue_size;
	bool remote_is_a_kernel_initiator;
	//needed for connection setup
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
}
#endif
