#ifndef PTL_PD_H
#define PTL_PD_H
#include "ptl_log.h"
#include "ptl_object_types.h"
#include <infiniband/verbs.h>
#include <portals4.h>
#include <spdk/util.h>
#include <stdbool.h>
#include <stdint.h>
struct spdk_rdma_utils_mem_map;
struct ptl_mem_desc;
struct ptl_pd;
struct ptl_pd_mem_desc_map;
struct ptl_pd_mem_desc_map_ops;
typedef struct ptl_pd_mem_desc_map *(*ptl_pd_mem_desc_map_create)(uint32_t num_entries,
		const char *name);
typedef bool (*ptl_pd_mem_desc_map_add)(struct ptl_pd_mem_desc_map *map,
					struct ptl_mem_desc *mem_desc);
typedef struct ptl_mem_desc *(*ptl_pd_mem_desc_map_get)(struct ptl_pd_mem_desc_map *map,
		uint64_t address, size_t length,
		bool is_remote_operation);
typedef bool (*ptl_mem_desc_map_destroy)(struct ptl_pd_mem_desc_map *mem_desc_map);

struct ptl_pd_mem_desc_map_ops {
	ptl_pd_mem_desc_map_create create;
	ptl_pd_mem_desc_map_add add;
	ptl_pd_mem_desc_map_get get;
	ptl_mem_desc_map_destroy destroy;
};


struct ptl_pd {
	ptl_obj_type_e object_type;
	struct ibv_pd fake_pd;

	struct ptl_context *ptl_cnxt;

	/**
	* ptl_pd object keeps references to the ptl_md_handle_t. A major difference with the ibv_pd,
	* is that in the Portals case, the ptl_pd needs to know the ptl_cq (or ibv_cq) because it needs it
	* to issue PtlMDBinds calls. All objects will know each other during ibv_create_qp function. In
	* Portals case each ptl_pd associates with a single ptl_eq contrary to the verbs case.
	**/
	struct ptl_eq *ptl_eq;
	struct spdk_rdma_utils_mem_map *mem_map;
	/**
	* This is where we keep for accounting purposes the
	* coarse grain memory allocations of SPDK
	*/
	struct ptl_pd_mem_desc_map *mem_desc_map;
	struct ptl_pd_mem_desc_map_ops ops;
	bool in_use;
};




struct ptl_pd *ptl_pd_create(struct ptl_context *ptl_context, struct ptl_pd_mem_desc_map_ops *ops);

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
#endif
