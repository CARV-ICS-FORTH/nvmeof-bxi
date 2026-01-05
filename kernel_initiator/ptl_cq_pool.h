#ifndef PTL_CQ_POOL_H
#define PTL_CQ_POOL_H
#include "ptl_object_types.h"
#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <rdma/ib_verbs.h>
#define PTL_CQ_POOL_MAX_SIZE 1024
#define PTL_CQ_POOL_ENABLED 0
#define PTL_CQ_POOL_DISABLED 1
struct ptl_cq_pool {
	ptl_obj_type_e obj_type;
	struct ptl_bxiv3_device *bxiv3_device;
	struct list_head ptl_cq_list_free;
	struct list_head ptl_cq_list_reserved;
	u32 num_free_cqs;
	u32 num_reserved_cqs;
	struct list_head head;
	spinlock_t cq_list_lock;
	unsigned long pool_state;
};

struct ptl_cq_pool *ptl_cq_pool_create(struct ptl_bxiv3_device *bxiv3_dev);
int ptl_cq_pool_destroy(struct ptl_cq_pool *ptl_cq_pool);
struct ptl_cq *ptl_cq_pool_get(struct ptl_cq_pool *ptl_cq_pool, int nr_cqes,
                               enum ib_poll_context poll_ctx);
int ptl_cq_pool_put(struct ptl_cq_pool *ptl_cq_pool, struct ptl_cq *ptl_cq);
#endif
