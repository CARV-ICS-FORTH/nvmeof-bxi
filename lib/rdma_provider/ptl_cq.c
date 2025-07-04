#include "ptl_cq.h"
#include "deque.h"
#include "ptl_config.h"
#include "ptl_context.h"
#include "ptl_log.h"
#include "ptl_macros.h"
#include "ptl_object_types.h"
#include <portals4.h>
#include <stdint.h>

struct ptl_cq_singleon_part cq_static = {.lock = PTHREAD_MUTEX_INITIALIZER, .object_type = PTL_STATIC_CQ};
struct ptl_cq ptl_cq_array[PTL_CQ_MAX_QUEUES];

struct ptl_cq *ptl_cq_get(int ptl_cq_id)
{
	RDMA_CM_LOCK(&cq_static.lock);
	if (false == cq_static.initialized) {
		SPDK_PTL_FATAL("Static cq not initialized");
	}

	if (false == ptl_cq_array[ptl_cq_id].is_in_use) {
		SPDK_PTL_FATAL("PTL cq id: %d not initialized", ptl_cq_id);
	}

	RDMA_CM_UNLOCK(&cq_static.lock);
	return &ptl_cq_array[ptl_cq_id];
}



static void ptl_cq_initialize_static(void)
{

	ptl_handle_ni_t nic;
	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	int ret;
	cq_static.initialized = true;
	cq_static.ptl_context = ptl_cnxt_get();
	nic = ptl_cnxt_get_ni_handle(ptl_cnxt);
	ret = PtlEQAlloc(nic, PTL_CQ_SIZE, &cq_static.eq_handle);
	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("PtlEQAlloc failed with error code %d", ret);
	}
	ret =
		PtlPTAlloc(ptl_cnxt_get_ni_handle(cq_static.ptl_context), 0,
			   cq_static.eq_handle, PTL_PT_INDEX, &cq_static.ptl_context->portals_idx_send_recv);
	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("PtlPTAlloc failed for SEND/RECV PORTALS INDEX");
	}
	SPDK_PTL_DEBUG("Allocated portals index: %u for *ALL* (send/recv/rma) operations",
		       cq_static.ptl_context->portals_idx_send_recv);

	SPDK_PTL_DEBUG("Initialized STATIC PART of event queue! %p", &cq_static);
}

struct ptl_cq *ptl_cq_create(void *cq_context)
{
	struct ptl_cq *ptl_cq;
	RDMA_CM_LOCK(&cq_static.lock);

	if (false == cq_static.initialized) {
		ptl_cq_initialize_static();
	}
	if (cq_static.cq_context == NULL) {
		cq_static.cq_context = cq_context;
	}
	if (ptl_cq_array[cq_static.cq_next_id].is_in_use) {
		SPDK_PTL_FATAL("Sorry completion queue with id: %d already in use", cq_static.cq_next_id);
	}
	ptl_cq = &ptl_cq_array[cq_static.cq_next_id];
	ptl_cq->object_type = PTL_CQ;
	ptl_cq->is_in_use = true;
	ptl_cq->cq_id = cq_static.cq_next_id++;
	ptl_cq->cq_static = &cq_static;

	ptl_cq->pending_completions = deque_create(NULL);
	if (ptl_cq->pending_completions == NULL) {
		SPDK_PTL_FATAL("Failed to initialize deque");
	}
	SPDK_PTL_DEBUG("PtlCQ: Initialized pending completions queue for cq_id: %d", ptl_cq->cq_id);

	ptl_cq->fake_ibv_cq.context = ptl_cnxt_get_ibv_context(ptl_cnxt_get());
	SPDK_PTL_DEBUG("Created PtlCQ with id = %d pointer: %p static part: %p", ptl_cq->cq_id, ptl_cq,
		       ptl_cq->cq_static);
	RDMA_CM_UNLOCK(&cq_static.lock);
	return ptl_cq;
}

ptl_handle_eq_t ptl_cq_get_static_event_queue(void)
{
	RDMA_CM_LOCK(&cq_static.lock);
	if (false == cq_static.initialized) {
		ptl_cq_initialize_static();

	}
	RDMA_CM_UNLOCK(&cq_static.lock);
	return cq_static.eq_handle;
}

