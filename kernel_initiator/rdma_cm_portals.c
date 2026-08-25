// SPDX-License-Identifier: GPL-2.0
#include "rdma_cm_portals.h"

#include<linux/container_of.h>
#include <linux/err.h>         /* ERR_PTR / IS_ERR */
#include <linux/errno.h>
#include <linux/in.h>          /* struct sockaddr_in, ntohs */
#include <linux/in6.h>         /* struct sockaddr_in6 */
#include <linux/kref.h>
#include <linux/net.h>         /* get_net / put_net */
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/build_bug.h>
#include "ib_portals.h"        /* ib_portals_dma_* wrappers (virt-DMA corner case) */
#include "ptl_bxiv3_dev_map.h"
#include "ptl_bxiv3_device.h"
/* ptl_connection.h provides conn_msg / conn_send_buffer, the protocol version,
 * the OPEN/CLOSE_CONNECTION op types, PTL_CP_SERVER_PTE and PTL_INITIATOR_DEPTH. */
#include "ptl_pd.h"
#include "ptl_uuid.h"
#include "ptl_connection.h"
#include "ptl_cq.h"            /* ptl_cq: recv_cq->pte / ->ptl_cq_id / conn_mgmt_eq */
#include "ptl_object_types.h"  /* PTL_CONN_SEND_BUFFER, PTL_DEBUG/WARN, ptl_obj_conn_params */
#include "ptl_qp.h"            /* ptl_qp: ->qpn, ->recv_cq */

/* the global device map, defined in the device layer */
extern struct ptl_bxiv3_dev_map bxiv3_dev_map;

/* Control-message send — port of rdma_cm_ptl_send_request             */
/* Fire-and-forget: the heap-allocated send_buffer is DMA-mapped,      */
/* PtlMsgPutOnce'd with user_ptr = send_buffer, and freed later when   */
/* the SEND/ACK event surfaces on the device's conn_mgmt EQ (handled   */
/* by the existing ptl_cq.c dispatch, keyed on object_type ==          */
/* PTL_CONN_SEND_BUFFER).                                              */
/*                                                                     */
/* DMA goes through the ib_portals_dma_* wrappers, not the kernel      */
/* dma_* API directly: they carry the virt-DMA corner case             */
/* (ib_portals_uses_virt_dma). Replacing ib_portals is a later MR.     */
/* ------------------------------------------------------------------ */

static int ptl_cm_send_request(struct ptl_conn_send_buffer *send_buffer,
			       struct ptl_cm_id *id)
{
	struct ptl_conn_comm_pair_info *peer_info =
		&send_buffer->conn_msg.msg_header.peer_info;
	ptl_process_t target;
	int rc;

	target.phys.nid = peer_info->dest.nid;
	target.phys.pid = peer_info->dest.pid;

	send_buffer->bxiv3_dev = id->bxiv3_dev;

	/* DMA-map the message for the NIC (md.start is a DMA address) */
	send_buffer->md.length    = send_buffer->conn_msg.msg_header.total_msg_size;
	send_buffer->md.cpu_start = &send_buffer->conn_msg;
	send_buffer->md.start     = ib_portals_dma_map_single(&id->bxiv3_dev->fake_ib_dev,
							      send_buffer->md.cpu_start,
							      send_buffer->md.length,
							      DMA_TO_DEVICE);
	if (ib_portals_dma_mapping_error(&id->bxiv3_dev->fake_ib_dev,
					 send_buffer->md.start)) {
		PTL_WARN("DMA mapping failed for length %llu",
			 send_buffer->md.length);
		return -EIO;
	}
	send_buffer->md.options   = 0;
	send_buffer->md.eq_handle = id->bxiv3_dev->conn_mgmt_eq->eq;
	send_buffer->md.ct_handle = PTL_CT_NONE;

	send_buffer->msg.length    = send_buffer->conn_msg.msg_header.total_msg_size;
	send_buffer->msg.ack_req   = PTL_ACK_REQ;
	send_buffer->msg.target_id = target;
	send_buffer->msg.pt_index  = peer_info->dest.pte;
	send_buffer->msg.user_ptr  = send_buffer;
	ptl_uuid_set_op_type(&send_buffer->msg.hdr_data,
			     send_buffer->conn_msg.msg_header.msg_type);

	PTL_DEBUG("Sending CM message type %llu, %llu bytes to {nid:%d,pid:%d,pte:%d}",
		  (u64)send_buffer->conn_msg.msg_header.msg_type,
		  send_buffer->conn_msg.msg_header.total_msg_size,
		  target.phys.nid, target.phys.pid, peer_info->dest.pte);

	rc = PtlMsgPutOnce(id->bxiv3_dev->nicia_handle,
			   (const ptl_md_t *)&send_buffer->md,
			   (const ptl_msg_t *)&send_buffer->msg);
	if (rc != PTL_OK) {
		PTL_FATAL("PtlPut failed with code: %d (%s)", rc,
			  PtlToStr(rc, PTL_STR_ERROR));
		return -EIO;
	}

	return 0;
}

int ptl_cm_resolve_route(struct ptl_cm_id *id, unsigned long timeout_ms)
{
	/* XXX TODO XXX consider pinging the target here. */
	struct ptl_cm_event ev = {0};
	int rc;

	if (!id)
		return -EINVAL;

	rc = ptl_cm_id_set_state(id, PTL_CM_ROUTE_RESOLVING);
	if (rc)
		return rc;

	/* Portals has no route resolution equivalent. */
	(void)timeout_ms;

	rc = ptl_cm_id_set_state(id, PTL_CM_ROUTE_RESOLVED);
	if (rc) {
		ptl_cm_id_set_state(id, PTL_CM_ERROR);
		return rc;
	}

	ev.event  = PTL_CM_EVENT_ROUTE_RESOLVED;
	ev.status = 0;

	if (id->event_handler)
		return id->event_handler(id, &ev);

	return 0;
}

/* Connection — port of rdma_cm_portals_connect_locked                 */
/* Old conn_msg wire format; ESTABLISHED is driven later by ptl_cq.c   */
/* when the open-connection reply arrives on the conn_mgmt EQ.         */


int ptl_cm_connect_locked(struct ptl_cm_id *id, struct ptl_cm_conn_param *param)
{
	struct ptl_conn_send_buffer *send_buffer;
	char *private_data_buf;
	int rc;

	if (!id || !param)
		return -EINVAL;
	if (!id->bxiv3_dev || !id->ptl_qp)
		return -ENODEV;   /* resolve_addr + create_qp must run first */

	if (param->initiator_depth == 0) {
		PTL_DEBUG("Caution initiator_depth set to 0. Setting it to %d",
			  PTL_INITIATOR_DEPTH);
		param->initiator_depth = PTL_INITIATOR_DEPTH;
	}

	send_buffer = kzalloc(sizeof(*send_buffer) + param->private_data_len,
			      GFP_KERNEL);
	if (!send_buffer)
		return -ENOMEM;

	send_buffer->object_type = PTL_CONN_SEND_BUFFER;
	send_buffer->conn_msg.msg_header.version  = PTL_SPDK_PROTOCOL_VERSION;
	send_buffer->conn_msg.msg_header.msg_type = PTL_OPEN_CONNECTION;
	send_buffer->conn_msg.msg_header.total_msg_size =
		sizeof(send_buffer->conn_msg) + param->private_data_len;

	/* self identification */
	send_buffer->conn_msg.msg_header.peer_info.src.nid = id->self_peer.phys.nid;
	send_buffer->conn_msg.msg_header.peer_info.src.pid = id->self_peer.phys.pid;
	send_buffer->conn_msg.msg_header.peer_info.src.pte = PTL_CP_SERVER_PTE;
	/* destination */
	send_buffer->conn_msg.msg_header.peer_info.dest.nid = id->remote_peer.phys.nid;
	send_buffer->conn_msg.msg_header.peer_info.dest.pid = id->remote_peer.phys.pid;
	send_buffer->conn_msg.msg_header.peer_info.dest.pte = PTL_CP_SERVER_PTE;

	/* body: initiator resources the target must know about.
	 * rma_pte deliberately equals msg_pte  */
	send_buffer->conn_msg.conn_open.msg_pte = id->ptl_qp->recv_cq->pte;
	send_buffer->conn_msg.conn_open.rma_pte =
		send_buffer->conn_msg.conn_open.msg_pte;
	send_buffer->conn_msg.conn_open.cq_id   = id->ptl_qp->recv_cq->ptl_cq_id;
	send_buffer->conn_msg.conn_open.initiator_qp_num = id->ptl_qp->qpn;

	/* extensions */
	send_buffer->conn_msg.conn_open.is_kernel_initiator = 1;
	send_buffer->conn_msg.conn_open.nvme_cpl_start_addr = id->nvme_cpl_start;
	send_buffer->conn_msg.conn_open.nvme_cpl_queue_size =
		id->nvme_completion_queue_size;

	memcpy(&send_buffer->conn_msg.conn_open.src_addr, &id->src_addr,
	       sizeof(send_buffer->conn_msg.conn_open.src_addr));

	/* serialize conn params; receiver fixes private_data pointer */
	send_buffer->conn_msg.conn_open.conn_param = *param;
	send_buffer->conn_msg.conn_open.conn_param.private_data = NULL;

	if (param->private_data) {
		private_data_buf = (char *)send_buffer + sizeof(*send_buffer);
		memcpy(private_data_buf, param->private_data,
		       param->private_data_len);
		send_buffer->conn_msg.conn_open.conn_param.private_data_len =
			param->private_data_len;
	}

	/* keep an owned copy of the params on the id */
	id->param = *param;
	if (param->private_data) {
		id->param.private_data = kzalloc(param->private_data_len,
						 GFP_KERNEL);
		if (!id->param.private_data) {
			kfree(send_buffer);
			return -ENOMEM;
		}
		memcpy((void *)id->param.private_data, param->private_data,
		       param->private_data_len);
	}

	/* record our qp num for the wire identity */
	id->initiator_qp_num = id->ptl_qp->qpn;

	rc = ptl_cm_id_set_state(id, PTL_CM_CONNECTING);
	if (rc)
		goto err_free;

	rc = ptl_cm_send_request(send_buffer, id);
	if (rc) {
		ptl_cm_id_set_state(id, PTL_CM_ERROR);
		goto err_free;
	}

	return 0;

err_free:
	kfree((void *)id->param.private_data);
	id->param.private_data = NULL;
	kfree(send_buffer);
	return rc;
}

int ptl_cm_connect_locked_with_ptl_params(struct ptl_cm_id *id,
				       struct ptl_cm_conn_param *param,
				       struct ptl_obj_conn_params *ptl_params)
{
	if (!id || !ptl_params)
		return -EINVAL;

	id->nvme_cpl_start             = ptl_params->nvme_cpl_start_dma_addr;
	id->nvme_completion_queue_size = ptl_params->queue_size;

	return ptl_cm_connect_locked(id, param);
}

/* Disconnect — port of rdma_cm_portals_disconnect                     */

int ptl_cm_disconnect(struct ptl_cm_id *id)
{
	struct ptl_conn_send_buffer *close_req_buf;
	unsigned long flags;
	int rc;

	if (!id)
		return -EINVAL;

	spin_lock_irqsave(&id->state_lock, flags);
	if (id->cm_id_state == PTL_CM_DISCONNECTING ||
	    id->cm_id_state == PTL_CM_DISCONNECTED) {
		PTL_DEBUG("ptl_cm_id {initiator_qp_num: %d target_qp_num: %d} already disconnecting...go on",
			  id->initiator_qp_num, id->target_qp_num);
		spin_unlock_irqrestore(&id->state_lock, flags);
		return 0;
	}
	id->cm_id_state = PTL_CM_DISCONNECTING;
	spin_unlock_irqrestore(&id->state_lock, flags);

	close_req_buf = kzalloc(sizeof(*close_req_buf), GFP_KERNEL);
	if (!close_req_buf)
		return -ENOMEM;

	close_req_buf->object_type = PTL_CONN_SEND_BUFFER;
	close_req_buf->conn_msg.msg_header.version  = PTL_SPDK_PROTOCOL_VERSION;
	close_req_buf->conn_msg.msg_header.msg_type = PTL_CLOSE_CONNECTION;
	close_req_buf->conn_msg.msg_header.total_msg_size =
		sizeof(close_req_buf->conn_msg);
	/* self identification */
	close_req_buf->conn_msg.msg_header.peer_info.src.nid = id->self_peer.phys.nid;
	close_req_buf->conn_msg.msg_header.peer_info.src.pid = id->self_peer.phys.pid;
	close_req_buf->conn_msg.msg_header.peer_info.src.pte = PTL_CP_SERVER_PTE;
	/* destination */
	close_req_buf->conn_msg.msg_header.peer_info.dest.nid = id->remote_peer.phys.nid;
    close_req_buf->conn_msg.msg_header.peer_info.dest.pid = id->remote_peer.phys.pid;
	close_req_buf->conn_msg.msg_header.peer_info.dest.pte = PTL_CP_SERVER_PTE;
	/* body */
	close_req_buf->conn_msg.conn_close.initiator_qp_num = id->initiator_qp_num;
	close_req_buf->conn_msg.conn_close.target_qp_num    = id->target_qp_num;

	rc = ptl_cm_send_request(close_req_buf, id);
	if (rc) {
		kfree(close_req_buf);
		spin_lock_irqsave(&id->state_lock, flags);
		id->cm_id_state = PTL_CM_ERROR;
		spin_unlock_irqrestore(&id->state_lock, flags);
		return rc;
	}

	return 0;
}

/* Sends a CLOSE_CONNECTION_REPLY to the peer that initiated the close. Process
 * context only: the triggering handler holds drain_lock and this maps DMA. */
int ptl_cm_send_close_reply(struct ptl_cm_id *id)
{
      struct ptl_conn_send_buffer *reply_buf;
      int rc;

      reply_buf = kzalloc(sizeof(*reply_buf), GFP_ATOMIC);
      if (!reply_buf)
              return -ENOMEM;

      reply_buf->object_type = PTL_CONN_SEND_BUFFER;
      reply_buf->conn_msg.msg_header.version  = PTL_SPDK_PROTOCOL_VERSION;
      reply_buf->conn_msg.msg_header.msg_type = PTL_CLOSE_CONNECTION_REPLY;
      reply_buf->conn_msg.msg_header.total_msg_size = sizeof(reply_buf->conn_msg);
      /* self identification */
      reply_buf->conn_msg.msg_header.peer_info.src.nid = id->self_peer.phys.nid;
      reply_buf->conn_msg.msg_header.peer_info.src.pid = id->self_peer.phys.pid;
      reply_buf->conn_msg.msg_header.peer_info.src.pte = PTL_CP_SERVER_PTE;
      /* destination */
      reply_buf->conn_msg.msg_header.peer_info.dest.nid = id->remote_peer.phys.nid;
      reply_buf->conn_msg.msg_header.peer_info.dest.pid = id->remote_peer.phys.pid;
      reply_buf->conn_msg.msg_header.peer_info.dest.pte = PTL_CP_SERVER_PTE;
      /* body */
      reply_buf->conn_msg.conn_close_reply.initiator_qp_num = id->initiator_qp_num;
      reply_buf->conn_msg.conn_close_reply.target_qp_num    = id->target_qp_num;
      reply_buf->conn_msg.conn_close_reply.status = PTL_OK;

      rc = ptl_cm_send_request(reply_buf, id);
      if (rc)
              kfree(reply_buf);
      return rc;
}
/* Helpers mirrored from rdma_cm_portals */


const char *ptl_cm_event_msg(int ev)
{
	switch (ev) {
	case PTL_CM_EVENT_ADDR_RESOLVED:   return "ADDR_RESOLVED";
	case PTL_CM_EVENT_ROUTE_RESOLVED:  return "ROUTE_RESOLVED";
	case PTL_CM_EVENT_CONNECT_REQUEST: return "CONNECT_REQUEST";
	case PTL_CM_EVENT_ESTABLISHED:     return "ESTABLISHED";
	case PTL_CM_EVENT_DISCONNECTED:    return "DISCONNECTED";
	case PTL_CM_EVENT_REJECTED:        return "REJECTED";
	case PTL_CM_EVENT_ADDR_ERROR:      return "ADDR_ERROR";
	case PTL_CM_EVENT_ROUTE_ERROR:     return "ROUTE_ERROR";
	case PTL_CM_EVENT_CONNECT_ERROR:   return "CONNECT_ERROR";
	case PTL_CM_EVENT_DEVICE_REMOVAL:  return "DEVICE_REMOVAL";
	case PTL_CM_EVENT_TIMEWAIT_EXIT:   return "TIMEWAIT_EXIT";
	default:                           return "UNKNOWN";
	}
}

const void *ptl_cm_reject_msg(struct ptl_cm_id *id, int status)
{
	(void)id;
	(void)status;
	return "connection rejected by target";
}

/* Nothing emits PTL_CM_EVENT_REJECTED yet, so this is unreachable: the
 * wire protocol has no reject message. Fail loudly rather than return
 * empty data, so whoever wires up rejection knows it needs a real body. */
const void *ptl_cm_consumer_reject_data(struct ptl_cm_id *id,
					struct ptl_cm_event *ev, u8 *len)
{
	(void)id;
	(void)ev;
	(void)len;
	PTL_FATAL("ptl_cm_consumer_reject_data unimplemented");
	return ERR_PTR(-EOPNOTSUPP);
}
/* in ptl_qp.c — the CM-facing entry, replaces rdma_cm_portals_create_qp */
int ptl_cm_create_qp(struct ptl_cm_id *ptl_id, struct ib_pd *pd,
                  struct ib_qp_init_attr *attr)
{
    struct ptl_pd *ptl_pd;

    ptl_pd = container_of(pd, struct ptl_pd, fake_pd);
    if (PTL_PD != ptl_pd->object_type) {
        PTL_FATAL("Corrupted ptl_pd!");
        return -EINVAL;
    }

    ptl_id->ptl_qp = ptl_qp_create(ptl_id, ptl_pd, attr);
    PTL_DEBUG("Created QP successfully");
    return IS_ERR(ptl_id->ptl_qp) ? -EINVAL : 0;
}
int ptl_cm_destroy_qp(struct ptl_cm_id *id)
{
    (void)id;
    PTL_FATAL("ptl_destroy_qp unimplemented");
    return -EOPNOTSUPP;
}
