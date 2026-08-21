#include "ptl_cq.h"
#include "asm-generic/errno.h"
#include "ib_portals.h"
#include "linux/gfp_types.h"
#include "linux/slab.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cm_id.h"
#include "ptl_connection.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include "ptl_recv_op.h"
#include "ptl_uuid.h"
#include <asm-generic/errno-base.h>
#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/workqueue.h>
#include <nvme.h>
#include <portals4.h>
#include <portals4_bxiext.h>

static void ptl_cnxt_process_get(ptl_event_t event, struct ptl_cq *ptl_cq) {
  PTL_DEBUG("Target performed an RDMA read from me (the initiator) ignore");
}

static void ptl_cnxt_process_get_overflow(ptl_event_t event,
                                          struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_handle_open_connection_reply(ptl_event_t *event,
                                             struct ptl_cq *ptl_cq) {
  struct ptl_cm_event cm_event = {0};
  struct ptl_conn_recv_buffer *recv_buffer = event->user_ptr;
  struct ptl_conn_msg *msg = recv_buffer->conn_msg;
  struct ptl_bxiv3_qp_map_entry *entry;
  struct ptl_qp *ptl_qp = NULL;

  int qpn;

  qpn = msg->conn_open_reply.initiator_qp_num;
  PTL_DEBUG("REPLY: msg_pte=%d rma_pte=%d cq_id=%d status=%d",
            msg->conn_open_reply.msg_pte, msg->conn_open_reply.rma_pte,
            msg->conn_open_reply.cq_id, msg->conn_open_reply.status);
  PTL_DEBUG("<PTL_OPEN_CONNECTION_REPLY> rlength: %llu mlength: %llu",
            event->rlength, event->mlength);
  PTL_DEBUG("Initiator qp num for which this event is: %d", qpn);
  PTL_DEBUG("Target qp num for which this event is: %d",
            msg->conn_open_reply.target_qp_num);
  spin_lock(&ptl_cq->bxiv3_dev->qp_map_lock);
  hash_for_each_possible(ptl_cq->bxiv3_dev->qp_map, entry, node, qpn) {
    if (entry->key == qpn) {
      ptl_qp = entry->ptl_qp;
      break; // Exit immediately once found
    }
  }

  if (NULL == ptl_qp) {
    /* Not fatal: a reply with no matching QP is an ordinary race once
     * nvme_rdma_wait_for_cm() times out and tears the queue down. Note the
     * unlock - the old PTL_FATAL() BUG()ed while holding qp_map_lock. */
    spin_unlock(&ptl_cq->bxiv3_dev->qp_map_lock);
    PTL_WARN("Stale open_connection_reply for qp {initiator_qp_num: %d, "
             "target_qp_num: %d} - connection already torn down, dropping",
             msg->conn_open_reply.initiator_qp_num,
             msg->conn_open_reply.target_qp_num);
    return;
  }

  ptl_qp->ptl_id->remote_msg_pte = msg->conn_open_reply.msg_pte;
  ptl_qp->ptl_id->remote_rma_pte = msg->conn_open_reply.rma_pte;
  ptl_qp->ptl_id->remote_cq_id = msg->conn_open_reply.cq_id;
  ptl_qp->ptl_id->initiator_qp_num = msg->conn_open_reply.initiator_qp_num;
  ptl_qp->ptl_id->target_qp_num = msg->conn_open_reply.target_qp_num;
  ptl_qp->ptl_id->remote_cq_id = msg->conn_open_reply.cq_id;

  spin_unlock(&ptl_cq->bxiv3_dev->qp_map_lock);
  if (ptl_cm_id_set_state(ptl_qp->ptl_id, PTL_CM_ESTABLISHED)) {
    PTL_WARN("QPN: %d reply arrived in unexpected CM state %d", qpn,
             ptl_qp->ptl_id->cm_id_state);
    return;
  }
  cm_event.event = PTL_CM_EVENT_ESTABLISHED;
  cm_event.status = 0;
  if (ptl_qp->ptl_id->event_handler)
    ptl_qp->ptl_id->event_handler(ptl_qp->ptl_id, &cm_event);
  PTL_DEBUG("</PTL_OPEN_CONNECTION_REPLY> QPN: %d FOUND", qpn);
}

static void ptl_handle_close_connection_reply(ptl_event_t *event,
                                              struct ptl_cq *ptl_cq) {
  struct ptl_conn_recv_buffer *recv_buffer = event->user_ptr;
  struct ptl_conn_msg *msg = recv_buffer->conn_msg;
  PTL_DEBUG("<PTL_CLOSE_CONNECTION_REPLY>");
  PTL_DEBUG("Initiator qp num for which this event is: %d",
            msg->conn_close_reply.initiator_qp_num);
  PTL_DEBUG("Target qp num for which this event is: %d",
            msg->conn_close_reply.target_qp_num);
  PTL_DEBUG("</PTL_CLOSE_CONNECTION_REPLY>");
}

/* Target-initiated close (PTL_CLOSE_CONNECTION): ack with a CLOSE_CONNECTION_REPLY
 * and raise DISCONNECTED so nvme tears down. The reply send may sleep and this
 * runs under drain_lock, so it is deferred to a work item. */
struct ptl_close_work {
  struct work_struct work;
  struct ptl_cm_id  *id;
};

static void ptl_close_work_fn(struct work_struct *w) {
  struct ptl_close_work *cw =
      container_of(w, struct ptl_close_work, work);
  struct ptl_cm_event cm_event = {0};
  int rc = ptl_cm_send_close_reply(cw->id);
  if (rc)
    PTL_WARN("CLOSE_CONNECTION_REPLY send failed rc=%d", rc);
  cm_event.event  = PTL_CM_EVENT_DISCONNECTED;
  cm_event.status = 0;
  if (cw->id->event_handler)
    cw->id->event_handler(cw->id, &cm_event);
  kfree(cw);
}

static void ptl_handle_close_connection(ptl_event_t *event,
                                        struct ptl_cq *ptl_cq) {
  struct ptl_conn_recv_buffer *recv_buffer = event->user_ptr;
  struct ptl_conn_msg *msg = recv_buffer->conn_msg;
  struct ptl_bxiv3_qp_map_entry *entry;
  struct ptl_qp *ptl_qp = NULL;
  struct ptl_close_work *cw;
  int qpn = msg->conn_close.initiator_qp_num;

  PTL_DEBUG("<PTL_CLOSE_CONNECTION> init_qp=%d tgt_qp=%d",
            msg->conn_close.initiator_qp_num,
            msg->conn_close.target_qp_num);

  spin_lock(&ptl_cq->bxiv3_dev->qp_map_lock);
  hash_for_each_possible(ptl_cq->bxiv3_dev->qp_map, entry, node, qpn) {
    if (entry->key == qpn) {
      ptl_qp = entry->ptl_qp;
      break;
    }
  }
  spin_unlock(&ptl_cq->bxiv3_dev->qp_map_lock);
  if (NULL == ptl_qp) {
    PTL_WARN("Target-initiated close for unknown qpn %d, ignoring", qpn);
    return;
  }

  if (ptl_qp->ptl_id->cm_id_state != PTL_CM_DISCONNECTING &&
      ptl_qp->ptl_id->cm_id_state != PTL_CM_DISCONNECTED)
    ptl_cm_id_set_state(ptl_qp->ptl_id, PTL_CM_DISCONNECTING);

  cw = kzalloc(sizeof(*cw), GFP_ATOMIC);
  if (!cw) {
    PTL_WARN("close_work OOM for qpn %d", qpn);
    return;
  }
  cw->id = ptl_qp->ptl_id;
  INIT_WORK(&cw->work, ptl_close_work_fn);
  schedule_work(&cw->work); /* reply + DISCONNECTED off the drain_lock */
}

/* Fail one completion instead of the whole machine: a cid that disagrees with the
 * buffer it landed in is untrusted, so deliver an errored wc and let NVMe retry
 * rather than stalling until the 30 s command timeout. */
static void ptl_fail_nvme_cpl(struct ptl_cq *ptl_cq, struct ptl_qp *ptl_qp,
                              u16 nvme_cid) {
  struct ptl_recv_op *recv_op_meta;
  struct ib_wc wc;

  if (nvme_cid >= ptl_qp->recv_op_meta_size)
    return; /* cannot index safely; command will time out and reconnect */
  recv_op_meta = &ptl_qp->recv_op_meta[nvme_cid];
  if (false == recv_op_meta->is_set || NULL == recv_op_meta->wr_cqe)
    return; /* nothing outstanding in this slot, nothing to complete */

  memset(&wc, 0, sizeof(wc));
  wc.status = IB_WC_GENERAL_ERR;
  wc.opcode = IB_WC_RECV;
  wc.wr_id = recv_op_meta->wr_id;
  wc.wr_cqe = recv_op_meta->wr_cqe;
  wc.qp = &recv_op_meta->ptl_qp->fake_qp;
  wc.src_qp = recv_op_meta->ptl_qp->qpn;

  recv_op_meta->is_set = false;
  recv_op_meta->late_wc_valid = false;
  recv_op_meta->parts_num_received = 0;
  recv_op_meta->total_parts = 0;
  recv_op_meta->wr_cqe->done(&ptl_cq->fake_cq, &wc);
}

static void ptl_handle_nvme_cpl(ptl_event_t *event, struct ptl_cq *ptl_cq) {
  struct ptl_recv_op *recv_op_meta = NULL;
  struct ptl_qp *ptl_qp;
  struct ib_wc wc;
  u16 nvme_cid;
  u16 calculated_cid;
  /*<gesalous> non-matching feat*/
  // vanilla case
  // recv_op = event->user_ptr;

  ptl_qp = event->user_ptr;
  /* user_ptr is wire-derived: a stale event after a reconnect can name a ptl_qp
   * that was freed and reused, so drop it rather than BUG().
   *still dereferences a possibly-dangling pointer; closing that needs
   * a qp handle table instead of a raw pointer in user_ptr. */
  if (NULL == ptl_qp || PTL_QP != ptl_qp->object_type) {
    PTL_WARN_RL("nvme_cpl with stale/invalid qp context %p, dropping event",
                ptl_qp);
    return;
  }
  PTL_DEBUG("nvme_cpl: got nvme_completion at addr: 0x%llx pte: %d qpn: %d",
            event->start, event->pt_index, ptl_qp->qpn);

  nvme_cid = ptl_uuid_get_nvme_cid(&event->hdr_data);
  /* new addition: everything below is derived from the wire, so none of it may
   * BUG(). A remote peer (or a stale event arriving after a reconnect) must not
   * be able to reboot this host  */
  if (nvme_cid >= ptl_qp->recv_op_meta_size) {
    PTL_WARN_RL("Wrong recv_op_meta_idx: it is: %u size is: %lu qpn: %d, "
                "dropping event",
                nvme_cid, ptl_qp->recv_op_meta_size, ptl_qp->qpn);
    return;
  }
  calculated_cid =
      (u16)(((u64)event->start - (u64)ptl_qp->ptl_id->nvme_cpl_start) /
            sizeof(struct nvme_completion));
  if (calculated_cid != nvme_cid) {
    PTL_WARN_RL("Corrupted nvme_cid value: %u calculated: %u qpn: %d, "
                "failing this completion",
                nvme_cid, calculated_cid, ptl_qp->qpn);
    ptl_fail_nvme_cpl(ptl_cq, ptl_qp, nvme_cid);
    return;
  }
  recv_op_meta = &ptl_qp->recv_op_meta[nvme_cid];

  // PTL_DEBUG("nvme_cpl: recv_op_meta_idx = %llu for qpn: %d",recv_op_meta_idx,
  // ptl_qp->qpn);
  if (false == recv_op_meta->is_set) {
    PTL_WARN_RL("Metadata not set for qpn: %d nvme_cid: %u, dropping event",
                ptl_qp->qpn, nvme_cid);
    return;
  }
  wc.status =
      event->ni_fail_type == PTL_NI_OK ? IB_WC_SUCCESS : IB_WC_LOC_PROT_ERR;
  wc.opcode = IB_WC_RECV;
  wc.wr_id = recv_op_meta->wr_id;   // Not usefull from the driver
  wc.wr_cqe = recv_op_meta->wr_cqe; // Very useful!
  wc.byte_len = event->rlength;
  wc.qp = &recv_op_meta->ptl_qp->fake_qp; // Very useful!
  wc.src_qp = recv_op_meta->ptl_qp->qpn;
  wc.wc_flags = IB_WC_WITH_INVALIDATE;
  wc.ex.invalidate_rkey = PTL_MAGIC_FAKE_MR_KEY;
  /*Question 1: How many parts does this nvme_cpl consists of?*/
  recv_op_meta->total_parts = ptl_uuid_get_total_parts(&event->hdr_data);
  if (recv_op_meta->total_parts != recv_op_meta->parts_num_received) {
    PTL_DEBUG("Out of order nvme cpl. Ok we are going to wait to "
              "received the missing parts for nvme cid: %u. {received parts: "
              "%u total_parts: %u}",
              nvme_cid, recv_op_meta->parts_num_received,
              recv_op_meta->total_parts);
    recv_op_meta->late_wc = wc;
    recv_op_meta->late_wc_valid = true;
    return;
  }
  PTL_DEBUG("On time wc delivery for nvme cid: %u with total parts: %u",
            ptl_uuid_get_nvme_cid(&event->hdr_data), recv_op_meta->total_parts);
  recv_op_meta->is_set = false;
  recv_op_meta->late_wc_valid = false;
  recv_op_meta->parts_num_received = 0;
  recv_op_meta->total_parts = 0;
  recv_op_meta->wr_cqe->done(&ptl_cq->fake_cq, &wc);
}

static void ptl_handle_rdma_write(ptl_event_t *event, struct ptl_cq *ptl_cq) {
  struct ptl_recv_op *recv_op_meta = NULL;
  struct ptl_qp *ptl_qp = event->user_ptr;
  u16 nvme_cid;


  /* new addition: same wire-derived context as ptl_handle_nvme_cpl - a stale
   * post-reconnect event must not be able to BUG() the host. See :214. */
  if (NULL == ptl_qp || PTL_QP != ptl_qp->object_type) {
    PTL_WARN_RL("rdma_write with stale/invalid qp context %p, dropping event",
                ptl_qp);
    return;
  }

  // sanity check
  if (event->rlength == sizeof(struct nvme_completion)) {
    PTL_WARN_RL("rdma_write sized like an nvme_completion, dropping event");
    return;
  }

  nvme_cid = ptl_uuid_get_nvme_cid(&event->hdr_data);
  PTL_DEBUG("nvme_write: Target performed an RDMA write to me at iova:0x%llx, "
            "ignore it is just data from the target pte: %d qpn: %d nvme_cid: "
            "%d queue size: %d",
            event->start, event->pt_index, ptl_qp->qpn, nvme_cid,
            ptl_qp->recv_op_meta_size);
  /* new addition: nvme_cid is a wire value and this path had no bounds check at
   * all - an out-of-range cid indexed recv_op_meta[] straight out of bounds. */
  if (nvme_cid >= ptl_qp->recv_op_meta_size) {
    PTL_WARN_RL("rdma_write cid %u out of range (size %lu) qpn: %d, dropping",
                nvme_cid, ptl_qp->recv_op_meta_size, ptl_qp->qpn);
    return;
  }
  recv_op_meta = &ptl_qp->recv_op_meta[nvme_cid];
  ++recv_op_meta->parts_num_received;
  PTL_DEBUG("[RDMA WRITE interrupt] Got part for {nvme cid: %u "
            "parts_num_received: %u total_parts: %u}",
            nvme_cid, recv_op_meta->parts_num_received,
            recv_op_meta->total_parts);

  if (recv_op_meta->parts_num_received == recv_op_meta->total_parts) {
    PTL_DEBUG("Late wc delivery for nvme cid: %u with total parts: %u",
              nvme_cid, recv_op_meta->total_parts);
    recv_op_meta->is_set = false;
    recv_op_meta->late_wc_valid = false;
    recv_op_meta->parts_num_received = 0;
    recv_op_meta->total_parts = 0;
    recv_op_meta->wr_cqe->done(&ptl_cq->fake_cq, &recv_op_meta->late_wc);
  }
}

static void ptl_cnxt_process_put(ptl_event_t event, struct ptl_cq *ptl_cq) {
  int op_type = ptl_uuid_get_op_type(&event.hdr_data);
  /*XXX TODO XXX fix the target to report it specifically.*/
  if (NVMeOF_cpl == op_type) {
    ptl_handle_nvme_cpl(&event, ptl_cq);
  } else if (NVMeOF_rma == op_type) {
    ptl_handle_rdma_write(&event, ptl_cq);
  } else if (PTL_OPEN_CONNECTION_REPLY == op_type) {
    ptl_handle_open_connection_reply(&event, ptl_cq);
  } else if (PTL_CLOSE_CONNECTION_REPLY == op_type) {
    ptl_handle_close_connection_reply(&event, ptl_cq);
  } else if (PTL_CLOSE_CONNECTION == op_type) {
    ptl_handle_close_connection(&event, ptl_cq);
  } else if (NVMeOF_cmd == op_type) {
    PTL_FATAL("Got an NVMeOF_cmd. I am the initiator hello?");
  } else {
    PTL_FATAL("No action registered for PtlPut object of type: %d", op_type);
  }
}

static void ptl_cnxt_process_put_overflow(ptl_event_t event,
                                          struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_atomic(ptl_event_t event, struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_atomic_overflow(ptl_event_t event,
                                             struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_fetch_atomic(ptl_event_t event,
                                          struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_fetch_atomic_overflow(ptl_event_t event,
                                                   struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_reply(ptl_event_t event, struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_send(ptl_event_t event, struct ptl_cq *ptl_cq) {
  PTL_DEBUG("Just a send ignore");
}

static void ptl_cnxt_process_ack(ptl_event_t event, struct ptl_cq *ptl_cq) {
  struct ptl_send_op *send_op;
  struct ptl_conn_send_buffer *conn_send_buffer;
  ptl_obj_type_e *obj_type = event.user_ptr;
  struct ib_wc wc;
  if (obj_type == NULL) {
    PTL_DEBUG("Unsignaled PTL_EVENT_ACK ignore and processd");
    return;
  }

  if (PTL_CONN_SEND_BUFFER == *obj_type) {
    conn_send_buffer = event.user_ptr;
    PTL_DEBUG("PTL_EVENT_ACK for PTL_OPEN_CONNECTION unmap buffer...");
    ib_portals_dma_unmap_single(&conn_send_buffer->bxiv3_dev->fake_ib_dev,
                                conn_send_buffer->md.start,
                                conn_send_buffer->md.length, DMA_TO_DEVICE);
    PTL_DEBUG("PTL_EVENT_ACK for PTL_OPEN_CONNECTION freeing buffer...");
    kfree(conn_send_buffer);
    return;
  }

  if (PTL_SEND_OP != *obj_type) {
    PTL_FATAL("PTL_EVENT_ACK Corrupted object type");
  }

  send_op = event.user_ptr;
  wc.status =
      event.ni_fail_type == PTL_NI_OK ? IB_WC_SUCCESS : IB_WC_LOC_PROT_ERR;
  wc.opcode = IB_WC_SEND;
  wc.wr_id = send_op->wr_id;
  wc.wr_cqe = send_op->wr_cqe;
  wc.byte_len = event.rlength;
  wc.qp = &send_op->ptl_qp->fake_qp;
  wc.src_qp = send_op->ptl_qp->qpn;
  PTL_DEBUG("PTL_EVENT_ACK (send has moved its data to the remote memory). "
            "Calling its callback");
  send_op->wr_cqe->done(&ptl_cq->fake_cq, &wc);
  atomic_long_fetch_sub(1, &send_op->ptl_qp->pending_nvme_cmds);
  kfree(send_op);
}

static void ptl_cnxt_process_bt_disabled(ptl_event_t event,
                                         struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_auto_unlink(ptl_event_t event,
                                         struct ptl_cq *ptl_cq) {
  ptl_obj_type_e *obj_type = event.user_ptr;
  struct ptl_conn_recv_buffer *recv_buffer;
  struct ptl_recv_op *recv_op;

  int rc;
  if (NULL == obj_type) {
    PTL_FATAL("Recv buffer without metadata? Cannot happen in Nida!");
  }

  if (PTL_RECV_OP == *obj_type) {
    recv_op = event.user_ptr;
    PTL_FATAL("Autounlink for an NVMeOF_cpl buffer. This should not happen "
              "without matching support");
    return;
  }
  if (PTL_CONN_RECV_BUFFER != *obj_type) {
    PTL_FATAL("Corrupted object type: %d", obj_type);
  }
  recv_buffer = event.user_ptr;

  PTL_DEBUG("Got an unlink event for a receive buffer of the connections "
            "protocol. Reappend the receive buffer for PTL_CONNECTIONS");
  rc = PtlLEAppend(ptl_cq->bxiv3_dev->nicia_handle, PTL_CP_SERVER_PTE,
                   &recv_buffer->le, PTL_PRIORITY_LIST, recv_buffer,
                   &recv_buffer->leh);
  if (rc != PTL_OK) {
    PTL_FATAL("PtlLEAppend failed with reason: %s",
              PtlToStr(rc, PTL_STR_ERROR));
  }
}

static void ptl_cnxt_process_auto_free(ptl_event_t event,
                                       struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_search(ptl_event_t event, struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

static void ptl_cnxt_process_link(ptl_event_t event, struct ptl_cq *ptl_cq) {
  PTL_FATAL("UNIMPLEMENTED");
}

typedef void (*process_event)(ptl_event_t event, struct ptl_cq *ptl_cq);
static process_event handler[16] = {
    ptl_cnxt_process_get,          ptl_cnxt_process_get_overflow,
    ptl_cnxt_process_put,          ptl_cnxt_process_put_overflow,
    ptl_cnxt_process_atomic,       ptl_cnxt_process_atomic_overflow,
    ptl_cnxt_process_fetch_atomic, ptl_cnxt_process_fetch_atomic_overflow,
    ptl_cnxt_process_reply,        ptl_cnxt_process_send,
    ptl_cnxt_process_ack,          ptl_cnxt_process_bt_disabled,
    ptl_cnxt_process_auto_unlink,  ptl_cnxt_process_auto_free,
    ptl_cnxt_process_search,       ptl_cnxt_process_link};

#define PTL_CQ_POLL_MS 250   //new addition (EQ poll fallback interval)

/* EQ poll fallback is compiled OUT by default; build with -DPTL_EQ_POLL to include it.
* This flag is readable at runtime so a loaded module can say which variant it is:
* cat /sys/module/bxiv3_initiator/parameters/ptl_eq_poll_enabled */
#ifdef PTL_EQ_POLL
static bool ptl_eq_poll_enabled = true;
#else
static bool ptl_eq_poll_enabled;
#endif
module_param(ptl_eq_poll_enabled, bool, 0444);
MODULE_PARM_DESC(ptl_eq_poll_enabled,
                 "EQ poll fallback compiled in (rebuild with -DPTL_EQ_POLL to enable)");

/* Shared drain used by both the interrupt callback and the poll fallback;
 * spin_trylock keeps the two off each other's reassembly state. Handlers run
 * under this spinlock and must not sleep. */
static void ptl_eq_drain(struct ptl_cq *ptl_cq) {
  ptl_event_t event;
  int rc;
  if (!spin_trylock(&ptl_cq->drain_lock))
    return; /* another context already draining this EQ */
  for (;;) {
    rc = PtlEQGet(ptl_cq->eq, &event); /* was eqh; poller has no eqh */
    /* PTL_EQ_DROPPED carries a VALID event, but events BEFORE it were lost.
     * Deliberately fatal: a gap in the completion stream corrupts NVMe state in
     * ways that surface far from here and long after. Stop at the point of loss
     * so it gets reported, rather than degrading quietly. */
    if (rc == PTL_OK || rc == PTL_EQ_DROPPED) {
      if (rc == PTL_EQ_DROPPED) {
        PTL_FATAL("EQ overflow on iface_id:%d pte:%d - events were dropped "
                  "BEFORE this one, the completion stream has a gap",
                  ptl_cq->bxiv3_dev->iface_id, ptl_cq->pte);
      }
      PTL_DEBUG(
          "Event: iface_id:%d EV:%d(%s) PTE:%d match_bits=0x%llx "
          "initiator:{nid:%d,pid:%d,pt_index: %d} ni_fail:%d(%s) fc_err:%d",
          ptl_cq->bxiv3_dev->iface_id, event.type,
          PtlToStr(event.type, PTL_STR_EVENT), event.pt_index,
          (unsigned long long)event.match_bits, event.initiator.phys.nid,
          event.initiator.phys.pid, event.pt_index, event.ni_fail_type,
          PtlToStr(event.ni_fail_type, PTL_STR_FAIL_TYPE), event.fc_err);
      if (event.type >= sizeof(handler) / sizeof(handler[0])) {
        PTL_FATAL("Cannot handle event of type: %d", event.type);
      }
      if (event.ni_fail_type != PTL_NI_OK) {
        PTL_WARN(
        "[%s:%s:%d] PTL: ni_fail %d(%s) on event %d(%s) from {nid:%d,pid:%d}, "
        "delivering errored completion",
        __FILE__, __func__, __LINE__,
        event.ni_fail_type, PtlToStr(event.ni_fail_type, PTL_STR_FAIL_TYPE),
        event.type, PtlToStr(event.type, PTL_STR_EVENT),
        event.initiator.phys.nid, event.initiator.phys.pid);
}

      handler[event.type](event, ptl_cq);
      continue;
    } else if (rc == PTL_EQ_EMPTY) {
      break;
    } else {
      PTL_FATAL("PtlEQGet unhandled code %d", rc);
      break;
    }
  }
  spin_unlock(&ptl_cq->drain_lock);
}

void ptl_eq_callback(void *arg, ptl_handle_eq_t eqh) {     /* interrupt path */
  struct ptl_cq *ptl_cq = arg;
  (void)eqh;                                               //new addition (drain uses ptl_cq->eq)
  if (PTL_CQ != ptl_cq->object_type) {
    PTL_FATAL("Corrupted PTL_CQ");
    return;
  }
  ptl_eq_drain(ptl_cq);
}

#ifdef PTL_EQ_POLL
/* new addition: timer fallback for the shared-interrupt/coalescing problem.
 * A lone admin completion (keep-alive / reconnect Connect) can sit undrained at
 * idle; poll every EQ so admin always makes progress even with no IO traffic. */
static void ptl_cq_poll_work(struct work_struct *w) {
  struct ptl_cq *ptl_cq =
      container_of(to_delayed_work(w), struct ptl_cq, poll_work);
  ptl_eq_drain(ptl_cq);
  schedule_delayed_work(&ptl_cq->poll_work,
                        msecs_to_jiffies(PTL_CQ_POLL_MS));
}
#endif /* PTL_EQ_POLL */

struct ptl_cq *ptl_cq_create(struct ptl_cq_pool *cq_pool,
                             struct ptl_bxiv3_device *bxiv3_dev, int nr_cqes,
                             ptl_pt_index_t pte,
                             enum ib_poll_context poll_ctx) {
  struct ptl_cq *cq;
  int rc;
  if (poll_ctx != IB_POLL_SOFTIRQ) {
    PTL_FATAL("Sorry! Currently BXIv3 NVMe-oF initiator supports only "
              "IB_POLL_SOFTIRQ mode");
    return ERR_PTR(-EINVAL);
  }

  PTL_DEBUG("%s", cq_pool ? "CP POOL SET OK!"
                          : "NULL, this PTL_CQ does not belong to a pool");

  cq = kzalloc(sizeof(*cq), GFP_KERNEL);
  if (!cq) {
    PTL_FATAL("Out of memory");
    return ERR_PTR(-ENOMEM);
  }

  cq->object_type = PTL_CQ;
  cq->cq_pool = cq_pool;
  cq->bxiv3_dev = bxiv3_dev;
  cq->ptl_cq_id = pte;
  cq->nr_cqes = nr_cqes;

  /* Must precede PtlEQAllocAsync: that call arms ptl_eq_callback with this cq,
   * so an event can reach ptl_eq_drain() -> spin_trylock(&cq->drain_lock)
   * before the tail of this function is reached. Initialising the lock down
   * there also re-inits it under a drain already in flight. */
  spin_lock_init(&cq->drain_lock);
#ifdef PTL_EQ_POLL
  INIT_DELAYED_WORK(&cq->poll_work, ptl_cq_poll_work);
#endif /* PTL_EQ_POLL */

  rc = PtlEQAllocAsync(bxiv3_dev->nicia_handle, cq->nr_cqes, &cq->eq,
                       ptl_eq_callback, cq, cq->bxiv3_dev->intr_index);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to allocate event queue for pte: %d", pte);
    goto err;
  }
  rc = PtlPTAlloc(bxiv3_dev->nicia_handle, 0, cq->eq, pte, &cq->pte);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to allocate PTE: %u with reason: %d", pte, rc);
    goto free_cq;
  }
  rc = PtlPTEnable(bxiv3_dev->nicia_handle, cq->pte);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to enable PTE: %u with reason: %d", pte, rc);
    goto free_cq;
  }
  PTL_DEBUG("Enabled for iface_id: %d PTE: %u and created cq with %d number of "
            "entries",
            bxiv3_dev->iface_id, cq->pte, nr_cqes);
#ifdef PTL_EQ_POLL
  /* new addition: arm the poll fallback so admin completions drain even when
   * the shared NIC interrupt does not fire at idle. Armed last, once the CQ is
   * fully built; the error paths below never queued it, so they need no cancel. */
  schedule_delayed_work(&cq->poll_work,
                        msecs_to_jiffies(PTL_CQ_POLL_MS));
#endif /* PTL_EQ_POLL */
  return cq;
free_cq:
  rc = PtlEQFree(cq->eq);
  if (PTL_OK != rc)
    PTL_WARN("Failed to free event queue with code: %d, continuing", rc);
err:
  kfree(cq);
  return ERR_PTR(-EINVAL);
}

ptl_pt_index_t ptl_cq_destroy(struct ptl_cq *ptl_cq) {
  ptl_pt_index_t pte;
  int rc;

  if (!ptl_cq) {
    PTL_WARN("Attempting to destroy NULL cq");
    return -1;
  }
  pte = ptl_cq->pte;

  /* new addition: stop the poll fallback before tearing down the EQ/PTE, so no
   * poll runs against a freed event queue. sync = wait for any in-flight poll. */
#ifdef PTL_EQ_POLL
  cancel_delayed_work_sync(&ptl_cq->poll_work);   //new addition
#endif /* PTL_EQ_POLL */

  /* Free the portal table entry */
  rc = PtlPTFree(ptl_cq->bxiv3_dev->nicia_handle, ptl_cq->pte);
  if (PTL_OK != rc) {
    PTL_WARN("Failed to free PTE: %d with code: %d, continuing", ptl_cq->pte,
             rc);
  }

  /* Free the event queue */
  rc = PtlEQFree(ptl_cq->eq);
  if (PTL_OK != rc) {
    PTL_WARN("Failed to free event queue with code: %d, continuing", rc);
  }
  PTL_DEBUG("Destroyed cq for PTE: %d", ptl_cq->pte);
  /* Free the cq structure */
  kfree(ptl_cq);
  return pte;
}
