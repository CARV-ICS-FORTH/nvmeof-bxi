/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PTL_CM_H
#define _PTL_CM_H

#include <rdma/ib_verbs.h> /* uses struct ib_qp_init_attr for now */
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/socket.h>      /* struct sockaddr, sockaddr_storage */

#include <portals4.h>
#include <portals4_bxiext.h>
#include"ptl_cm_id.h"
/*
 * Speaks the EXISTING conn_msg wire protocol (PTL_SPDK_PROTOCOL_VERSION,
 * PTL_OPEN_CONNECTION / PTL_CLOSE_CONNECTION over PTL_CP_SERVER_PTE), so
 * the deployed SPDK target keeps understanding. Connection replies
 * arrive on the device's conn_mgmt EQ and are dispatched by ptl_cq.c
 */

/* Forward declarations */
struct ptl_qp;
struct ptl_pd;
struct ptl_qp_init_attr;
struct ptl_bxiv3_device;
struct ptl_obj_conn_params;    
struct net;

/* Sentinels — not in the Portals 4.0 spec; guard in case BXI headers
 * provide them. */
#ifndef PTL_INVALID_PT_INDEX
#define PTL_INVALID_PT_INDEX  ((ptl_pt_index_t)~0u)
#endif

/* Address / route (route resolution is a no-op state walk in Portals).
 * Both fire their event synchronously before returning*/
int ptl_resolve_addr(struct ptl_cm_id *id,
		     const struct sockaddr *src_addr,
		     const struct sockaddr *dst_addr,
		     unsigned long timeout_ms);
int ptl_resolve_route(struct ptl_cm_id *id, unsigned long timeout_ms);

/* Connection — old conn_msg wire format, fire-and-forget send on the
 * device's conn_mgmt EQ. The ESTABLISHED transition is driven later by
 * ptl_cq.c when the open-connection reply arrives. */
int ptl_connect_locked(struct ptl_cm_id *id, struct ptl_cm_conn_param *param);
int ptl_connect_locked_with_ptl_params(struct ptl_cm_id *id,
				       struct ptl_cm_conn_param *param,
				       struct ptl_obj_conn_params *ptl_params);
int ptl_disconnect(struct ptl_cm_id *id);

/* QP */
int ptl_create_qp(struct ptl_cm_id *id, struct ib_pd *pd,struct ib_qp_init_attr *attr);

int ptl_destroy_qp(struct ptl_cm_id *id);


/* Helpers */
const char *ptl_cm_event_msg(int ev);
const void *ptl_cm_reject_msg(struct ptl_cm_id *id, int status);
const void *ptl_cm_consumer_reject_data(struct ptl_cm_id *id,
					struct ptl_cm_event *ev, u8 *len);

static inline int ptl_cm_set_service_type(struct ptl_cm_id *id, u8 tos)
{
	(void)id;
	(void)tos;
	return 0;
}

#endif 
