
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PTL_CM_ID_H
#define PTL_CM_ID_H

#include <linux/spinlock.h>
#include <linux/socket.h>      /* struct sockaddr, sockaddr_storage */
#include <linux/types.h>

#include <portals4.h>
#include <portals4_bxiext.h>
#include "ptl_object_types.h"
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

typedef  enum  {
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

}ptl_cm_id_e;


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
	ptl_obj_type_e object_type;

	/* fake_cm_id removed: the rdma_cm dependency this migration drops */

	spinlock_t state_lock;
	struct net *net;
	u16 initiator_qp_num;
	u16 target_qp_num;

	/* retyped: rdma_cm_event_handler took rdma_cm_id,rdma_cm_event*,
	 * which would keep the <rdma/rdma_cm.h> dependency */
	ptl_cm_handler event_handler;
	void *event_handler_context;

	/* Portals-native identities: stored as ptl_process_t so PtlPut paths
	* assign msg.target_id directly instead of rebuilding it field by field */
	ptl_process_t self_peer;
	ptl_process_t remote_peer;

	int remote_msg_pte;
	int remote_rma_pte;
	int remote_cq_id;
	u64 nvme_cpl_start;
	u64 remote_nvme_cpl_start;
	size_t nvme_completion_queue_size;
	struct ptl_bxiv3_device *bxiv3_dev;
	struct ptl_qp *ptl_qp;

	ptl_cm_id_e cm_id_state;

	/* retyped: rdma_conn_param -> ptl_cm_conn_param (byte-identical,
	 * 24 bytes, static_assert'd — the packed conn_open must not change) */
	struct ptl_cm_conn_param param;

	/* new: was fake_cm_id->route.addr.src_addr, needed by
	 * ptl_connect_locked() to fill conn_open.src_addr */
	struct sockaddr_storage src_addr;
};

/* Lifecycle */
struct ptl_cm_id *ptl_cm_id_create(struct net *net, ptl_cm_handler handler,
				void *context);
void ptl_cm_id_destroy(struct ptl_cm_id *id);

/* State transitions are validated; returns -EINVAL on illegal moves.
 * Called by ptl_cq.c's connection-reply dispatch as well as internally. */
int ptl_cm_id_set_state(struct ptl_cm_id *id, ptl_cm_id_e new_state);

int ptl_cm_id_resolve_addr(struct ptl_cm_id *id,const struct sockaddr *src_addr,const struct sockaddr *dst_addr,unsigned long timeout_ms);

int ptl_cm_send_close_reply(struct ptl_cm_id *id);
#endif
