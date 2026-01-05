#include "ptl_mem_desc.h"
#include "ptl_connection.h"
#include "ptl_context.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_object_types.h"
#include "ptl_uuid.h"

struct ptl_mem_desc *ptl_mem_desc_create_remote(void *start, size_t size, bool remote_read,
		bool remote_write)
{

	int rc;
	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	struct ptl_mem_desc *mem_desc = calloc(1UL, sizeof(*mem_desc));
	mem_desc->obj_type = PTL_MEM_DESC_REMOTE;
	SPDK_PTL_DEBUG("Memory registration for RMA operations requested....exposing the whole address space");
	memset(&mem_desc->remote.remote_wr_me, 0x00, sizeof(mem_desc->remote.remote_wr_me));
	mem_desc->remote.remote_wr_me.ignore_bits = PTL_UUID_IGNORE_MASK;
	// ptl_pd_mem_desc->remote_wr_me.match_bits = ptl_uuid_set_op_type(PTL_UUID_IGNORE_MASK, PTL_RMA);
	mem_desc->remote.remote_wr_me.match_bits = PTL_UUID_RMA_MASK;
	mem_desc->remote.remote_wr_me.match_id.phys.nid = PTL_NID_ANY;
	mem_desc->remote.remote_wr_me.match_id.phys.pid = PTL_PID_ANY;
	mem_desc->remote.remote_wr_me.min_free = 0;
	mem_desc->remote.remote_wr_me.start = start;
	mem_desc->remote.remote_wr_me.length = size;
	mem_desc->remote.remote_wr_me.uid = PTL_UID_ANY;
	/*Create and associate counting events*/
	// ret = PtlCTAlloc(ptl_cnxt_get_ni_handle(ptl_cnxt), &ptl_pd_mem_desc->remote_rw_ct_handle);
	// if (ret != PTL_OK) {
	// 	SPDK_PTL_FATAL("Failed to allocate counting event");
	// }
	// ptl_pd_mem_desc->remote_wr_me.ct_handle = ptl_pd_mem_desc->remote_rw_ct_handle;
	mem_desc->remote.remote_wr_me.ct_handle = PTL_CT_NONE;
	mem_desc->remote.remote_wr_me.options = PTL_RMA_ME_OPTS;
	if (remote_read) {
		SPDK_PTL_DEBUG("Enabling READ access for the remote region as requested");
		mem_desc->remote.remote_wr_me.options     |= PTL_ME_OP_GET;
	}
	if (remote_write) {
		SPDK_PTL_DEBUG("Enabling WRITE access for the remote region as requested");
		mem_desc->remote.remote_wr_me.options     |= PTL_ME_OP_PUT;
	}
	rc = PtlMEAppend(ptl_cnxt_get_ni_handle(ptl_cnxt), PTL_PT_INDEX, &mem_desc->remote.remote_wr_me,
			 PTL_PRIORITY_LIST, NULL, &mem_desc->remote.remote_rw_mem_handle);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("PtlMEAppend for RMA operations failed with error code: %d", rc);
	}
	SPDK_PTL_INFO("PtlMEAppend for RMA operation is successful!");
#if !BXIV3
	rc = PtlCTAlloc(ptl_cnxt_get_ni_handle(ptl_cnxt), &mem_desc->remote.remote_wr_me.ct_handle);
	if (PTL_OK != rc) {
		SPDK_PTL_FATAL("Failed to allocate a counting event");
	}
#endif
	return mem_desc;
}

struct ptl_mem_desc *ptl_mem_desc_create_local(void *vaddr, size_t size, bool bind,
		ptl_handle_eq_t event_queue)
{
	struct ptl_context *ptl_context = ptl_cnxt_get();
	// int rc;
	int ret;
	struct ptl_mem_desc *mem_desc = calloc(1UL, sizeof(*mem_desc));
	mem_desc->obj_type = PTL_MEM_DESC_LOCAL;
	SPDK_PTL_DEBUG("IBV_LOCAL_WRITE requested calling PtlMDBind()");
	/* Portals staff follows*/
	mem_desc->local.local_w_mem_desc.start = vaddr;
	mem_desc->local.local_w_mem_desc.options = 0;
	mem_desc->local.local_w_mem_desc.length = size;

	mem_desc->local.local_w_mem_desc.eq_handle = event_queue;
	/*I do not need counting events for now*/
	// rc = PtlCTAlloc(ptl_cnxt_get_ni_handle(ptl_context), &mem_desc->local.local_w_mem_desc.ct_handle);
	// if (PTL_OK != rc) {
	// 	SPDK_PTL_FATAL("Failed to allocate a counting event");
	// }

	if (bind) {
		ret = PtlMDBind(ptl_cnxt_get_ni_handle(ptl_context), &mem_desc->local.local_w_mem_desc,
				&mem_desc->local.local_w_mem_handle);
		if (PTL_OK != ret) {
			SPDK_PTL_FATAL("Failed to register virtual addr %p of size: %lu reason: %d",
				       vaddr, size, ret);
		}
	}
	mem_desc->is_bind = bind;
	return mem_desc;
}



void ptl_mem_desc_clean(struct ptl_mem_desc *mem_desc)
{
	int rc;
	if (mem_desc->obj_type == PTL_MEM_DESC_LOCAL) {
		SPDK_PTL_DEBUG("PTL_MEM_DESC: Cleaning up *LOCAL* memory descriptor from ptl_pd_mem_desc %p",
			       mem_desc);
		if (mem_desc->is_bind) {
			rc = PtlMDRelease(mem_desc->local.local_w_mem_handle);
			if (rc != PTL_OK) {
				SPDK_PTL_FATAL("Failed with code: %d", rc);
			}
		}
	} else {

		SPDK_PTL_DEBUG("PTL_MEM_DESC: Cleaning up *REMOTE* memory descriptor from ptl_pd_mem_desc %p",
			       mem_desc);

		SPDK_PTL_DEBUG("Cleaning up also remote read/write memory areas");
		// rc = PtlCTFree(mem_desc->remote.remote_rw_ct_handle);
		// if (rc != PTL_OK) {
		// 	SPDK_PTL_FATAL("Error freeing counting event with error code: %d\n", rc);
		// }
		rc = PtlMEUnlink(mem_desc->remote.remote_rw_mem_handle);
		if (rc != PTL_OK) {
			SPDK_PTL_FATAL("Error freeing memory entry with error code: %d\n", rc);
		}
	}
	free(mem_desc);
}

