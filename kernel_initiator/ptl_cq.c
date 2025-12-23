#include "ptl_cq.h"
#include "asm-generic/errno.h"
#include "linux/gfp_types.h"
#include "linux/slab.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cm_id.h"
#include "ptl_connection.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include "ptl_uuid.h"
#include "rdma/rdma_cm.h"
#include <asm-generic/errno-base.h>
#include <linux/err.h>
#include <linux/hashtable.h>
#include <portals4.h>
#include <portals4_bxiext.h>

static void ptl_cnxt_process_get(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_get_overflow(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}


static void ptl_handle_open_connection_reply(ptl_event_t *event, struct ptl_cq *ptl_cq)
{
	struct rdma_cm_event *cm_event = NULL;
	struct ptl_bxiv3_device_recv_buffer *recv_buffer = event->user_ptr;
	struct ptl_conn_msg *msg = recv_buffer->conn_msg;
	struct ptl_bxiv3_qp_map_entry *entry;
	struct ptl_qp *ptl_qp = NULL;
	int qpn;
	cm_event = kzalloc(sizeof(*cm_event), GFP_KERNEL);

	qpn = ptl_uuid_get_initiator_qp_num(msg->conn_open_reply.uuid);
	PTL_DEBUG("<PTL_OPEN_CONNECTION_REPLY> rlength: %llu mlength: %llu", event->rlength, event->mlength);
	PTL_DEBUG("Initiator qp num for which this event is: %d", qpn);
	PTL_DEBUG("Target qp num for which this event is: %d", ptl_uuid_get_target_qp_num(msg->conn_open_reply.uuid));
	spin_lock(&ptl_cq->bxiv3_dev->qp_map_lock);
	hash_for_each_possible(ptl_cq->bxiv3_dev->qp_map, entry, node, qpn) {
		if (entry->key == qpn) {
			ptl_qp = entry->ptl_qp;
			break; // Exit immediately once found
		}
	}
	spin_unlock(&ptl_cq->bxiv3_dev->qp_map_lock);
	if (NULL == ptl_qp) {
		PTL_WARN("QPN: %d not found!, is Target ok in its health?", qpn);
		cm_event->event = RDMA_CM_EVENT_CONNECT_ERROR;
		cm_event->status = -ENETUNREACH;
	} else {
		cm_event->event = RDMA_CM_EVENT_ESTABLISHED;
	}

	ptl_qp->ptl_id->event_handler(&ptl_qp->ptl_id->fake_cm_id, cm_event);
	PTL_DEBUG("</PTL_OPEN_CONNECTION_REPLY> QPN: %d FOUND", qpn);
}

static void ptl_handle_close_connection_reply(ptl_event_t *event, struct ptl_cq *ptl_cq)
{
	struct ptl_bxiv3_device_recv_buffer *recv_buffer = event->user_ptr;
	struct ptl_conn_msg *msg = recv_buffer->conn_msg;
	PTL_DEBUG("<PTL_CLOSE_CONNECTION_REPLY>");
	PTL_DEBUG("Initiator qp num for which this event is: %d", ptl_uuid_get_initiator_qp_num(msg->conn_close_reply.uuid));
	PTL_DEBUG("Target qp num for which this event is: %d", ptl_uuid_get_target_qp_num(msg->conn_close_reply.uuid));
	PTL_DEBUG("</PTL_CLOSE_CONNECTION_REPLY>");
}

static void ptl_handle_nvme_cpl(ptl_event_t *event, struct ptl_cq *ptl_cq)
{
	PTL_DEBUG("<NVMeoF_cpl>");
	PTL_DEBUG("</NVMeoF_cpl>");
}


static void ptl_cnxt_process_put(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	/*XXX TODO XXX fix the target to report it specifically.*/
	if (NVMeOF_cpl == event.hdr_data) {
		ptl_handle_nvme_cpl(&event, ptl_cq);
	} else if (PTL_OPEN_CONNECTION_REPLY == event.hdr_data) {
		ptl_handle_open_connection_reply(&event, ptl_cq);
	} else if (PTL_CLOSE_CONNECTION_REPLY == event.hdr_data) {
		ptl_handle_close_connection_reply(&event, ptl_cq);
	} else {
		PTL_FATAL("No action register for PtlPut object of type: %llu", event.hdr_data);
	}
}

static void ptl_cnxt_process_put_overflow(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}


static void ptl_cnxt_process_atomic(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}


static void ptl_cnxt_process_atomic_overflow(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}


static void ptl_cnxt_process_fetch_atomic(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_fetch_atomic_overflow(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_reply(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_send(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_DEBUG("Just a send ignore");
}


static void ptl_cnxt_process_ack(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_DEBUG("Just an ack (send has moved its data to the remote memory), ignore");
}

static void ptl_cnxt_process_bt_disabled(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_auto_unlink(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	struct ptl_bxiv3_device_recv_buffer *recv_buffer = event.user_ptr;
	int rc;

	if (NVMeOF_cmd != event.hdr_data && NVMeOF_cpl != event.hdr_data) {
		if (PTL_CONN_RECV_BUFFER != recv_buffer->obj_type) {
			PTL_FATAL("Corrupted recv buffer");
		}
		PTL_DEBUG("Got an unlink event for a receive buffer of the connections protocol. Reappend the receive buffer for PTL_CONNECTIONS");
		rc = PtlMEAppend(ptl_cq->bxiv3_dev->nicia_handle,
		                 PTL_CP_SERVER_PTE,
		                 &recv_buffer->me,
		                 PTL_PRIORITY_LIST,
		                 recv_buffer,
		                 &recv_buffer->meh);
		if (rc != PTL_OK) {
			PTL_FATAL("PtlMEAppend failed with code: %d", rc);
		}
		return;
	}
	PTL_DEBUG("Got an unlink event for NVMe shit, ignore");
}


static void ptl_cnxt_process_auto_free(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_search(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_link(ptl_event_t event, struct ptl_cq *ptl_cq)
{
	PTL_FATAL("UNIMPLEMENTED");
}

typedef void(*process_event)(ptl_event_t event, struct ptl_cq *ptl_cq);
static process_event handler[16] = {
	ptl_cnxt_process_get,          ptl_cnxt_process_get_overflow,
	ptl_cnxt_process_put,          ptl_cnxt_process_put_overflow,
	ptl_cnxt_process_atomic,       ptl_cnxt_process_atomic_overflow,
	ptl_cnxt_process_fetch_atomic, ptl_cnxt_process_fetch_atomic_overflow,
	ptl_cnxt_process_reply,        ptl_cnxt_process_send,
	ptl_cnxt_process_ack,          ptl_cnxt_process_bt_disabled,
	ptl_cnxt_process_auto_unlink,  ptl_cnxt_process_auto_free,
	ptl_cnxt_process_search,       ptl_cnxt_process_link
};

void ptl_eq_callback(void *arg, ptl_handle_eq_t eqh)
{
	struct ptl_cq *ptl_cq = arg;
	ptl_event_t event;
	int rc;

	if (PTL_CQ != ptl_cq->obj_type) {
		PTL_FATAL("Corrupted PTL_CQ");
		return;
	}

	for (;;) {
		rc = PtlEQGet(eqh, &event);
		if (rc == PTL_OK) {

			PTL_DEBUG("Event: iface_id:%d EV:%d(%s) PTE:%d match_bits=0x%llx "
			          "initiator:{nid:%d,pid:%d,pt_index: %d} ni_fail:%d(%s) fc_err:%d",
			          ptl_cq->bxiv3_dev->iface_id,
			          event.type, PtlToStr(event.type, PTL_STR_EVENT),
			          event.pt_index,
			          (unsigned long long)event.match_bits,
			          event.initiator.phys.nid,
			          event.initiator.phys.pid,
			          event.pt_index,
			          event.ni_fail_type, PtlToStr(event.ni_fail_type, PTL_STR_FAIL_TYPE),
			          event.fc_err);
			if (event.type > sizeof(handler) / sizeof(handler[0])) {
				PTL_FATAL("Cannot handle event of type: %d", event.type);
			}
			if (event.ni_fail_type != PTL_OK) {
				PTL_FATAL("Oops error");
			}
			handler[event.type](event, ptl_cq);
			continue;
		} else if (rc == PTL_EQ_EMPTY) {
			break;
		} else if (rc == PTL_EQ_DROPPED) {
			PTL_DEBUG("EQ dropped events (overflow)");
			break;
		} else {
			PTL_FATAL("PtlEQGet unhandled code %d", rc);
			break;
		}
	}
}


struct ptl_cq *ptl_cq_create(struct ptl_cq_pool *cq_pool,
                             struct ptl_bxiv3_device *bxiv3_dev, int nr_cqes,
                             ptl_pt_index_t pte, enum ib_poll_context poll_ctx)
{
	struct ptl_cq *cq;
	int rc;
	if (poll_ctx != IB_POLL_SOFTIRQ) {
		PTL_FATAL("Sorry! Currently BXIv3 NVMe-oF initiator supports only "
		          "IB_POLL_SOFTIRQ mode");
		return ERR_PTR(-EINVAL);
	}

	PTL_DEBUG("%s", cq_pool ? "CP POOL SET OK!" : "NULL, this PTL_CQ does not belong to a pool");

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		PTL_FATAL("Out of memory");
		return ERR_PTR(-ENOMEM);
	}

	cq->obj_type = PTL_CQ;
	cq->cq_pool = cq_pool;
	cq->bxiv3_dev = bxiv3_dev;
	cq->ptl_cq_id = pte;
	cq->nr_cqes = nr_cqes;


	rc = PtlEQAllocAsync(bxiv3_dev->nicia_handle,
	                     cq->nr_cqes, &cq->eq, ptl_eq_callback, cq,
	                     cq->bxiv3_dev->intr_index);
	if (PTL_OK != rc) {
		PTL_FATAL("Failed to allocate event queue for pte: %d", pte);
		goto err;
	}
	rc = PtlPTAlloc(bxiv3_dev->nicia_handle, 0, cq->eq,
	                pte, &cq->pte);
	if (PTL_OK != rc) {
		PTL_FATAL("Failed to allocate PTE: %u with reason: %d",
		          pte, rc);
		goto free_cq;
	}
	rc = PtlPTEnable(bxiv3_dev->nicia_handle, cq->pte);
	if (PTL_OK != rc) {
		PTL_FATAL("Failed to enable PTE: %u with reason: %d",
		          pte, rc);
		goto free_cq;
	}
	PTL_DEBUG("Enabled for iface_id: %d PTE: %u and created cq with %d number of entries", bxiv3_dev->iface_id,
	          cq->pte, nr_cqes);
	return cq;
free_cq:
	rc = PtlEQFree(cq->eq);
	if (PTL_OK != rc)
		PTL_WARN
		("Failed to free event queue with code: %d, continuing",
		 rc);
err:
	kfree(cq);
	return ERR_PTR(-EINVAL);
}

ptl_pt_index_t ptl_cq_destroy(struct ptl_cq *ptl_cq)
{
	ptl_pt_index_t pte;
	int rc;

	if (!ptl_cq) {
		PTL_WARN("Attempting to destroy NULL cq");
		return -1;
	}
	pte = ptl_cq->pte;


	/* Free the portal table entry */
	rc = PtlPTFree(ptl_cq->bxiv3_dev->nicia_handle,
	               ptl_cq->pte);
	if (PTL_OK != rc) {
		PTL_WARN
		("Failed to free PTE: %d with code: %d, continuing",
		 ptl_cq->pte, rc);
	}

	/* Free the event queue */
	rc = PtlEQFree(ptl_cq->eq);
	if (PTL_OK != rc) {
		PTL_WARN
		("Failed to free event queue with code: %d, continuing",
		 rc);
	}
	PTL_DEBUG("Destroyed cq for PTE: %d", ptl_cq->pte);
	/* Free the cq structure */
	kfree(ptl_cq);
	return pte;
}
