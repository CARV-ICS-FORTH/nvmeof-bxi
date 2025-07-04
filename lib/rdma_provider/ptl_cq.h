#ifndef PTL_CQ_H
#define PTL_CQ_H
#include "ptl_config.h"
#include "ptl_log.h"
#include "ptl_object_types.h"
#include "spdk/util.h"
#include <infiniband/verbs.h>
#include <portals4.h>
#include <pthread.h>
#include <stdint.h>
#define PTL_CQ_MAX_QUEUES 256UL
struct ptl_context;
struct deque;



struct ptl_cq_singleon_part {
	ptl_obj_type_e object_type;
	struct ptl_context *ptl_context;
	ptl_handle_eq_t eq_handle;
	void *cq_context;
	pthread_mutex_t lock;
	int cq_next_id;
	bool initialized;
};

struct ptl_cq {
	ptl_obj_type_e object_type;
	struct ptl_cq_singleon_part *cq_static;
	struct ibv_cq fake_ibv_cq;
	int cq_id;
	struct deque *pending_completions;
	bool is_in_use;
	// struct ptl_context *ptl_context;
	// ptl_handle_eq_t eq_handle;
	// void *cq_context;
	// pthread_mutex_t lock;
	// bool initialized;
};
extern struct ptl_cq ptl_cq_array[PTL_CQ_MAX_QUEUES];

struct ptl_cq *ptl_cq_get(int ptl_cq_id);

struct ptl_cq *ptl_cq_create(void *cq_context);
// struct ptl_cq *ptl_cq_get_instance(void *cq_context);

static inline ptl_handle_eq_t ptl_cq_get_queue(struct ptl_cq *ptl_cq)
{
	if (false == ptl_cq->cq_static->initialized) {
		SPDK_PTL_FATAL("Uninitialized event queue");
	}
	return ptl_cq->cq_static->eq_handle;
}

static inline struct ptl_cq *ptl_cq_get_from_ibv_cq(struct ibv_cq *ibv_cq)
{
	struct ptl_cq *ptl_cq = SPDK_CONTAINEROF(ibv_cq, struct ptl_cq, fake_ibv_cq);
	if (PTL_CQ != ptl_cq->object_type) {
		SPDK_PTL_FATAL("Corrupted ptl_cq");
	}
	return ptl_cq;
}

static inline struct ibv_cq *ptl_cq_get_ibv_cq(struct ptl_cq *ptl_cq)
{
	return &ptl_cq->fake_ibv_cq;
}

ptl_handle_eq_t ptl_cq_get_static_event_queue(void);
#endif
