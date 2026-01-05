#ifndef PTL_MEM_DESC_H
#define PTL_MEM_DESC_H
#include "ptl_object_types.h"
#include <infiniband/verbs.h>
#include <portals4.h>
#include <stdbool.h>

/**
 * @file ptl_mem_desc.h
 * @brief Defines structures and functions for managing Portals 4 memory descriptors.
 *
 * This header provides an abstraction for both local memory descriptors (MDs)
 * used for local operations and remote memory entries (MEs) used for
 * exposing memory for Remote Memory Access (RMA) operations.
 */

struct ptl_mem_desc_local {
	ptl_md_t local_w_mem_desc;
	ptl_handle_md_t local_w_mem_handle;
	struct ibv_mr fake_mr;
	int associated_pte;
};

struct ptl_mem_desc_remote {
	ptl_me_t remote_wr_me;
	ptl_handle_md_t remote_rw_mem_handle;
	ptl_handle_ct_t remote_rw_ct_handle;
	struct ibv_mr fake_mr;
};


struct ptl_mem_desc {
	ptl_obj_type_e obj_type;
	bool is_bind;
	union {
		struct ptl_mem_desc_local local;
		struct ptl_mem_desc_remote remote;
	};
};


struct ptl_mem_desc *ptl_mem_desc_create_local(void *vaddr, size_t size, bool bind,
		ptl_handle_eq_t eq);

struct ptl_mem_desc *ptl_mem_desc_create_remote(void *start, size_t size, bool remote_read,
		bool remote_write);

void ptl_mem_desc_clean(struct ptl_mem_desc *mem_desc);
#endif

