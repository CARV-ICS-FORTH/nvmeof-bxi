#ifndef PTL_PD_H
#define PTL_PD_H
#include "ptl_log.h"
#include "ptl_object_types.h"
#include <infiniband/verbs.h>
#include <portals4.h>
#include <spdk/util.h>
#include <stdbool.h>
#include <stdint.h>
#define PTL_PD_MAX_MEM_DESC 32
struct spdk_rdma_utils_mem_map;

struct ptl_pd_mem_desc {
	ptl_md_t local_w_mem_desc;
	ptl_me_t remote_wr_me;
	ptl_handle_md_t local_w_mem_handle;
	ptl_handle_md_t remote_rw_mem_handle;
	ptl_handle_ct_t remote_rw_ct_handle;
	bool remote_read;
	bool remote_write;
	bool local_write;
	bool is_valid;
};

struct ptl_pd {
	ptl_obj_type_e object_type;
	struct ibv_pd fake_pd;
	struct ptl_pd_mem_desc *ptl_mem_desc[PTL_PD_MAX_MEM_DESC];
	struct ptl_context *ptl_cnxt;
	uint32_t num_ptl_mem_desc;

	/**
	* ptl_pd object keeps references to the ptl_md_handle_t. A major difference with the ibv_pd,
	* is that in the Portals case, the ptl_pd needs to know the ptl_cq (or ibv_cq) because it needs it
	* to issue PtlMDBinds calls. All objects will know each other during ibv_create_qp function. In
	* Portals case each ptl_pd associates with a single ptl_eq contrary to the verbs case.
	**/
	struct ptl_eq *ptl_eq;
	struct spdk_rdma_utils_mem_map *mem_map;
	bool in_use;
};

struct ptl_pd *ptl_pd_create(struct ptl_context *ptl_context);

static inline bool ptl_pd_in_use(struct ptl_pd *ptl_pd)
{
	return ptl_pd->in_use;
}

static inline void ptl_pd_set_in_use(struct ptl_pd *ptl_pd)
{
	ptl_pd->object_type = PTL_PD;
	ptl_pd->in_use = true;
}

static inline void ptl_pd_set_cnxt(struct ptl_pd *ptl_pd,
				   struct ptl_context *ptl_cnxt)
{
	ptl_pd->ptl_cnxt = ptl_cnxt;
}

static inline struct ptl_context *ptl_pd_get_cnxt(struct ptl_pd *ptl_pd)
{
	return ptl_pd->ptl_cnxt;
}
static inline struct ptl_pd *ptl_pd_get_from_ibv_pd(struct ibv_pd *ib_pd)
{
	struct ptl_pd *ptl_pd = SPDK_CONTAINEROF(ib_pd, struct ptl_pd, fake_pd);
	if (PTL_PD != ptl_pd->object_type) {
		SPDK_PTL_FATAL("Corrupted ptl_pd expected: %d got %d", PTL_PD, ptl_pd->object_type);
	}
	return ptl_pd;
}

static inline void ptl_pd_set_mem_map(struct ptl_pd *ptl_pd,
				      struct spdk_rdma_utils_mem_map *mem_map)
{
	ptl_pd->mem_map = mem_map;
}

static inline struct ibv_pd *ptl_pd_get_ibv_pd(struct ptl_pd *ptl_pd)
{
	return &ptl_pd->fake_pd;
}

static inline struct ptl_eq *ptl_pd_get_ptl_cq(struct ptl_pd *ptl_pd)
{
	if (NULL == ptl_pd->ptl_eq) {
		SPDK_PTL_FATAL("ptl_pq has not been set!");
	}
	return ptl_pd->ptl_eq;
}
bool ptl_pd_add_mem_desc(struct ptl_pd *ptl_pd, struct ptl_pd_mem_desc *mem_desc);

struct ptl_pd_mem_desc *ptl_pd_get_mem_desc(struct ptl_pd *ptl_pd, uint64_t address,
		size_t length, bool is_local_operation, bool is_remote_operation);
#endif
