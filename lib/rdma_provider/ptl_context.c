#include "ptl_context.h"
#include "deque.h"
#include "ptl_config.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include "ptl_print_nvme_commands.h"
#include "ptl_uuid.h"
#include <assert.h>
#include <infiniband/verbs.h>
#include <portals4.h>
#include <portals4_bxiext.h>
#include <pthread.h>
#include <spdk/util.h>
#include <stdbool.h>
#include <stdint.h>
extern volatile int is_target;

struct ptl_cnxt_mem_handle {
	void *vaddr;
	size_t size;
	ptl_md_t mem_handle;
	bool inuse;
};

struct ptl_wc {
	struct ibv_wc wc;
	struct ptl_context_op_meta *op_meta;
};

const char *help_message =
	"ROLE=<target,initiator>(mandatory) PORTALS_NID=<nid number> (optional) PORTALS_PID=<pid number> (mandatory only for target)";
static struct ptl_context ptl_context;
typedef struct ptl_context_op_meta *(*process_event)(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq);

#if PTL_USE_MATCHING
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static uint64_t ptl_cnxt_calculate_crc64(const void *data, size_t length)
{
	uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
	const unsigned char *p = data;

	for (size_t i = 0; i < length; i++) {
		crc ^= (uint64_t)p[i];
		for (int j = 0; j < 8; j++) {
			if (crc & 1) {
				crc = (crc >> 1) ^ 0xC96C5795D7870F42ULL;
			} else {
				crc >>= 1;
			}
		}
	}
	return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

static inline void ptl_cnxt_destroy_op_meta(struct ptl_context_op_meta *op_meta)
{
#if PTL_ENABLE_BIND_PER_OP
	if (op_meta->obj_type == PTL_RECV_OP) {
		goto destroy;
	}
	for (uint32_t i = 0; i < PTL_MAX_SG_LIST; i++) {
		if (NULL == op_meta->md_handle[i].handle) {
			break;
		}
		PtlMDRelease(op_meta->md_handle[i]);
	}
destroy:
#endif
	free(op_meta);
}

#if PTL_USE_MATCHING
static void ptl_cnxt_keep_event(struct ptl_cq *ptl_cq, struct ibv_wc *wc,
				struct ptl_context_op_meta *op_meta)
{
	struct ptl_wc *ptl_wc = calloc(1UL, sizeof(*ptl_wc));

	if (ptl_cq->pending_completions == NULL) {
		SPDK_PTL_FATAL("pending_completions is NULL for ptl_cq id: %d in use: %d", ptl_cq->cq_id,
			       ptl_cq->is_in_use);
	}
	ptl_wc->wc = *wc;
	ptl_wc->op_meta = op_meta;
	deque_push_back(ptl_cq->pending_completions, ptl_wc);
}
#endif

static struct ptl_context_op_meta *ptl_cnxt_process_get(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{
	SPDK_PTL_DEBUG("NVMe: Someone performed an RDMA READ from me, ignore Portals internal");
	return NULL;
}

static struct ptl_context_op_meta *ptl_cnxt_process_get_overflow(ptl_event_t event,
		struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}

static struct ptl_context_op_meta *ptl_cnxt_process_put(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{
	struct ptl_context_op_meta *recv_meta;

	if (NULL == event.user_ptr) {
		SPDK_PTL_DEBUG("NVMe: RECV operation with null context received an RDMA_WRITE");
		return NULL;
	}

	/*Note: Receive operations from the srq is of type PTL_LE_METADATA
	 * (Target). For the initiator is the plain wr_id. We use the receive
	 * length which is 16 bytes for the receive operations of the initiator
	 * and 64 for the target. For now we do this just to avoid additional
	 * calloc and free operations re-think about it.
	 * */
	recv_meta = event.user_ptr;


	if (recv_meta->obj_type != PTL_RECV_OP) {
		SPDK_PTL_FATAL("Corrupted recv op");
	}


	if (event.start != recv_meta->recv_op.io_vector[0].iov_base) {
		SPDK_PTL_FATAL(
			"Corrupted receive event.start: %p event.legnth: %lu "
			"iovector[0] = %p iovector size[0] = %lu pte: %d",
			event.start, event.rlength, recv_meta->recv_op.io_vector[0].iov_base,
			recv_meta->recv_op.io_vector[0].iov_len, event.pt_index);
	}

	recv_meta->recv_op.initiator_qp_num =  ptl_uuid_get_initiator_qp_num(event.match_bits);
	recv_meta->recv_op.target_qp_num = ptl_uuid_get_target_qp_num(event.match_bits);

	if (event.rlength != 64 && event.rlength != 16) {
		SPDK_PTL_FATAL("Wrong size, should have been either 64 B (NVMe command "
			       "size) or 16 B (NVMe response) size it is: %lu",
			       event.rlength);
	}
	if (recv_meta->recv_op.initiator_qp_num == 0 || recv_meta->recv_op.target_qp_num == 0) {
		SPDK_PTL_DEBUG("Staff from header data: initiator qp num %d target qp num: %d",
			       ptl_uuid_get_initiator_qp_num(event.hdr_data), ptl_uuid_get_target_qp_num(event.hdr_data));
		SPDK_PTL_FATAL("Nida does not assign 0 qp num initiator = %d target = %d",
			       recv_meta->recv_op.initiator_qp_num, recv_meta->recv_op.target_qp_num);
	}
	recv_meta->recv_op.bytes_received = event.rlength;
	recv_meta->recv_op.receive_done = true;

	return NULL;
}

static struct ptl_context_op_meta *ptl_cnxt_process_put_overflow(ptl_event_t event,
		struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}


static struct ptl_context_op_meta *ptl_cnxt_process_atomic(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}


static struct ptl_context_op_meta *ptl_cnxt_process_atomic_overflow(ptl_event_t event,
		struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}


static struct ptl_context_op_meta *ptl_cnxt_process_fetch_atomic(ptl_event_t event,
		struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}

static struct ptl_context_op_meta *ptl_cnxt_process_fetch_atomic_overflow(ptl_event_t event,
		struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}


/**
 * Handles PTL_EVENT_REPLY events, which indicate that a PtlGet operation
 * issued by the initiator has successfully transferred the data into its
 * memory. This notification confirms the RDMA read completion.
 *
 * @param event The Portals event containing reply information
 * @param wc Work completion structure to be filled with operation results
 * @param ptl_cq The Portals completion queue
 * @return Pointer to the operation metadata, or NULL if no context was provided
 */
static struct ptl_context_op_meta *ptl_cnxt_process_reply(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{
	struct ptl_context_op_meta *rdma_read_meta = event.user_ptr;

	if (rdma_read_meta == NULL) {
		SPDK_PTL_DEBUG("Caution RDMA read without a context app does not want a signal ok.");
		return NULL;
	}

	if (rdma_read_meta->obj_type != PTL_RDMA_READ_OP) {
		SPDK_PTL_FATAL("Corrupted obj type should have been a PTL_RDMA_READ_OP");
	}


	if (event.ni_fail_type != PTL_NI_OK) {
		SPDK_PTL_FATAL("Operation failed with code: %d", event.ni_fail_type);
	}

	if (++rdma_read_meta->rdma_read_op.parts_acked < rdma_read_meta->rdma_read_op.total_parts) {
		return NULL;
	}

	if (false == rdma_read_meta->signal_app) {
		ptl_cnxt_destroy_op_meta(rdma_read_meta);
		return NULL;
	}

	wc->status =
		event.ni_fail_type == PTL_NI_OK ? IBV_WC_SUCCESS : IBV_WC_LOC_PROT_ERR;
	wc->opcode = IBV_WC_RDMA_READ;
	wc->wr_id = rdma_read_meta->wr_id;
	wc->byte_len = event.rlength;
	wc->qp_num = rdma_read_meta->send_op.qp_num;

	if (wc->qp_num == 0) {
		SPDK_PTL_FATAL("Nida does not assign 0 fake qp numbers");
	}
	wc->src_qp = 0;/*XXX TODO XXX*/
	SPDK_PTL_DEBUG("NVMe: RDMA read done (PTL_EVENT_REPLY). Number of bytes received: %lu. Filling wc with code %d qp_num: %d",
		       event.rlength, event.ni_fail_type, rdma_read_meta->send_op.qp_num);


	return rdma_read_meta;
}

static struct ptl_context_op_meta *ptl_cnxt_process_send(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{
	return NULL;
}


/**
 * Process a PTL_EVENT_ACK for an initiator-issued PtlPut.
 *
 * This handler is invoked when the initiator receives a PTL_EVENT_ACK generated
 * for a PtlPut operation that explicitly requested an acknowledgment. In Portals 4,
 * PTL_EVENT_ACK is an initiator-side event that indicates the target has matched the
 * incoming PUT against its match list (ME/LE) and completed the delivery to the target
 * memory according to Portals 4 semantics.
 *
 * Notes:
 * - Applies to PtlPut regardless of whether the initiator’s usage corresponds to a
 *   send or an (RMA) PUT. In both cases, the ACK confirms data are in the memory of the
 *   Target.
 * - This is stronger than local send completion (PTL_EVENT_SEND). PTL_EVENT_ACK means
 *   the target has received, matched, and placed the data into its memory.
 * - You will not receive PTL_EVENT_ACK for PtlGet; GET completion at the initiator is
 *   indicated by PTL_EVENT_REPLY instead.
 *
 * On success (event.ni_fail_type == PTL_NI_OK), this function populates the provided
 * ibv_wc with a successful SEND completion semantics for the original PUT, using the
 * metadata carried in event.user_ptr, and returns the associated ptl_context_op_meta.
 * On failure, it reports the Portals NI failure code.
 */
static struct ptl_context_op_meta *ptl_cnxt_process_ack(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{
	struct ptl_context_op_meta *send_meta;

	if (NULL == event.user_ptr) {
		SPDK_PTL_DEBUG("NVMe: PTL_EVENT_ACK without context?");
		return NULL;
	}
	send_meta = event.user_ptr;

	if (send_meta->obj_type != PTL_SEND_OP && send_meta->obj_type != PTL_RDMA_WRITE_OP) {
		SPDK_PTL_FATAL("Corrupted object type: %d this is not a PTL_SEND_OP or PTL_RDMA_WRITE_OP",
			       send_meta->obj_type);
	}

	SPDK_PTL_DEBUG("NVMe: Got a PTL_EVENT_ACK event for an %s operation. Filling "
		       "wc with code %d event type: %d from local qp num: %d",
		       send_meta->obj_type == PTL_SEND_OP ? "SEND" : "RDMA_WRITE",
		       event.ni_fail_type, event.type, send_meta->send_op.qp_num);

	if (event.ni_fail_type != PTL_NI_OK) {
		SPDK_PTL_FATAL("Operation failed with code: %d", event.ni_fail_type);
	}

	if (PTL_RDMA_WRITE_OP == send_meta->obj_type &&
	    ++send_meta->rdma_write_op.parts_acked < send_meta->rdma_write_op.total_parts) {
		return NULL;
	}

	if (false == send_meta->signal_app) {
		SPDK_PTL_DEBUG("App does not want to be signaled freeing the buffer");
		ptl_cnxt_destroy_op_meta(send_meta);
		return NULL;
	}

	wc->status =
		event.ni_fail_type == PTL_NI_OK ? IBV_WC_SUCCESS : IBV_WC_LOC_PROT_ERR;
	wc->opcode = IBV_WC_SEND;
	wc->wr_id = send_meta->wr_id;
	wc->byte_len = event.rlength;
	wc->qp_num = send_meta->send_op.qp_num;

	if (wc->qp_num == 0) {
		SPDK_PTL_FATAL("Nida does not assign 0 fake qp numbers");
	}

	SPDK_PTL_DEBUG("Got ack for send for wr_id: %lu mismatch of queues? %s send ack "
		       "is for queue: %d current ptl_cq is: %d",
		       send_meta->wr_id, ptl_cq_get_id(ptl_cq) != send_meta->cq_id ? "YES" : "NO",
		       send_meta->cq_id, ptl_cq_get_id(ptl_cq));

	wc->src_qp = 0; // TOOO
	return send_meta;
}

static struct ptl_context_op_meta *ptl_cnxt_process_bt_disabled(ptl_event_t event,
		struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}

static struct ptl_context_op_meta *ptl_cnxt_process_auto_unlink(ptl_event_t event,
		struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{
	struct ptl_context_op_meta *recv_meta;

	if (NULL == event.user_ptr) {
		SPDK_PTL_FATAL("Unlink event must have an associated user context");
	}

	recv_meta = event.user_ptr;

	if (recv_meta->obj_type != PTL_RECV_OP) {
		SPDK_PTL_FATAL("Corrupted recv_op");
	}

	if (false == recv_meta->recv_op.receive_done) {
		SPDK_PTL_FATAL("AUTO_UNLINK without a prior receive for "
			       "{buffer:%lu, len: %lu cq id: %d} event fail type: %d",
			       (size_t)recv_meta->recv_op.io_vector[0].iov_base,
			       recv_meta->recv_op.io_vector[0].iov_len, ptl_cq_get_id(ptl_cq), event.ni_fail_type);
	}

	if (event.ni_fail_type != PTL_NI_OK) {
		SPDK_PTL_FATAL("Operation failed");
	}

	wc->status =
		event.ni_fail_type == PTL_NI_OK ? IBV_WC_SUCCESS : IBV_WC_LOC_PROT_ERR;
	wc->opcode = IBV_WC_RECV;

	wc->byte_len = recv_meta->recv_op.bytes_received;
	wc->wr_id = recv_meta->wr_id;
	wc->qp_num = is_target ? recv_meta->recv_op.target_qp_num : recv_meta->recv_op.initiator_qp_num;

	SPDK_PTL_DEBUG("NVMe-cmd-recv: RECV (PtlPut+AUTO_UNLINK) operation is "
		       "between the pair initiator_qp_num = %d target_qp_num = "
		       "%d is target? %s size: %lu B wc->qp_num = %d recv_buffer = %lu",
		       recv_meta->recv_op.initiator_qp_num,
		       recv_meta->recv_op.target_qp_num, is_target ? "YES" : "NO",
		       recv_meta->recv_op.bytes_received, wc->qp_num, (uint64_t)recv_meta->recv_op.io_vector[0].iov_base);

	if (wc->qp_num == 0) {
		SPDK_PTL_FATAL(
			"Nida does not assign 0 fake qp numbers is_target? %s pair "
			"is [initiator qp num: %d target_qp_num: %d]",
			is_target ? "YES" : "NO", recv_meta->recv_op.initiator_qp_num,
			recv_meta->recv_op.target_qp_num);
	}

	wc->src_qp = INT32_MAX;

	SPDK_PTL_DEBUG("Got ack for recv for wr_id: %lu mismatch of queues? %s send ack "
		       "is for queue: %d current ptl_cq is: %d",
		       recv_meta->wr_id, ptl_cq_get_id(ptl_cq) != recv_meta->cq_id ? "YES" : "NO",
		       recv_meta->cq_id, ptl_cq_get_id(ptl_cq));
	return recv_meta;
}

static struct ptl_context_op_meta *ptl_cnxt_process_auto_free(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}

static struct ptl_context_op_meta *ptl_cnxt_process_search(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}


static struct ptl_context_op_meta *ptl_cnxt_process_link(ptl_event_t event, struct ibv_wc *wc,
		struct ptl_cq *ptl_cq)
{

	SPDK_PTL_DEBUG("PROCESS LINK EVENT OK go on PORTALS internal");
	return NULL;
}

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

// static const char *ptl_event_kind_to_str(ptl_event_kind_t event_kind)
// {
// 	switch (event_kind) {
// 	case PTL_EVENT_GET:
// 		return "PTL_EVENT_GET";
// 	case PTL_EVENT_GET_OVERFLOW:
// 		return "PTL_EVENT_GET_OVERFLOW";
// 	case PTL_EVENT_PUT:
// 		return "PTL_EVENT_PUT";
// 	case PTL_EVENT_PUT_OVERFLOW:
// 		return "PTL_EVENT_PUT_OVERFLOW";
// 	case PTL_EVENT_ATOMIC:
// 		return "PTL_EVENT_ATOMIC";
// 	case PTL_EVENT_ATOMIC_OVERFLOW:
// 		return "PTL_EVENT_ATOMIC_OVERFLOW";
// 	case PTL_EVENT_FETCH_ATOMIC:
// 		return "PTL_EVENT_FETCH_ATOMIC";
// 	case PTL_EVENT_FETCH_ATOMIC_OVERFLOW:
// 		return "PTL_EVENT_FETCH_ATOMIC_OVERFLOW";
// 	case PTL_EVENT_REPLY:
// 		return "PTL_EVENT_REPLY";
// 	case PTL_EVENT_SEND:
// 		return "PTL_EVENT_SEND";
// 	case PTL_EVENT_ACK:
// 		return "PTL_EVENT_ACK";
// 	case PTL_EVENT_PT_DISABLED:
// 		return "PTL_EVENT_PT_DISABLED";
// 	case PTL_EVENT_AUTO_UNLINK:
// 		return "PTL_EVENT_AUTO_UNLINK";
// 	case PTL_EVENT_AUTO_FREE:
// 		return "PTL_EVENT_AUTO_FREE";
// 	case PTL_EVENT_SEARCH:
// 		return "PTL_EVENT_SEARCH";
// 	case PTL_EVENT_LINK:
// 		return "PTL_EVENT_LINK";
// 	default:
// 		return "UNKNOWN_EVENT";
// 	}
// }

static int ptl_print_event(struct ptl_context_op_meta *op_meta, bool is_late)
{

	//TODO remove later DEBUG
	if (op_meta->obj_type == PTL_SEND_OP &&
	    op_meta->send_op.crc_checksum != ptl_cnxt_calculate_crc64(op_meta->send_op.addr,
			    op_meta->send_op.length)) {
		SPDK_PTL_FATAL("Corruption, src buffer was touched before ACK!");
	}
	const char *prefix_cmd_send = "NVMe-cmd-send";
	const char *prefix_cmd_send_late = "NVMe-cmd-send-late";
	const char *prefix_cpl_send = "NVMe-cpl-send";
	const char *prefix_cpl_send_late = "NVMe-cpl-send-late";

	const char *prefix_cmd_recv = "NVMe-cmd-recv";
	const char *prefix_cmd_recv_late = "NVMe-cmd-recv-late";
	const char *prefix_cpl_recv = "NVMe-cpl-recv";
	const char *prefix_cpl_recv_late = "NVMe-cpl-recv-late";
	if (op_meta->obj_type == PTL_SEND_OP) {
		SPDK_PTL_INFO(
			"SEND Event Print: %d",
			op_meta->send_op.length == 64
			? ptl_print_nvme_cmd(
				op_meta->send_op.addr,
				is_late ? prefix_cmd_send_late : prefix_cmd_send, op_meta->cq_id)
			: ptl_print_nvme_cpl(
				op_meta->send_op.addr,
				is_late ? prefix_cpl_send_late : prefix_cpl_send, op_meta->cq_id));
	} else if (op_meta->obj_type == PTL_RECV_OP) {
		SPDK_PTL_INFO(
			"RECV Event Print: %d",
			op_meta->recv_op.bytes_received == 64
			? ptl_print_nvme_cmd(
				op_meta->recv_op.io_vector[0].iov_base,
				is_late ? prefix_cmd_recv_late : prefix_cmd_recv, op_meta->cq_id)
			: ptl_print_nvme_cpl(
				op_meta->recv_op.io_vector[0].iov_base,
				is_late ? prefix_cpl_recv_late : prefix_cpl_recv, op_meta->cq_id));
	} else if (op_meta->obj_type == PTL_RDMA_WRITE_OP) {
		SPDK_PTL_INFO("NVMe-send: An RDMA write completed ok move on");
	} else if (op_meta->obj_type == PTL_RDMA_READ_OP) {
		SPDK_PTL_INFO("NVMe-recv: An RDMA read completed ok move on");
	} else {
		SPDK_PTL_FATAL("Corrupted op meta");
	}
	return 1;
}

#if PTL_USE_MATCHING
static int ptl_cnxt_poll_cq(struct ibv_cq *ibv_cq, int num_entries,
			    struct ibv_wc *wc)
{

	ptl_event_t event;
	int ret;
	int events_processed = 0;
	struct ptl_wc *ptl_late_wc;
	struct ptl_context_op_meta *op_meta;


	pthread_mutex_lock(&g_lock);

	struct ptl_cq *ptl_cq = ptl_cq_get_from_ibv_cq(ibv_cq);

	if (ptl_cq->pending_completions == NULL) {
		SPDK_PTL_FATAL("pending_completions is NULL, at this point it shouldn't");
	}

	if (ptl_cq->is_in_use == false) {
		SPDK_PTL_FATAL("Cannot happen");
	}

	/*First of all look for pending staff that was for me and I missed them*/
	while (events_processed < num_entries) {
		ptl_late_wc = deque_pop_front(ptl_cq->pending_completions);
		if (ptl_late_wc == NULL) {
			break;
		}
		SPDK_PTL_DEBUG("PtlCQ: Delivered late event for ptl_cq id: %d wr_id = %lu", ptl_cq->cq_id,
			       ptl_late_wc->wc.wr_id);
		wc[events_processed++] = ptl_late_wc->wc;
		SPDK_PTL_DEBUG("Late event: %d", ptl_print_event(ptl_late_wc->op_meta, true));
		ptl_cnxt_destroy_op_meta(ptl_late_wc->op_meta);
		ptl_late_wc->op_meta = NULL;
		free(ptl_late_wc);
		ptl_late_wc = NULL;
	}

	while (events_processed < num_entries) {
		ret = PtlEQGet(ptl_cq_get_queue(ptl_cq), &event);
		if (ret == PTL_OK) {
			op_meta = handler[event.type](event, &wc[events_processed], ptl_cq);
			if (NULL == op_meta) {
				continue;
			}

			if (ptl_cq->cq_id != op_meta->cq_id) {
				SPDK_PTL_DEBUG("PtlCQ: Wrong cq_id for the event current ptl_cq id = %d "
					       "event is for: %d, keep it to serve it later wr_id = %lu",
					       ptl_cq->cq_id, op_meta->cq_id, wc->wr_id);
				ptl_cnxt_keep_event(&ptl_cq_array[op_meta->cq_id], &wc[events_processed], op_meta);
				continue;
			}

			SPDK_PTL_DEBUG("PtlCQ Delivered on-time event: %d", ptl_print_event(op_meta, false));
			ptl_cnxt_destroy_op_meta(op_meta);
			op_meta = NULL;
			++events_processed;

		} else if (ret == PTL_EQ_EMPTY) {
			// SPDK_PTL_DEBUG("No events ok COOL");
			break;
		} else if (ret == PTL_EQ_DROPPED) {
			SPDK_PTL_DEBUG("Ok queue overflow break");
			break;
		} else {
			SPDK_PTL_FATAL("PtlEQGet failed with error code %d", ret);
		}
	}
	pthread_mutex_unlock(&g_lock);
	return events_processed;
}
#else
static int ptl_cnxt_poll_cq(struct ibv_cq *ibv_cq, int num_entries,
			    struct ibv_wc *wc)
{

	ptl_event_t event;
	int ret;
	int events_processed = 0;
	struct ptl_context_op_meta *op_meta;

	struct ptl_cq *ptl_cq = ptl_cq_get_from_ibv_cq(ibv_cq);
	if (false == ptl_cq->eq_enabled) { //Not ready yet
		return 0;
	}

	while (events_processed < num_entries) {
		ret = PtlEQGet(ptl_cq_get_queue(ptl_cq), &event);
		if (ret == PTL_OK) {
			op_meta = handler[event.type](event, &wc[events_processed], ptl_cq);
			if (NULL == op_meta) {
				continue;
			}

			if (ptl_cq_get_id(ptl_cq) != op_meta->cq_id) {
				SPDK_PTL_FATAL("PtlCQ: Wrong cq_id for the event current ptl_cq id = %d "
					       "event is for: %d. This case is FATAL for the non-matching case",
					       ptl_cq_get_id(ptl_cq), op_meta->cq_id);
			}
			ptl_cnxt_destroy_op_meta(op_meta);
			op_meta = NULL;
			++events_processed;

		} else if (ret == PTL_EQ_EMPTY) {
			// SPDK_PTL_DEBUG("No events ok COOL");
			break;
		} else if (ret == PTL_EQ_DROPPED) {
			SPDK_PTL_DEBUG("Ok queue overflow break");
			break;
		} else {
			SPDK_PTL_FATAL("PtlEQGet failed with error code %d", ret);
		}
	}
	return events_processed;
}
#endif

struct ptl_context *ptl_cnxt_get(void)
{
	static pthread_mutex_t cnxt_lock = PTHREAD_MUTEX_INITIALIZER;
	ptl_ni_limits_t desired;
	ptl_ni_limits_t actual;
	int ret;
	const char *role;/*target or initiator*/
	const char *ptl_pid;
	const char *ptl_nid;
	ptl_process_t actual_phys_id;
	pthread_mutex_lock(&cnxt_lock);
	if (ptl_context.initialized) {
		goto exit;
	}

	role = getenv("ROLE");
	if (NULL == role) {
		SPDK_PTL_FATAL("Sorry you need to set the role of this process (target, initiator). %s",
			       help_message);
	}

	if (strcmp(role, "target") && strcmp(role, "initiator")) {
		SPDK_PTL_FATAL("ROLE should be either targer or initiator. %s", help_message);
	}

	ptl_pid = getenv("PORTALS_PID");
	if (NULL == ptl_pid && 0 == strcmp(role, "target")) {
		SPDK_PTL_FATAL("Sorry you need to set SERVER_PID env variable for the target case. %s",
			       help_message);
	}

	if (NULL == ptl_pid && 0 == strcmp(role, "initiator")) {
		SPDK_PTL_WARN("The system will assign automatically a PID for the initiator role");
	}

	ptl_nid = getenv("PORTALS_NID");
	if (NULL == ptl_pid && 0 == strcmp(role, "target")) {
		SPDK_PTL_FATAL("Sorry you need to set SERVER_NID env variable for the target case. %s",
			       help_message);
	}

	if (NULL == ptl_pid && 0 == strcmp(role, "initiator")) {
		SPDK_PTL_WARN("The system will assign automatically an NID for the initiator role");
	}

	ptl_context.object_type = PTL_CONTEXT;
	SPDK_PTL_DEBUG("Calling PtlInit()");
	ret = PtlInit();
	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("PtlInit failed");
	}

	/*XXX TODO XXX Check for errors and staff*/
	ptl_context.nid = ptl_nid ? atoi(ptl_nid) : PTL_NID_ANY;
	ptl_context.pid = ptl_pid ? atoi(ptl_pid) : PTL_PID_ANY;
	ptl_context.is_target = (0 == strcmp(role, "target"));
	SPDK_PTL_DEBUG("Is target?: %d", ptl_context.is_target);

	memset(&desired, 0, sizeof(desired));

	desired.max_entries = 479075;
	//This affects EQAlloc
	desired.max_eqs = 1024;
	//This affect MDBind
	desired.max_mds = 479075;
	//This affects max ptes?
	desired.max_pt_index = 511;
	// desired.max_list_size = 479075;
	// desired.max_unexpected_headers = 479075;
	// desired.max_cts = 1024;
	// desired.max_iovecs = 1073741823;
	// desired.max_triggered_ops = 479075;
	// desired.max_msg_size = 68719476735UL;
	// desired.max_atomic_size = 0;
	// desired.max_fetch_atomic_size = 0;
	// desired.max_waw_ordered_size = 0;
	// desired.max_war_ordered_size = 0;
	// desired.max_volatile_size = 60;
	desired.features = PTL_BXI3_SERVICE;
	// desired.bxi_max_cqs = 1;
	// desired.bxi_compute_line = 2374264728;
	// desired.cq_mode = 32767;
	// desired.host_cq_size = 139646804452922;

#if PTL_USE_MATCHING
	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_MATCHING | PTL_NI_PHYSICAL,
			ptl_context.pid, &desired, &actual, &ptl_context.ni_handle);
#else
	ret = PtlNIInit(PTL_IFACE_DEFAULT, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL,
			ptl_context.pid, &desired, &actual, &ptl_context.ni_handle);
#endif

	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("PtlNIInit failed with code: %d for nid: %d and pid: %d", ret,
			       ptl_context.nid, ptl_context.pid);
	}

	ret = PtlGetPhysId(ptl_context.ni_handle, &actual_phys_id);
	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("PtlGetPhysId failed: %d", ret);
	}
	ptl_context.nid = actual_phys_id.phys.nid;
	ptl_context.pid =  actual_phys_id.phys.pid;
	SPDK_PTL_INFO("Started role: %s with {nid:%d,pid:%d}", role,
		      actual_phys_id.phys.nid, actual_phys_id.phys.pid);

	// Check if PTL_TOTAL_DATA_ORDERING is supported
	// if (actual.features & PTL_TOTAL_DATA_ORDERING) {
	// 	SPDK_PTL_DEBUG("Total data ordering is enabled");
	// } else {
	// 	SPDK_PTL_FATAL("Total data ordering is not supported by this implementation. Cannot support NVMe-OF properties");
	// }

	ptl_context.fake_ibv_cnxt.ops.poll_cq = ptl_cnxt_poll_cq;
	ptl_context.fake_cq.context = &ptl_context.fake_ibv_cnxt;
	ptl_context.pte_table_size = actual.max_pt_index;
	ptl_context.pte_table = calloc(ptl_context.pte_table_size,
				       sizeof(*ptl_context.pte_table));
	pthread_mutex_init(&ptl_context.pte_table_lock, NULL);
	ptl_context.pte_table[PTL_CP_SERVER_PTE] = 1;

#if PTL_USE_MATCHING
	ptl_context.portals_idx_rma = PTL_PT_INDEX;
#else
	/**
	 * XXX TODO XXX ptl_context.is_target is directly inferred from the env variable. On the other hand,
	 * is_target volatile variable is inferred when either rdma_listen or rdma_connect is called.
	 * One of them should go.
	 */
	if (!ptl_context.is_target) {
		SPDK_PTL_DEBUG("Role is: %s is_target: %d strcmp res: %d", role, is_target, strcmp(role, "target"));
		ptl_context.portals_idx_rma = ptl_cnxt_allocate_pte(&ptl_context);
	}
	SPDK_PTL_DEBUG("SUCCESSFULLY create and initialized PORTALS context for "
		       "initiator accepting RMA operations at PTE: %d",
		       ptl_context.portals_idx_rma);
#endif
	SPDK_PTL_DEBUG("SUCCESSFULLY create and initialized PORTALS context with matcing enabled with role: %s",
		       role);
	ptl_context.initialized = true;
exit:
	pthread_mutex_unlock(&cnxt_lock);
	return &ptl_context;
}

int ptl_cnxt_get_pte(struct ptl_context *cnxt, int pte)
{
	int rc;
	pthread_mutex_lock(&cnxt->pte_table_lock);
	if (cnxt->pte_table[pte]) {
		SPDK_PTL_WARN("Suspicious pte: %d requested already taken.", pte);
		rc = -1;
		goto exit;
	}
	rc = pte;
	cnxt->pte_table[pte] = 1;
exit:
	pthread_mutex_unlock(&cnxt->pte_table_lock);
	return rc;
}

int ptl_cnxt_allocate_pte(struct ptl_context *cnxt)
{
	int pte;
	pthread_mutex_lock(&cnxt->pte_table_lock);
#if PTL_USE_MATCHING
	/*In the case of matching we use a single PTE PTL_PT_INDEX for the NVMe-cmd, NVMe-cpl, and RMA operations*/
	SPDK_PTL_DEBUG("Allocated PTE: %d", PTL_PT_INDEX);
	cnxt->pte_table[PTL_PT_INDEX] = 1;
	pte = PTL_PT_INDEX;
	goto exit;
#else
	if (cnxt->is_target && cnxt->pte_table[PTL_PT_INDEX]) {
		SPDK_PTL_FATAL("Role is \"Target\" and PTL_PT_INDEX already taken smells like error");
	}
	for (uint32_t i = 0; i < cnxt->pte_table_size; i++) {
		if (cnxt->pte_table[i]) {
			continue;
		}
		cnxt->pte_table[i] = 1;
		pte = i;
		goto exit;
	}
	SPDK_PTL_FATAL("No more ptes sorry!");
#endif
exit:
	pthread_mutex_unlock(&cnxt->pte_table_lock);
	return pte;
}

struct ibv_context *ptl_cnxt_get_ibv_context(struct ptl_context *cnxt)
{
	return &cnxt->fake_ibv_cnxt;
}

struct ptl_context *ptl_cnxt_get_from_ibcnxt(struct ibv_context *ib_cnxt)
{
	struct ptl_context *cnxt =
		SPDK_CONTAINEROF(ib_cnxt, struct ptl_context, fake_ibv_cnxt);
	if (PTL_CONTEXT != cnxt->object_type) {
		SPDK_PTL_FATAL("Corrupted portals context, magic number does not match");
	}
	return cnxt;
}

struct ptl_context *ptl_cnxt_get_from_ibvpd(struct ibv_pd *ib_pd)
{
	struct ptl_pd *ptl_pd = ptl_pd_get_from_ibv_pd(ib_pd);
	return ptl_pd_get_cnxt(ptl_pd);
}

ptl_pt_index_t ptl_cnxt_get_portal_index(struct ptl_context *cnxt)
{
#if PTL_USE_MATCHING
	return cnxt->portals_idx_send_recv;
#else
	SPDK_PTL_FATAL("Unsupported function in the non-matching case");
	return -1;
#endif
}

ptl_handle_ni_t ptl_cnxt_get_ni_handle(struct ptl_context *cnxt)
{
	if (false == cnxt->initialized) {
		SPDK_PTL_FATAL("Context is not initialized!");
	}
	return cnxt->ni_handle;
}

/**
  * Returns the PTE responsdile (that has an LE) for RMA operations
*/
int ptl_cnxt_get_rma_pte(struct ptl_context *cnxt)
{

	SPDK_PTL_DEBUG("RMA pte = %d", cnxt->is_target ? -1 : cnxt->portals_idx_rma);
	return cnxt->is_target ? -1 : cnxt->portals_idx_rma;
}

