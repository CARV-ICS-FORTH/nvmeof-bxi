#include "ptl_cm_id.h"
#include "deque.h"
#include "ptl_context.h"
#include "ptl_log.h"
#include "ptl_macros.h"
#include "ptl_object_types.h"
#include "ptl_uuid.h"
#include "rdma_cm_ptl_event_channel.h"
#include <assert.h>
#include <rdma/rdma_cma.h>
#include <stdlib.h>
static int ptl_local_qp_num = 1;

struct ptl_cm_id *ptl_cm_id_create(struct rdma_cm_ptl_event_channel *ptl_channel, void *context)
{
	struct ptl_context * ptl_context;
	struct ptl_cm_id * ptl_id = calloc(1UL, sizeof(*ptl_id));
	if (NULL == ptl_id) {
		SPDK_PTL_FATAL("Failed to allocate memory");
	}
	ptl_id->object_type = PTL_CM_ID;
	/*o mpampas sas*/
	ptl_id->ptl_channel = ptl_channel;
	ptl_id->fake_cm_id.context = context;
	ptl_context = ptl_cnxt_get();
	ptl_id->fake_cm_id.verbs = ptl_cnxt_get_ibv_context(ptl_context);
	ptl_id->fake_cm_id.ps = RDMA_PS_TCP;
	ptl_id->ptl_qp_num = ptl_local_qp_num++;
	ptl_id->cm_id_state = PTL_CM_UNCONNECTED;
	ptl_id->my_match_bits = ptl_uuid_get_next_match_bit();
	SPDK_PTL_DEBUG("MATCH_BITS: SUCCESSFULLY created PTL_ID: %p MY match bits are %lu", ptl_id,
		       ptl_id->my_match_bits);
	return ptl_id;
}

struct rdma_cm_event *ptl_cm_id_create_event(struct ptl_cm_id *ptl_id, struct ptl_cm_id *listen_id,
		enum rdma_cm_event_type event_type)
{

	struct rdma_cm_event *fake_event;
	/*Create a fake event*/
	fake_event = calloc(1UL, sizeof(struct rdma_cm_event));
	if (!fake_event) {
		SPDK_PTL_FATAL("No memory!");
	}
	fake_event->id = &ptl_id->fake_cm_id;
	SPDK_PTL_DEBUG("CP server: creating event %d for qp num: %d", event_type,
		       fake_event->id->qp ? fake_event->id->qp->qp_num : -128);
	fake_event->listen_id = (void*)0xFFFFFFFFFFFFFFFF;
	if (listen_id) {
		fake_event->listen_id = &listen_id->fake_cm_id;
	}
	fake_event->status = 0;
	fake_event->event = event_type;
	//original
	// fake_event->param.conn.private_data = ptl_id->fake_data;
	/*rdma_cm library uses the private_data field to negotiate a new connection*/
	fake_event->param.conn = ptl_id->conn_param;
	if (ptl_id->conn_param.private_data) {
		SPDK_PTL_DEBUG("CONN_PARAM: setting connection params for this event");
		fake_event->param.conn.private_data = calloc(1UL, fake_event->param.conn.private_data_len);
		memcpy((void *)fake_event->param.conn.private_data, ptl_id->conn_param.private_data,
		       fake_event->param.conn.private_data_len);
	}
	// fake_event->param.conn.private_data = private_data;
	// fake_event->param.conn.private_data_len = private_data_len;
	// fake_event->param.conn.initiator_depth = 32;
	// fake_event->param.conn.responder_resources = 0;
	// fake_event->param.conn.retry_count = 7;
	// fake_event->param.conn.rnr_retry_count = 7;
	return fake_event;
}

void ptl_cm_id_add_event(struct ptl_cm_id *ptl_id,
			 struct rdma_cm_event *event)
{
	rdma_cm_ptl_event_channel_lock_event_deque(ptl_id->ptl_channel);
	if (false ==
	    deque_push_front(ptl_id->ptl_channel->events_deque, event)) {
		SPDK_PTL_FATAL("Failed to queue fake event");
	}
	// SPDK_PTL_DEBUG(" (nikos) ********* Added event of type: %d for qp_num: %d and channel %p", event->event, ptl_id->ptl_qp_num, ptl_id->ptl_channel);
	uint64_t result;
	if (write(ptl_id->ptl_channel->fake_channel.fd, &result, sizeof(result)) != sizeof(result)) {
		perror("read");
		SPDK_PTL_WARN("Failed to write event");
	}
	rdma_cm_ptl_event_channel_unlock_event_deque(ptl_id->ptl_channel);
}

void ptl_cm_id_set_fake_data(struct ptl_cm_id *ptl_id,
			     const void *fake_data)
{
	ptl_id->fake_data = fake_data;
}



