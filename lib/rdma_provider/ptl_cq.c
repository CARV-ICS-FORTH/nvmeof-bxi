#include "ptl_cq.h"
#include "deque.h"
#include "ptl_config.h"
#include "ptl_context.h"
#include "ptl_log.h"
#include "ptl_macros.h"
#include "ptl_object_types.h"
#include <portals4.h>
#include <stdint.h>
#if PTL_USE_MATCHING
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
	int ret;
	cq_static.pte = ptl_cnxt_get_pte(ptl_cnxt_get(), PTL_PT_INDEX);
	if (-1 == cq_static.pte) {
		SPDK_PTL_FATAL("Failed to get PTE: %d", PTL_PT_INDEX);
	}
	cq_static.initialized = true;
	cq_static.ptl_context = ptl_cnxt_get();
	nic = ptl_cnxt_get_ni_handle(cq_static.ptl_context);
	ret = PtlEQAlloc(nic, PTL_CQ_SIZE, &cq_static.eq_handle);
	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("PtlEQAlloc failed with error code %d", ret);
	}
	ret =
		PtlPTAlloc(ptl_cnxt_get_ni_handle(cq_static.ptl_context), 0,
			   cq_static.eq_handle, cq_static.pte, &cq_static.ptl_context->portals_idx_send_recv);
	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("PtlPTAlloc failed for SEND/RECV PORTALS INDEX");
	}
	ret = PtlPTEnable(ptl_cnxt_get_ni_handle(ptl_cnxt_get()),
			  cq_static.ptl_context->portals_idx_send_recv);
	if (ret != PTL_OK) {
		SPDK_PTL_FATAL("Failed to enable PTE: %d", cq_static.pte);
	}
	SPDK_PTL_DEBUG("Static part of ptl_cq: Allocated PTE:%d for *ALL* (send/recv/rma) operations",
		       cq_static.ptl_context->portals_idx_send_recv);
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
#else
static int next_cq_id;
struct ptl_cq_core *ptl_cq_core_create(int pte, bool is_shared)
{

	struct ptl_cq_core *ptl_cq_core = calloc(1UL, sizeof(*ptl_cq_core));
	int rc;
	ptl_cq_core->pte = pte;
	rc = PtlEQAlloc(ptl_cnxt_get_ni_handle(ptl_cnxt_get()), PTL_CQ_SIZE, &ptl_cq_core->eq_handle);
	if (PTL_OK != rc) {
		SPDK_PTL_FATAL("Failed to initialize EQ error code: %d", rc);
	}
	rc = PtlPTAlloc(ptl_cnxt_get_ni_handle(ptl_cnxt_get()), 0, ptl_cq_core->eq_handle, ptl_cq_core->pte,
			&ptl_cq_core->pte_handle);
	if (PTL_OK != rc) {
		SPDK_PTL_FATAL("Failed to initialize PTE: %d with error code: %d", ptl_cq_core->pte, rc);
	}
	rc = PtlPTEnable(ptl_cnxt_get_ni_handle(ptl_cnxt_get()), ptl_cq_core->pte_handle);
	if (PTL_OK != rc) {
		SPDK_PTL_FATAL("Failed to enable PTE: %d with error code: %d", ptl_cq_core->pte, rc);
	}
	SPDK_PTL_DEBUG("PTL_SRQ: Initialized SRQ Successfully at PTE: %d and plugged to its event queue",
		       ptl_cq_core->pte);

	ptl_cq_core->cq_id = __sync_fetch_and_add(&next_cq_id, 1);
	ptl_cq_core->is_shared = is_shared;
	return ptl_cq_core;
}

struct ptl_cq *ptl_cq_create(void *cq_context)
{
	(void)cq_context;
	struct ptl_cq *ptl_cq;

	ptl_cq = calloc(1UL, sizeof(*ptl_cq));
	if (ptl_cq == NULL) {
		SPDK_PTL_FATAL("Out of memory");
	}
	ptl_cq->object_type = PTL_CQ;
	ptl_cq->fake_ibv_cq.context = ptl_cnxt_get_ibv_context(ptl_cnxt_get());
	return ptl_cq;
}

#endif

