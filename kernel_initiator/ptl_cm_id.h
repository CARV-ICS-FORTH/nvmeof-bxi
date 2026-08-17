
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PTL_CM_ID_H
#define _PTL_CM_ID_H

#include <linux/spinlock.h>
#include <linux/socket.h>      /* struct sockaddr, sockaddr_storage */
#include <linux/types.h>

#include <portals4.h>
#include <portals4_bxiext.h>

#define PTL_INITIATOR_DEPTH 32

enum ptl_cm_event_type {
	PTL_CM_EVENT_ADDR_RESOLVED = 0,
	PTL_CM_EVENT_ROUTE_RESOLVED,
	PTL_CM_EVENT_CONNECT_REQUEST,
	PTL_CM_EVENT_ESTABLISHED,
	PTL_CM_EVENT_DISCONNECTED,
	PTL_CM_EVENT_REJECTED,
	PTL_CM_EVENT_ADDR_ERROR,
	PTL_CM_EVENT_ROUTE_ERROR,
	PTL_CM_EVENT_CONNECT_ERROR,
	PTL_CM_EVENT_DEVICE_REMOVAL,
	PTL_CM_EVENT_TIMEWAIT_EXIT,
	PTL_CM_EVENT_UNREACHABLE,
	PTL_CM_EVENT_ADDR_CHANGE,
};

enum ptl_cm_state {
	PTL_CM_IDLE = 0,
	PTL_CM_ADDR_RESOLVING,
	PTL_CM_ADDR_RESOLVED,
	PTL_CM_ROUTE_RESOLVING,
	PTL_CM_ROUTE_RESOLVED,
	PTL_CM_CONNECTING,
	PTL_CM_ESTABLISHED,
	PTL_CM_DISCONNECTING,
	PTL_CM_DISCONNECTED,
	PTL_CM_ERROR,
	
};


struct ptl_cm_id;

struct ptl_cm_event {
	enum ptl_cm_event_type event;
	int status;
	const void *private_data;
	u8 private_data_len;
};

/*
 * Connection parameters.
 
 * LAYOUT WARNING: the existing wire protocol embeds this struct BY VALUE
 * inside conn_msg.conn_open.To stay
 * byte-compatible with the deployed SPDK target, this struct mirrors
 * struct rdma_conn_param field-for-field, in order. Do not reorder or
 * resize fields. ptl_connection.h's conn_msg definition must have its
 * conn_param member re-typed from rdma_conn_param to this struct, 
 * identical layout => identical bytes on the wire.
 */
struct ptl_cm_conn_param {
	const void *private_data;
	u8  private_data_len;
	u8  responder_resources;
	u8  initiator_depth;
	u8  flow_control;
	u8  retry_count;
	u8  rnr_retry_count;
	u8  srq;
	u32 qp_num;
};

typedef int (*ptl_cm_handler)(struct ptl_cm_id *cm_id,
			      struct ptl_cm_event *event);

struct ptl_cm_id {
	/* user context + callback */
	void           *context;
	ptl_cm_handler  event_handler;

	/* refcounted at create (get_net) / destroy (put_net) */
	struct net     *net;

	/* device — bound in ptl_resolve_addr(); kref_get taken there.
	 * NULL until then; ptl_connect_locked() requires it. */
	struct ptl_bxiv3_device *bxiv3_dev;

	/* local identity, cached from bxiv3_dev->proc_id at bind time */
	int nid;
	int pid;

	/* peer identity (nid = last IP byte << 7, pid = port) */
	ptl_process_t   peer;

	/* source address copied into conn_open.src_addr at connect */
	struct sockaddr_storage src_addr;

	/* connection identity on the wire protocol */
	u16 initiator_qp_num;
	u16 target_qp_num;

	/* target-side resources from the open-connection reply */
	int remote_msg_pte;
	int remote_rma_pte;
	int remote_cq_id;

	/* NVMe completion queue bookkeeping — PTL_CHECK_NVME_CID reads
	 * ptl_qp->ptl_id->nvme_cpl_start, so these live here. Filled by
	 * ptl_connect_locked_with_ptl_params(). */
	u64    nvme_cpl_start;
	u64    remote_nvme_cpl_start;
	size_t nvme_completion_queue_size;

	/* owned copy of the connect params 
	 *freed in ptl_destroy_id) */
	struct ptl_cm_conn_param param;

	/* associated QP */
	struct ptl_qp  *qp;

	/* implementation private */
	void           *provider_data;

	/* state machine */
	enum ptl_cm_state state;
	spinlock_t         lock;
};

/* Lifecycle */
struct ptl_cm_id *ptl_create_id(struct net *net, ptl_cm_handler handler,
				void *context);
void ptl_destroy_id(struct ptl_cm_id *id);

/* State transitions are validated; returns -EINVAL on illegal moves.
 * Called by ptl_cq.c's connection-reply dispatch as well as internally. */
int ptl_cm_set_state(struct ptl_cm_id *id, enum ptl_cm_state new_state);

#endif
