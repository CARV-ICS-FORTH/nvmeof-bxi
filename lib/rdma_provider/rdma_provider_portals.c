/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2020 Intel Corporation. All rights reserved.
 *   Copyright (c) Mellanox Technologies LTD. All rights reserved.
 *   Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#include "ptl_print_nvme_commands.h"
#include "portals4.h"
#include "ptl_cm_id.h"
#include "ptl_config.h"
#include "ptl_context.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include "ptl_qp.h"
#include "ptl_srq.h"
#include "ptl_uuid.h"
#include "spdk/likely.h"
#include "spdk/log.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/util.h"
#include "spdk_internal/rdma_provider.h"
#include "spdk_internal/rdma_utils.h"
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
//from common.c staff
#define SPDK_PTL_PROVIDER_SRQ_MAGIC_NUMBER 27081983UL
#define SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER 19082018UL
#define SPDK_PTL_CHECK_SRQ(X) \
    if ((X)->magic_number != SPDK_PTL_PROVIDER_SRQ_MAGIC_NUMBER) { \
        SPDK_PTL_FATAL("Corrupted PORTALS SRQ"); \
    }

#define SPDK_PTL_CHECK_SGE_LENGTH(wr) \
	do { \
		if ((wr)->num_sge != 1) { \
			for (int i = 0; i < (wr)->num_sge; i++) { \
				SPDK_PTL_DEBUG("Opcode: wr[%d] = %d, addr: %lu length is: %d", \
					       i, (wr)->opcode, (wr)->sg_list[i].addr, \
					       (wr)->sg_list[i].length); \
			} \
			SPDK_PTL_FATAL("Num sges > 1 are under development, sorry requested are: %d", \
				       (wr)->num_sge); \
		} \
	} while (0)

struct spdk_portals_provider_srq {
	uint64_t magic_number;
	struct spdk_rdma_provider_srq fake_srq;
	struct ptl_context *ptl_context;
};

struct spdk_portals_provider_qp {
	uint64_t magic_number;
	struct ptl_context *ptl_context;
	struct ptl_cm_id *ptl_id;
	struct spdk_rdma_provider_qp fake_spdk_rdma_qp;
};

struct spdk_rdma_provider_srq *
spdk_rdma_provider_srq_create(struct spdk_rdma_provider_srq_init_attr *init_attr)
{
	assert(init_attr);
	assert(init_attr->pd);
	struct ptl_context * ptl_context;
	struct spdk_portals_provider_srq *portals_srq;
	struct spdk_rdma_provider_srq * fake_rdma_srq;

	ptl_context = ptl_cnxt_get_from_ibvpd(init_attr->pd);
	SPDK_PTL_DEBUG("Ok got portals context from ibv_pd!");
	portals_srq = calloc(1UL, sizeof(*portals_srq));
	if (!portals_srq) {
		SPDK_PTL_FATAL("Can't allocate memory for SRQ handle\n");
	}
	portals_srq->magic_number = SPDK_PTL_PROVIDER_SRQ_MAGIC_NUMBER;
	portals_srq->ptl_context = ptl_context;
	fake_rdma_srq = &portals_srq->fake_srq;

	if (init_attr->stats) {
		fake_rdma_srq->stats = init_attr->stats;
		fake_rdma_srq->shared_stats = true;
	} else {
		fake_rdma_srq->stats = calloc(1UL, sizeof(*fake_rdma_srq->stats));
		if (!fake_rdma_srq->stats) {
			SPDK_PTL_FATAL("SRQ statistics memory allocation failed");
			free(portals_srq);
			return NULL;
		}
	}

	struct ptl_pd *ptl_pd = ptl_pd_get_from_ibv_pd(init_attr->pd);
	struct ptl_srq * ptl_srq = ptl_create_srq(ptl_pd, &init_attr->srq_init_attr);
	fake_rdma_srq->srq = &ptl_srq->fake_srq;

	// if (!rdma_srq->srq) {
	// 	if (!init_attr->stats) {
	// 		free(rdma_srq->stats);
	// 	}
	// 	SPDK_ERRLOG("Unable to create SRQ, errno %d (%s)\n", errno, spdk_strerror(errno));
	// 	free(rdma_srq);
	// 	return NULL;
	// }
	SPDK_PTL_DEBUG("Ok emulated the RDMA_SRQ creation with PORTALS!");
	return fake_rdma_srq;
}

int
spdk_rdma_provider_srq_destroy(struct spdk_rdma_provider_srq *rdma_srq)
{
	if (!rdma_srq) {
		return 0;
	}

	assert(rdma_srq->srq);
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	free(rdma_srq);
	return -1;
}

static inline bool
rdma_queue_recv_wrs(struct spdk_rdma_provider_recv_wr_list *recv_wrs, struct ibv_recv_wr *first,
		    struct spdk_rdma_provider_wr_stats *recv_stats)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return false;
}

bool spdk_rdma_provider_srq_queue_recv_wrs(
	struct spdk_rdma_provider_srq *rdma_srq, struct ibv_recv_wr *first)
{
	// struct spdk_rdma_provider_srq *spdk_ptl_srq = SPDK_CONTAINEROF(rdma_srq, spdk_rdma_provider_srq, fake_srq);
	struct ibv_recv_wr *last;
	struct spdk_rdma_provider_wr_stats *recv_stats;
	struct spdk_rdma_provider_recv_wr_list *recv_wrs;
	bool ret;
	if (NULL == first) {
		SPDK_PTL_FATAL("First is NULL. XXX TODO XXX Nothing to do?");
		return false;
	}
	assert(rdma_srq->stats);
	recv_stats = rdma_srq->stats;
	recv_stats->num_submitted_wrs++;
	last = first;
	while (last->next != NULL) {
		last = last->next;
		recv_stats->num_submitted_wrs++;
	}

	recv_wrs = &rdma_srq->recv_wrs;

	if (recv_wrs->first == NULL) {
		recv_wrs->first = first;
		recv_wrs->last = last;
		ret = true;
	} else {
		recv_wrs->last->next = first;
		recv_wrs->last = last;
		ret = false;
	}
	// SPDK_PTL_DEBUG("Done exact the same steps as in IBV case total "
	// 	       "submitted wrs: %lu current: %lu",
	// 	       recv_stats->num_submitted_wrs,
	// 	       recv_stats->num_submitted_wrs - diff);
	return ret;
}


/**
 * @brief Cleans up the ibv_recv_wr descriptors. In the vanilla implementation hardware handles the cleanup process. However,
 * since SPDK_PTL translates the ibv_recv_wr into PTLMEAppend entries, SPDK_PTL needs to do the cleanup.
 */
// static bool spdk_ptl_clean_up_recv_wr_desc(struct ibv_recv_wr *recv_wrs) {
//   return true;
// }

int
spdk_rdma_provider_srq_flush_recv_wrs(struct spdk_rdma_provider_srq *rdma_srq,
				      struct ibv_recv_wr **bad_wr)
{
	ptl_handle_ni_t nic;
	ptl_me_t me;
	ptl_handle_me_t me_handle;
	struct ptl_context_recv_op *recv_op;
	int ret;

	if (spdk_unlikely(rdma_srq->recv_wrs.first == NULL)) {
		return 0;
	}
	//Now it's time to append the entries in portals
	for (struct ibv_recv_wr *wr = rdma_srq->recv_wrs.first; wr != NULL;
	     wr = wr->next) {

		struct spdk_portals_provider_srq *portals_srq =
			SPDK_CONTAINEROF(rdma_srq, struct spdk_portals_provider_srq, fake_srq);
		SPDK_PTL_CHECK_SRQ(portals_srq);
		nic = ptl_cnxt_get_ni_handle(portals_srq->ptl_context);

		if (wr->num_sge != PTL_IOVEC_SIZE) {
			SPDK_PTL_FATAL("IOVECTOR too small size is: %d needs %d", PTL_IOVEC_SIZE, wr->num_sge);
		}
		// SPDK_PTL_DEBUG("Num of sges are %d", wr->num_sge);
		recv_op = calloc(1UL, sizeof(*recv_op));
		recv_op->obj_type = PTL_RECV_OP;
		recv_op->cq_id = PTL_UUID_TARGET_COMPLETION_QUEUE_ID;
		for (int i = 0; i < wr->num_sge; i++) {
			recv_op->io_vector[i].iov_base = (ptl_addr_t)wr->sg_list[i].addr;
			recv_op->io_vector[i].iov_len = wr->sg_list[i].length;
			SPDK_PTL_DEBUG("iovector[%d] = : Address = %p, Length = %lu\n",
				       i, recv_op->io_vector[i].iov_base, recv_op->io_vector[i].iov_len);
		}

		/*Initialize the matching entries*/
		memset(&me, 0, sizeof(me));
		me.ignore_bits = PTL_UUID_IGNORE_MASK;
		// me.match_bits = ptl_uuid_set_op_type(PTL_UUID_IGNORE_MASK, PTL_SEND_RECV);
		me.match_bits = PTL_UUID_TARGET_SRQ_MATCH_BITS;
		me.match_id.phys.nid = PTL_NID_ANY;
		me.match_id.phys.pid = PTL_PID_ANY;
		me.min_free = 0;
		me.start = recv_op->io_vector;
		me.length = wr->num_sge;
		me.ct_handle = PTL_CT_NONE;
		me.uid = PTL_UID_ANY;
		me.options = PTL_SRV_ME_OPTS | PTL_IOVEC;
		recv_op->wr_id = wr->wr_id;
		// Append the memory entry
		ret = PtlMEAppend(
			      nic,                    // Network interface handle
			      ptl_cnxt_get_portal_index(portals_srq->ptl_context), //Portals table index
			      &me,                    // List entry
			      PTL_PRIORITY_LIST,      // List type (PRIORITY or OVERFLOW)
			      recv_op,               // User pointer, stores recv op meta (wr_id)
			      &me_handle              // Returned handle
		      );
		if (PTL_OK != ret) {
			SPDK_PTL_FATAL("Failed to append memory entry start addr: %p length %lu code is: %d", me.start,
				       me.length, ret);
		}

	}
	// rc = ibv_post_srq_recv(rdma_srq->srq, rdma_srq->recv_wrs.first, bad_wr);
	rdma_srq->recv_wrs.first = NULL;
	rdma_srq->stats->doorbell_updates++;
	return 0;
}

bool spdk_rdma_provider_qp_queue_recv_wrs(
	struct spdk_rdma_provider_qp *spdk_rdma_qp, struct ibv_recv_wr *first)
{

	assert(spdk_rdma_qp);
	assert(first);
	struct spdk_rdma_provider_wr_stats *recv_stats =
			&spdk_rdma_qp->stats->recv;
	struct spdk_rdma_provider_recv_wr_list *recv_wrs =
			&spdk_rdma_qp->recv_wrs;
	struct ibv_recv_wr *last;

	recv_stats->num_submitted_wrs++;
	last = first;
	while (last->next != NULL) {
		last = last->next;
		recv_stats->num_submitted_wrs++;
	}

	if (recv_wrs->first == NULL) {
		recv_wrs->first = first;
		recv_wrs->last = last;
		return true;
	} else {
		recv_wrs->last->next = first;
		recv_wrs->last = last;
		return false;
	}
}

int
spdk_rdma_provider_qp_flush_recv_wrs(struct spdk_rdma_provider_qp *spdk_rdma_qp,
				     struct ibv_recv_wr **bad_wr)
{
	// SPDK_PTL_DEBUG("PORTALS REGISTERING THE BUFFERS FOR THE *SINGLE* QUEUE PAIR CASE (NOT SRQ)");
	struct spdk_portals_provider_qp *portals_qp;
	ptl_handle_ni_t nic;
	ptl_pt_index_t pt_index;
	ptl_me_t me;
	ptl_handle_me_t me_handle;
	struct ptl_context_recv_op *recv_op;
	int ret;


	/* gesalous start */
	portals_qp = SPDK_CONTAINEROF(spdk_rdma_qp, struct spdk_portals_provider_qp, fake_spdk_rdma_qp);
	if (SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER != portals_qp->magic_number) {
		SPDK_PTL_FATAL("Corrupted Portals QP!");
	}

	if (spdk_unlikely(spdk_rdma_qp->recv_wrs.first == NULL)) {
		// SPDK_PTL_DEBUG("Nothing to register for receive?");
		// raise(SIGINT);
		return 0;
	}
	//Now it's time to append the entries in portals
	for (struct ibv_recv_wr *wr = spdk_rdma_qp->recv_wrs.first; wr != NULL;
	     wr = wr->next) {
		nic = ptl_cnxt_get_ni_handle(portals_qp->ptl_context);
		pt_index = ptl_cnxt_get_portal_index(portals_qp->ptl_context);

		if (wr->num_sge > PTL_IOVEC_SIZE) {
			SPDK_PTL_FATAL("io_vector too small size: %d needs %d", PTL_IOVEC_SIZE, wr->num_sge);
		}

		recv_op = calloc(1UL, sizeof(*recv_op));
		recv_op->obj_type = PTL_RECV_OP;
		recv_op->cq_id = portals_qp->ptl_id->ptl_qp->recv_cq->cq_id;
		for (int i = 0; i < wr->num_sge; i++) {
			recv_op->io_vector[i].iov_base = (ptl_addr_t) wr->sg_list[i].addr;
			recv_op->io_vector[i].iov_len = wr->sg_list[i].length;
			SPDK_PTL_DEBUG("SGE no: %d out of: %d: Address = %p, Length = %lu\n",
				       i, wr->num_sge, recv_op->io_vector[i].iov_base, recv_op->io_vector[i].iov_len);
		}
		// Setup the list entry
		// Initialize the matching entry
		memset(&me, 0, sizeof(me));
		me.ignore_bits = PTL_UUID_IGNORE_MASK;
		// me.match_bits = ptl_uuid_set_op_type(PTL_UUID_IGNORE_MASK, PTL_SEND_RECV);
		me.match_bits = portals_qp->ptl_id->my_match_bits;
		me.match_id.phys.nid = PTL_NID_ANY;
		me.match_id.phys.pid = PTL_PID_ANY;
		me.min_free = 0;
		me.start = recv_op->io_vector;
		me.length = wr->num_sge;
		me.ct_handle = PTL_CT_NONE;
		me.uid = PTL_UID_ANY;
		me.options = PTL_SRV_ME_OPTS | PTL_IOVEC;
		recv_op->wr_id = wr->wr_id;

		// Append the memory entry
		ret = PtlMEAppend(
			      nic,                    // Network interface handle
			      pt_index,               // Portal table index
			      &me,                    // List entry
			      PTL_PRIORITY_LIST,      // List type (PRIORITY or OVERFLOW)
			      recv_op,               // User pointer (can be used to store wr_id)
			      &me_handle              // Returned handle
		      );
		if (PTL_OK != ret) {
			SPDK_PTL_FATAL("Failed to append memory entry");
		}

	}

	/* gesalous end */
	// SPDK_PTL_DEBUG("MATCH_BITS: Registered memory for a single QP under match bits: %lu",
	// 	       portals_qp->ptl_id->my_match_bits);
	spdk_rdma_qp->recv_wrs.first = NULL;
	spdk_rdma_qp->stats->recv.doorbell_updates++;

	return 0;
}
// common end

struct spdk_rdma_provider_qp *
spdk_rdma_provider_qp_create(struct rdma_cm_id *cm_id,
			     struct spdk_rdma_provider_qp_init_attr *qp_attr)
{
	SPDK_PTL_DEBUG("DOING BASICALLY THE SAME STAFF as the original");
	struct spdk_portals_provider_qp *spdk_portals_qp;
	struct spdk_rdma_provider_qp *spdk_rdma_qp;
	int rc;
	struct ibv_qp_init_attr attr = {.qp_context = qp_attr->qp_context,
						.send_cq = qp_attr->send_cq,
						.recv_cq = qp_attr->recv_cq,
						.srq = qp_attr->srq,
						.cap = qp_attr->cap,
						.qp_type = IBV_QPT_RC
	};

	if (qp_attr->domain_transfer) {
		SPDK_PTL_FATAL(
			"PORTALS provider doesn't support memory domain transfer functionality");
		return NULL;
	}

	spdk_portals_qp = calloc(1UL, sizeof(*spdk_portals_qp));
	if (!spdk_portals_qp) {
		SPDK_ERRLOG("qp memory allocation failed");
	}

	spdk_portals_qp->ptl_context = ptl_cnxt_get();
	spdk_portals_qp->ptl_id = ptl_cm_id_get(cm_id);
	spdk_portals_qp->magic_number =  SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER;

	spdk_rdma_qp = &spdk_portals_qp->fake_spdk_rdma_qp;


	if (qp_attr->stats) {
		spdk_rdma_qp->stats = qp_attr->stats;
		spdk_rdma_qp->shared_stats = true;
	} else {
		spdk_rdma_qp->stats = calloc(1UL, sizeof(*spdk_rdma_qp->stats));
		if (!spdk_rdma_qp->stats) {
			SPDK_ERRLOG("qp statistics memory allocation failed\n");
			free(spdk_portals_qp);
			return NULL;
		}
	}

	rc = rdma_create_qp(cm_id, qp_attr->pd, &attr);
	if (rc) {
		SPDK_ERRLOG("Failed to create qp, rc %d, errno %s (%d)\n", rc,
			    spdk_strerror(errno), errno);
		free(spdk_portals_qp);
		return NULL;
	}
	spdk_rdma_qp->qp = cm_id->qp;
	spdk_rdma_qp->cm_id = cm_id;
	spdk_rdma_qp->domain = spdk_rdma_utils_get_memory_domain(qp_attr->pd);
	if (!spdk_rdma_qp->domain) {
		spdk_rdma_provider_qp_destroy(spdk_rdma_qp);
		return NULL;
	}

	qp_attr->cap = attr.cap;
	return spdk_rdma_qp;
}

int
spdk_rdma_provider_qp_accept(struct spdk_rdma_provider_qp *spdk_rdma_qp,
			     struct rdma_conn_param *conn_param)
{
	assert(spdk_rdma_qp != NULL);
	assert(spdk_rdma_qp->cm_id != NULL);
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(spdk_rdma_qp->cm_id);
	SPDK_PTL_DEBUG("CONN_PARAM: At accept got a valid ptl_id queue pair id: %d conn_param len: %u",
		       ptl_id->fake_cm_id.qp->qp_num, conn_param->private_data_len);

	SPDK_ERRLOG("GESALOUSTRA: CONN_PARAM sending to the target the following shit\n");
	SPDK_ERRLOG("GESALOUSTRA: CONN_PARAM param.srq = %u param.qp_num = %u "
		    "param.rnr_retry_count = %u param.responder_resources: %u "
		    "param.initiator_depth: %u param.flow_control: %u "
		    "param.private_data_len: %u\n",
		    conn_param->srq, conn_param->qp_num, conn_param->rnr_retry_count,
		    conn_param->responder_resources, conn_param->initiator_depth,
		    conn_param->flow_control, conn_param->private_data_len);
	return rdma_accept(spdk_rdma_qp->cm_id, conn_param);
}

int spdk_rdma_provider_qp_complete_connect(
	struct spdk_rdma_provider_qp *spdk_rdma_qp)
{
	// struct rdma_cm_event *fake_event;
	// struct ptl_cm_id *ptl_id = ptl_cm_id_get(spdk_rdma_qp->cm_id);
	/* Nothing to be done for Portals */
	SPDK_PTL_DEBUG("complete connect treat is a no-op");
	// fake_event = ptl_cm_id_create_event(ptl_id,
	// 				    NULL,
	// 				    RDMA_CM_EVENT_ESTABLISHED);
	// ptl_cm_id_add_event(ptl_id, fake_event);

	return 0;
}

void
spdk_rdma_provider_qp_destroy(struct spdk_rdma_provider_qp *spdk_rdma_qp)
{
	assert(spdk_rdma_qp != NULL);

	struct spdk_portals_provider_qp *portals_qp = SPDK_CONTAINEROF(spdk_rdma_qp,
		struct spdk_portals_provider_qp, fake_spdk_rdma_qp);
	if (portals_qp->magic_number != SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER) {
		SPDK_PTL_FATAL("Corrupted portals_qp");
	}
	SPDK_PTL_DEBUG("CAUTION, doing the same as the original");
	free(portals_qp);
}

int
spdk_rdma_provider_qp_disconnect(struct spdk_rdma_provider_qp *spdk_rdma_qp)
{
	int rc = 0;

	assert(spdk_rdma_qp != NULL);
	SPDK_PTL_DEBUG("Calling disconnect for the queue pair...");
	spdk_rdma_provider_qp_flush_send_wrs(spdk_rdma_qp, NULL);

	if (spdk_rdma_qp->cm_id) {
		rc = rdma_disconnect(spdk_rdma_qp->cm_id);
		if (rc) {
			if (errno == EINVAL && spdk_rdma_qp->qp->context->device->transport_type == IBV_TRANSPORT_IWARP) {
				/* rdma_disconnect may return an error and set errno to EINVAL in case of iWARP.
				 * This behaviour is expected since iWARP handles disconnect event other than IB and
				 * qpair is already in error state when we call rdma_disconnect */
				return 0;
			}
			SPDK_PTL_FATAL("rdma_disconnect failed, errno %s (%d)\n", spdk_strerror(errno), errno);
		}
	}

	return rc;
}

bool
spdk_rdma_provider_qp_queue_send_wrs(struct spdk_rdma_provider_qp *spdk_rdma_qp,
				     struct ibv_send_wr *first)
{

	struct ibv_send_wr *last;

	assert(spdk_rdma_qp);
	assert(first);

	if (first == NULL || spdk_rdma_qp == NULL) {
		SPDK_PTL_FATAL("NULL args");
	}
	spdk_rdma_qp->stats->send.num_submitted_wrs++;
	last = first;
	while (last->next != NULL) {
		last = last->next;
		spdk_rdma_qp->stats->send.num_submitted_wrs++;
	}

	// SPDK_PTL_DEBUG("NVMe: Enqueueing SEND WRS request as in the VANILLA CASE for "
	//                "Portals num of enqueued requests: %lu",
	//                spdk_rdma_qp->stats->send.num_submitted_wrs);

	if (spdk_rdma_qp->send_wrs.first == NULL) {
		spdk_rdma_qp->send_wrs.first = first;
		spdk_rdma_qp->send_wrs.last = last;
		return true;
	} else {
		spdk_rdma_qp->send_wrs.last->next = first;
		spdk_rdma_qp->send_wrs.last = last;
		return false;
	}
}



static void spdk_rdma_print_wr_flags(struct ibv_send_wr *wr)
{
	SPDK_PTL_DEBUG("Work Request Details:");
	SPDK_PTL_DEBUG("  Opcode: %s",
		       wr->opcode == IBV_WR_RDMA_WRITE ? "RDMA_WRITE" :
		       wr->opcode == IBV_WR_RDMA_READ ? "RDMA_READ" :
		       wr->opcode == IBV_WR_SEND ? "SEND" :
		       wr->opcode == IBV_WR_SEND_WITH_INV ? "SEND_WITH_INV" :
		       "UNKNOWN");

	if (wr->send_flags == 0) {
		SPDK_PTL_DEBUG("  Flags: NONE");
	} else {
		char flags[256] = "";
		if (wr->send_flags & IBV_SEND_SIGNALED) { strcat(flags, "IBV_SEND_SIGNALED "); }
		if (wr->send_flags & IBV_SEND_FENCE) { strcat(flags, "IBV_SEND_FENCE "); }
		if (wr->send_flags & IBV_SEND_INLINE) { strcat(flags, "IBV_SEND_INLINE "); }
		if (wr->send_flags & IBV_SEND_SOLICITED) { strcat(flags, "IBV_SEND_SOLICITED "); }
		SPDK_PTL_DEBUG("  Flags: %s", flags);
	}

	SPDK_PTL_DEBUG("  Number of SGE: %d", wr->num_sge);

	if (wr->opcode == IBV_WR_RDMA_READ || wr->opcode == IBV_WR_RDMA_WRITE) {
		SPDK_PTL_DEBUG("  RDMA Info:");
		SPDK_PTL_DEBUG("    Remote Addr: 0x%lx", wr->wr.rdma.remote_addr);
		SPDK_PTL_DEBUG("    Remote Key (rkey): 0x%x", wr->wr.rdma.rkey);
	}
	SPDK_PTL_DEBUG("  Next WR: %p", wr->next);
}

static void spdk_rdma_provider_ptl_rdma_read(struct ptl_pd *ptl_pd, struct ptl_qp *ptl_qp,
		struct ibv_send_wr *wr, uint64_t match_bits)
{
	ptl_process_t destination = {.phys.nid = ptl_qp->remote_nid, .phys.pid = ptl_qp->remote_pid};
	size_t local_offset;
	int rc;
	struct ptl_context_send_op *rdma_read_op;

	SPDK_PTL_CHECK_SGE_LENGTH(wr);

	struct ptl_pd_mem_desc * ptl_pd_mem_desc = ptl_pd_get_mem_desc(ptl_pd, wr->sg_list[0].addr,
		wr->sg_list[0].length, true,
		false);

	if (NULL == ptl_pd_mem_desc) {
		SPDK_PTL_FATAL("Failed to find descriptor");
	}


	rdma_read_op = NULL;
	if (wr->send_flags & IBV_SEND_SIGNALED) {
		rdma_read_op = calloc(1UL, sizeof(*rdma_read_op));
		rdma_read_op->obj_type = PTL_SEND_OP;
		rdma_read_op->wr_id = wr->wr_id;
		rdma_read_op->qp_num = ptl_qp->ptl_cm_id->ptl_qp_num;
	}

	local_offset = wr->sg_list[0].addr - (uint64_t)ptl_pd_mem_desc->local_w_mem_desc.start;
	SPDK_PTL_DEBUG("NVMe: Performing an RDMA read from node nid: %d pid: %d portal index: %d local offset: %lu match_bits: %lu is it signaled?: %s qp_num: %d",
		       destination.phys.nid, destination.phys.pid, PTL_PT_INDEX, local_offset, match_bits,
		       rdma_read_op ? "YES" : "NO", ptl_qp->ptl_cm_id->ptl_qp_num);
	/*XXX TODO XXX, set match bits correct here!XXX TODO XXX*/
	rc = PtlGet(ptl_pd_mem_desc->local_w_mem_handle, local_offset, wr->sg_list[0].length, destination,
		    PTL_PT_INDEX, match_bits, wr->wr.rdma.remote_addr, rdma_read_op);
	if (PTL_OK != rc) {
		SPDK_PTL_FATAL("Remote RDMA read failed Sorry!");
	}
}


static void spdk_rdma_provider_ptl_rdma_write(struct ptl_pd *ptl_pd, struct ptl_qp *ptl_qp,
		struct ibv_send_wr *wr, uint64_t match_bits)
{
	struct ptl_pd_mem_desc *ptl_pd_mem_desc;
	size_t local_offset;
	ptl_process_t destination = {.phys.nid = ptl_qp->remote_nid, .phys.pid = ptl_qp->remote_pid};
	struct ptl_context_send_op *send_op;
	int rc;
	uint64_t remote_addr =  wr->wr.rdma.remote_addr;

	for (int i = 0; i < wr->num_sge; i++) {
		ptl_pd_mem_desc =
			ptl_pd_get_mem_desc(ptl_pd, wr->sg_list[i].addr,
					    wr->sg_list[i].length, true, false);
		if (NULL == ptl_pd_mem_desc) {
			SPDK_PTL_FATAL("Failed to find descriptor");
		}
		local_offset = wr->sg_list[i].addr - (uint64_t)ptl_pd_mem_desc->local_w_mem_desc.start;
		SPDK_PTL_DEBUG("Performing an RDMA WRITE (sg[%d]) from node "
			       "nid: %d pid: %d portal index: %d local offset: "
			       "%lu length in B: %u is it signaled?: %s",
			       i, destination.phys.nid, destination.phys.pid,
			       PTL_PT_INDEX, local_offset,
			       wr->sg_list[i].length, send_op ? "YES" : "NO");

		send_op = NULL;
		if (wr->send_flags & IBV_SEND_SIGNALED && i == (wr->num_sge - 1)) {
			send_op = calloc(1UL, sizeof(*send_op));
			send_op->obj_type = PTL_SEND_OP;
			send_op->wr_id = wr->wr_id;
			send_op->qp_num = ptl_qp->ptl_cm_id->ptl_qp_num;
			send_op->cq_id = ptl_qp->send_cq->cq_id;
		}

		rc = PtlPut(ptl_pd_mem_desc->local_w_mem_handle,
			    local_offset,           // local offset
			    wr->sg_list[i].length,         // length
			    PTL_ACK_REQ,
			    destination,       // target process
			    PTL_PT_INDEX,        // portal table index
			    match_bits,// match bits
			    remote_addr,
			    send_op,
			    0);// priority

		if (PTL_OK != rc) {
			SPDK_PTL_FATAL("Remote RDMA write failed Sorry!");
		}
		remote_addr += wr->sg_list[i].length;
	}
}


int
spdk_rdma_provider_qp_flush_send_wrs(struct spdk_rdma_provider_qp *spdk_rdma_qp,
				     struct ibv_send_wr **bad_wr)
{

	assert(spdk_rdma_qp);
	// assert(bad_wr);
	int rc;
	uint64_t match_bits;
	struct ptl_qp *ptl_qp = ptl_qp_get_from_ibv_qp(spdk_rdma_qp->qp);
	struct ptl_pd *ptl_pd = ptl_qp_get_pd(ptl_qp);
	struct ptl_pd_mem_desc * ptl_mem_desc;
	ptl_process_t target = {.phys.nid = ptl_qp->remote_nid, .phys.pid = ptl_qp->remote_pid};
	struct ptl_context_send_op *send_op;
	uint64_t local_offset;


	if (spdk_unlikely(NULL == spdk_rdma_qp->send_wrs.first)) {
		// SPDK_PTL_DEBUG("Nothing to SEND");
		return 0;
	}

	match_bits = ptl_qp->ptl_cm_id->uuid;

	for (struct ibv_send_wr *wr = spdk_rdma_qp->send_wrs.first; wr != NULL; wr = wr->next) {

		// spdk_rdma_print_wr_flags(wr);
		if (wr->opcode == IBV_WR_RDMA_WRITE) {
			spdk_rdma_provider_ptl_rdma_write(ptl_pd, ptl_qp, wr, ptl_uuid_set_match_list(match_bits,
							  ptl_qp->ptl_cm_id->rma_match_bits));
			continue;
		}

		if (wr->opcode == IBV_WR_RDMA_READ) {
			spdk_rdma_provider_ptl_rdma_read(ptl_pd, ptl_qp, wr, ptl_uuid_set_match_list(match_bits,
							 ptl_qp->ptl_cm_id->rma_match_bits));
			continue;
		}

		SPDK_PTL_CHECK_SGE_LENGTH(wr);

		for (int i = 0; i < wr->num_sge; i++) {
			ptl_mem_desc = ptl_pd_get_mem_desc(ptl_pd, wr->sg_list[i].addr, wr->sg_list[i].length, true, false);

			if (NULL == ptl_mem_desc) {
				SPDK_PTL_FATAL("MEM desc not found!");
			}

			if (wr->sg_list[0].length == 64) {
				ptl_print_nvme_cmd((const struct spdk_nvme_cmd *)wr->sg_list[i].addr, "NVMe-cmd-send");
			}
			if (wr->sg_list[0].length == 16) {
				ptl_print_nvme_cpl((const struct spdk_nvme_cpl *)wr->sg_list[i].addr, "NVMe-cpl-send");
			}

			local_offset = wr->sg_list[i].addr - (uint64_t)ptl_mem_desc->local_w_mem_desc.start;


			send_op = NULL;
			if (wr->send_flags & IBV_SEND_SIGNALED) {
				send_op = calloc(1UL, sizeof(*send_op));
				send_op->obj_type = PTL_SEND_OP;
				send_op->wr_id = wr->wr_id;
				send_op->qp_num = ptl_qp->ptl_cm_id->ptl_qp_num;
				send_op->cq_id = ptl_qp->send_cq->cq_id;
			}

			SPDK_PTL_DEBUG(
				"%s: Performing a SEND (PtlPut) operation to nid: "
				"%d pid: %d pt_index: %d initiator qp_num: %d "
				"target qp_num: %d local_offset: %lu is it "
				"signaled?: %s",
				wr->sg_list[i].length == 16 ? "NVMe-cpl-send"
				: "NVMe-cmd-send",
				target.phys.nid, target.phys.pid,
				ptl_qp->remote_pt_index,
				ptl_uuid_get_initiator_qp_num(match_bits),
				ptl_uuid_get_target_qp_num(match_bits),
				local_offset, send_op ? "YES" : "NO");

			rc = PtlPut(ptl_mem_desc->local_w_mem_handle,
				    local_offset,//local offset
				    wr->sg_list[i].length,//length
				    PTL_ACK_REQ,
				    target,// target process
				    ptl_qp->remote_pt_index,//portal table index
				    ptl_uuid_set_match_list(match_bits, ptl_qp->ptl_cm_id->recv_match_bits),//match bits
				    0,// remote offset, don't care let target decide
				    send_op,
				    0);

			if (rc != PTL_OK) {
				SPDK_PTL_FATAL("PtlPut failed with rc: %d", rc);
			}

		}

	}
	// rc = ibv_post_send(spdk_rdma_qp->qp, spdk_rdma_qp->send_wrs.first, bad_wr);

	spdk_rdma_qp->send_wrs.first = NULL;
	spdk_rdma_qp->stats->send.doorbell_updates++;
	SPDK_PTL_DEBUG("NVMe: Flushing send requests....DONE\n");
	return 0;

}

bool
spdk_rdma_provider_accel_sequence_supported(void)
{
	SPDK_PTL_DEBUG("No accel sequence supported in PORTALS");
	return false;
}

int rdma_reject(struct rdma_cm_id *id, const void *private_data,
		uint8_t private_data_len)
{
	SPDK_PTL_WARN("XXX TODO XXX not impemented yet continue");
	return 0;
}
