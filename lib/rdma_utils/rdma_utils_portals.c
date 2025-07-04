/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (c) Intel Corporation. All rights reserved.
 *   Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#include "../rdma_provider/ptl_cm_id.h"
#include "../rdma_provider/ptl_config.h"
#include "../rdma_provider/ptl_context.h"
#include "../rdma_provider/ptl_cq.h"
#include "../rdma_provider/ptl_log.h"
#include "../rdma_provider/ptl_pd.h"
#include "../rdma_provider/ptl_uuid.h"
#include "spdk/file.h"
#include "spdk/likely.h"
#include "spdk/log.h"
#include "spdk/net.h"
#include "spdk/string.h"
#include "spdk/util.h"
#include "spdk_internal/assert.h"
#include "spdk_internal/rdma_utils.h"
#include <portals4.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#define PTL_SIZE_MAX ((1UL<<48)-1)
struct rdma_utils_device {
	struct ibv_pd			*pd;
	struct ibv_context		*context;
	int				ref;
	bool				removed;
	TAILQ_ENTRY(rdma_utils_device)	tailq;
};

struct spdk_rdma_utils_mem_map {
	struct spdk_mem_map			*map;
	struct ibv_pd				*pd;
	struct spdk_nvme_rdma_hooks		*hooks;
	uint32_t				ref_count;
	uint32_t				access_flags;
	LIST_ENTRY(spdk_rdma_utils_mem_map)	link;
};

struct rdma_utils_memory_domain {
	TAILQ_ENTRY(rdma_utils_memory_domain) link;
	uint32_t ref;
	enum spdk_dma_device_type type;
	struct ibv_pd *pd;
	struct spdk_memory_domain *domain;
	struct spdk_memory_domain_rdma_ctx rdma_ctx;
};

static pthread_mutex_t g_dev_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct ibv_context **g_ctx_list = NULL;
static TAILQ_HEAD(, rdma_utils_device) g_dev_list = TAILQ_HEAD_INITIALIZER(g_dev_list);

static LIST_HEAD(, spdk_rdma_utils_mem_map) g_rdma_utils_mr_maps = LIST_HEAD_INITIALIZER(
		&g_rdma_utils_mr_maps);
static pthread_mutex_t g_rdma_mr_maps_mutex = PTHREAD_MUTEX_INITIALIZER;

static TAILQ_HEAD(, rdma_utils_memory_domain) g_memory_domains = TAILQ_HEAD_INITIALIZER(
		g_memory_domains);
static pthread_mutex_t g_memory_domains_lock = PTHREAD_MUTEX_INITIALIZER;

static void spdk_ptl_print_access_flags(uint32_t access_flags)
{
	SPDK_PTL_INFO("IBV Access Flags (0x%x):", access_flags);

	struct {
		uint32_t flag;
		const char *name;
	} flag_names[] = {
		{IBV_ACCESS_LOCAL_WRITE, "IBV_ACCESS_LOCAL_WRITE"},
		{IBV_ACCESS_REMOTE_WRITE, "IBV_ACCESS_REMOTE_WRITE"},
		{IBV_ACCESS_REMOTE_READ, "IBV_ACCESS_REMOTE_READ"},
		{IBV_ACCESS_REMOTE_ATOMIC, "IBV_ACCESS_REMOTE_ATOMIC"},
		{IBV_ACCESS_MW_BIND, "IBV_ACCESS_MW_BIND"},
		{IBV_ACCESS_ZERO_BASED, "IBV_ACCESS_ZERO_BASED"},
		{IBV_ACCESS_ON_DEMAND, "IBV_ACCESS_ON_DEMAND"},
#ifdef IBV_ACCESS_RELAXED_ORDERING
		{IBV_ACCESS_RELAXED_ORDERING, "IBV_ACCESS_RELAXED_ORDERING"},
#endif
#ifdef IBV_ACCESS_OPTIONAL_FIRST
		{IBV_ACCESS_OPTIONAL_FIRST, "IBV_ACCESS_OPTIONAL_FIRST"},
#endif
		{0, NULL}
	};

	bool found = false;
	for (int i = 0; flag_names[i].name != NULL; i++) {
		if (access_flags & flag_names[i].flag) {
			if (found) {
				SPDK_PTL_INFO(" | ");
			}
			SPDK_PTL_INFO("%s", flag_names[i].name);
			found = true;
		}
	}

	if (!found) {
		SPDK_PTL_INFO("No access flags set");
	}
}

static inline bool rdma_utils_ptl_is_local_write(uint32_t access_flags)
{
	return !!(access_flags & IBV_ACCESS_LOCAL_WRITE);
}

static inline bool rdma_utils_ptl_is_remote_write(uint32_t access_flags)
{
	return !!(access_flags & IBV_ACCESS_REMOTE_WRITE);
}

static inline bool rdma_utils_ptl_is_remote_read(uint32_t access_flags)
{
	return !!(access_flags & IBV_ACCESS_REMOTE_READ);
}

static inline bool rdma_utils_ptl_is_remote_atomic(uint32_t access_flags)
{
	bool ret =  !!(access_flags & IBV_ACCESS_REMOTE_ATOMIC);
	if (ret) {
		SPDK_PTL_FATAL("Sorry unsupported atomic staff yet");
	}
	return ret;
}

static inline bool rdma_utils_ptl_is_remote_access_mw_bind(uint32_t access_flags)
{
	bool ret =  !!(access_flags & IBV_ACCESS_MW_BIND);
	if (ret) {
		SPDK_PTL_FATAL("Unsupported staff yet");
	}
	return ret;
}

static inline bool rdma_utils_ptl_is_access_zero_based(uint32_t access_flags)
{
	bool ret = !!(access_flags & IBV_ACCESS_ZERO_BASED);
	if (ret) {
		SPDK_PTL_FATAL("Unsupported staff yet");
	}
	return ret;
}

static int
rdma_utils_ptl_clean_mem_desc(struct ptl_pd_mem_desc *ptl_pd_mem_desc)
{
	int rc;
	if (ptl_pd_mem_desc->local_write == false) {
		goto remote;
	}
	SPDK_PTL_DEBUG("Cleaning up local write staff from ptl_pd_mem_desc %p", ptl_pd_mem_desc);

	rc = PtlCTFree(ptl_pd_mem_desc->local_w_mem_desc.ct_handle);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("Error freeing counting event with error code: %d\n", rc);
	}
	rc = PtlMDRelease(ptl_pd_mem_desc->local_w_mem_handle);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("Failed with code: %d", rc);
	}
remote:
	if (ptl_pd_mem_desc->remote_write || ptl_pd_mem_desc->remote_read) {
		return 0;
	}

	SPDK_PTL_DEBUG("Cleaning up also remote read/write memory areas");
	rc = PtlCTFree(ptl_pd_mem_desc->remote_rw_ct_handle);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("Error freeing counting event with error code: %d\n", rc);
	}
	rc = PtlMEUnlink(ptl_pd_mem_desc->remote_rw_mem_handle);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("Error freeing memory entry with error code: %d\n", rc);
	}
	return 0;
}

static int
rdma_utils_mem_notify(void *cb_ctx, struct spdk_mem_map *map,
		      enum spdk_mem_map_notify_action action,
		      void *vaddr, size_t size)
{
	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	struct ptl_pd *ptl_pd;
	struct spdk_rdma_utils_mem_map *rmap = cb_ctx;
	struct ibv_pd *pd = rmap->pd;
	struct ibv_mr *mr;
	struct ptl_context * ptl_context;
	struct ptl_pd_mem_desc * ptl_pd_mem_desc  = NULL;
	int rc = -1;
	int ret;
	int access_flags;

	SPDK_PTL_DEBUG("RDMAUTILSPTL: Registering memory vaddr is: %p size is: %lu action is: %d", vaddr,
		       size, action);

	if (rmap->hooks && (rmap->hooks->put_rkey || rmap->hooks->get_rkey ||
			    rmap->hooks->get_ibv_pd)) {
		SPDK_PTL_FATAL(
			"Sorry custom hooks are not YET supported in SPDK PORTALS.");
	}

	/*Check first for known unsuported yet features*/
	access_flags = rmap->access_flags;
	rdma_utils_ptl_is_remote_atomic(access_flags);
	rdma_utils_ptl_is_remote_access_mw_bind(access_flags);
	rdma_utils_ptl_is_access_zero_based(access_flags);

	ptl_pd = ptl_pd_get_from_ibv_pd(rmap->pd);
	ptl_context = ptl_cnxt_get_from_ibvpd(rmap->pd);
	spdk_ptl_print_access_flags(access_flags);

	switch (action) {
	case SPDK_MEM_MAP_NOTIFY_REGISTER:
		if (rmap->hooks && rmap->hooks->get_rkey) {
			/*gesalous, impossible path*/
			SPDK_PTL_FATAL("Impossible path");
			rc = spdk_mem_map_set_translation(
				     map, (uint64_t)vaddr, size,
				     rmap->hooks->get_rkey(pd, vaddr, size));
			break;
		}
		access_flags = rmap->access_flags;
#ifdef IBV_ACCESS_OPTIONAL_FIRST
		SPDK_PTL_DEBUG("CAUTION Unsupported staff XXX TODO XXX, ignore");
		access_flags |= IBV_ACCESS_RELAXED_ORDERING;
#endif
		/*Check first if a ptl_pd_mem_desc has been already created for this vaddr*/
		ptl_pd_mem_desc = calloc(1UL, sizeof(*ptl_pd_mem_desc));

		if (rdma_utils_ptl_is_local_write(access_flags)) {
			SPDK_PTL_DEBUG("IBV_LOCAL_WRITE requested calling PtlMDBind()");
			/* Portals staff follows*/
			ptl_pd_mem_desc->local_w_mem_desc.start = vaddr;
			ptl_pd_mem_desc->local_w_mem_desc.options = 0;
			ptl_pd_mem_desc->local_w_mem_desc.length = size;
			ptl_pd_mem_desc->local_w_mem_desc.eq_handle = ptl_cq_get_static_event_queue();
			rc = PtlCTAlloc(ptl_cnxt_get_ni_handle(ptl_context), &ptl_pd_mem_desc->local_w_mem_desc.ct_handle);
			if (PTL_OK != rc) {
				SPDK_PTL_FATAL("Failed to allocate a counting event");
			}

			ret = PtlMDBind(ptl_cnxt_get_ni_handle(ptl_context), &ptl_pd_mem_desc->local_w_mem_desc,
					&ptl_pd_mem_desc->local_w_mem_handle);
			if (PTL_OK != ret) {
				SPDK_PTL_FATAL("Failed to register virtual addr %p of size: %lu",
					       vaddr, size);
			}
			ptl_pd_mem_desc->local_write = true;
		}

		if (false == rdma_utils_ptl_is_remote_write(access_flags) &&
		    false == rdma_utils_ptl_is_remote_read(access_flags)) {
			SPDK_PTL_DEBUG("No remote READ or WRITE requested for the memory bye bye");
			goto done;

		}
		SPDK_PTL_DEBUG("Memory registration for RMA operations requested....exposing the whole address space");
		memset(&ptl_pd_mem_desc->remote_wr_me, 0x00, sizeof(ptl_pd_mem_desc->remote_wr_me));
		ptl_pd_mem_desc->remote_wr_me.ignore_bits = PTL_UUID_IGNORE_MASK;
		// ptl_pd_mem_desc->remote_wr_me.match_bits = ptl_uuid_set_op_type(PTL_UUID_IGNORE_MASK, PTL_RMA);
		ptl_pd_mem_desc->remote_wr_me.match_bits = PTL_UUID_RMA_MASK;
		ptl_pd_mem_desc->remote_wr_me.match_id.phys.nid = PTL_NID_ANY;
		ptl_pd_mem_desc->remote_wr_me.match_id.phys.pid = PTL_PID_ANY;
		ptl_pd_mem_desc->remote_wr_me.min_free = 0;
		ptl_pd_mem_desc->remote_wr_me.start = NULL;
		ptl_pd_mem_desc->remote_wr_me.length = PTL_SIZE_MAX;
		ptl_pd_mem_desc->remote_wr_me.uid = PTL_UID_ANY;

		ptl_pd_mem_desc->remote_read = rdma_utils_ptl_is_remote_read(access_flags);
		ptl_pd_mem_desc->remote_write = rdma_utils_ptl_is_remote_write(access_flags);

		/*Create and associate counting events*/
		// ret = PtlCTAlloc(ptl_cnxt_get_ni_handle(ptl_cnxt), &ptl_pd_mem_desc->remote_rw_ct_handle);
		// if (ret != PTL_OK) {
		// 	SPDK_PTL_FATAL("Failed to allocate counting event");
		// }
		// ptl_pd_mem_desc->remote_wr_me.ct_handle = ptl_pd_mem_desc->remote_rw_ct_handle;
		ptl_pd_mem_desc->remote_wr_me.ct_handle = PTL_CT_NONE;
		ptl_pd_mem_desc->remote_wr_me.options = PTL_RMA_ME_OPTS;
		if (ptl_pd_mem_desc->remote_read) {
			SPDK_PTL_DEBUG("Enabling READ access for the remote region as requested");
			ptl_pd_mem_desc->remote_wr_me.options     |= PTL_ME_OP_GET;
		}
		if (ptl_pd_mem_desc->remote_write) {
			SPDK_PTL_DEBUG("Enabling WRITE access for the remote region as requested");
			ptl_pd_mem_desc->remote_wr_me.options     |= PTL_ME_OP_PUT;
		}
		rc = PtlMEAppend(ptl_cnxt_get_ni_handle(ptl_cnxt), PTL_PT_INDEX, &ptl_pd_mem_desc->remote_wr_me,
				 PTL_PRIORITY_LIST, NULL, &ptl_pd_mem_desc->remote_rw_mem_handle);
		if (rc != PTL_OK) {
			SPDK_PTL_FATAL("PtlMEAppend for RMA operations failed with error code: %d", rc);
		}
		SPDK_PTL_INFO("PtlMEAppend for RMA operation is successful!");
		rc = PtlCTAlloc(ptl_cnxt_get_ni_handle(ptl_context), &ptl_pd_mem_desc->remote_wr_me.ct_handle);
		if (PTL_OK != rc) {
			SPDK_PTL_FATAL("Failed to allocate a counting event");
		}

		SPDK_PTL_DEBUG("Remote Memory Access ENABLED. REMOTE_WRITE: %s REMOTE_READ: %s",
			       rdma_utils_ptl_is_remote_write(access_flags) ? "YES" : "NO",
			       rdma_utils_ptl_is_remote_read(access_flags) ? "YES" : "NO");

		/*Vanilla staff*/
		// mr = ibv_reg_mr(pd, vaddr, size, access_flags);
		// if (mr == NULL) {
		//   SPDK_ERRLOG("ibv_reg_mr() failed\n");
		//   return -1;
		// }
		// rc = spdk_mem_map_set_translation(map, (uint64_t)vaddr, size,
		//
		//(uint64_t)mr);
done:
		rc = ptl_pd_mem_desc->local_w_mem_handle.handle ? spdk_mem_map_set_translation(map, (uint64_t)vaddr,
			size,
			(uint64_t)ptl_pd_mem_desc->local_w_mem_handle.handle) : -1;
		if (false == ptl_pd_add_mem_desc(ptl_pd, ptl_pd_mem_desc)) {
			SPDK_PTL_FATAL("Failed to keep memory handle in portals context");
		}
		SPDK_PTL_DEBUG("DONE with memory registration in Portals");
		break;
	case SPDK_MEM_MAP_NOTIFY_UNREGISTER:
		ptl_pd_mem_desc = ptl_pd_get_mem_desc(ptl_pd, (uint64_t)vaddr, 0,
						      rdma_utils_ptl_is_local_write(access_flags),
						      rdma_utils_ptl_is_remote_read(access_flags) || rdma_utils_ptl_is_local_write(access_flags));
		if (ptl_pd_mem_desc == NULL) {
			SPDK_PTL_FATAL("Mem desc not found! (It should have)");
		}

		if (rdma_utils_ptl_clean_mem_desc(ptl_pd_mem_desc)) {
			SPDK_PTL_FATAL("Failed to clean ptl_mem_desc");
		}
		if (rmap->hooks == NULL || rmap->hooks->get_rkey == NULL) {
			mr = (struct ibv_mr *)spdk_mem_map_translate(map, (uint64_t)vaddr, NULL);
			// if (mr) {
			// 	ibv_dereg_mr(mr);
			// }
		}
		rc = spdk_mem_map_clear_translation(map, (uint64_t)vaddr, size);
		memset(ptl_pd_mem_desc, 0x00, sizeof(*ptl_pd_mem_desc));
		SPDK_PTL_DEBUG("Cleaned memory area!");
		break;
	default:
		SPDK_UNREACHABLE();
	}

	return rc;
}

static int
rdma_check_contiguous_entries(uint64_t addr_1, uint64_t addr_2)
{
	SPDK_PTL_DEBUG("XXX TODO XXX Check if the addresses are contiguous");
	/* Two contiguous mappings will point to the same address which is the start of the RDMA MR. */
	return addr_1 == addr_2;
}

const struct spdk_mem_map_ops g_rdma_map_ops = {
	.notify_cb = rdma_utils_mem_notify,
	.are_contiguous = rdma_check_contiguous_entries
};

static void
_rdma_free_mem_map(struct spdk_rdma_utils_mem_map *map)
{
	assert(map);

	if (map->hooks) {
		spdk_free(map);
	} else {
		free(map);
	}
}

struct spdk_rdma_utils_mem_map *
spdk_rdma_utils_create_mem_map(struct ibv_pd *pd, struct spdk_nvme_rdma_hooks *hooks,
			       uint32_t access_flags)
{
	SPDK_PTL_DEBUG("RDMAPTLUTILS: hooks are NULL? %s", hooks ? "NO" : "YES");

	struct spdk_rdma_utils_mem_map *map;
	struct ptl_pd *ptl_pd;

	/*No IWARP support this is PORTALS*/
	// if (pd->context->device->transport_type == IBV_TRANSPORT_IWARP) {
	//               RDMAUTILSPTL_FATAL("No IWARP support this is PORTALS");
	/* IWARP requires REMOTE_WRITE permission for RDMA_READ operation */
	/*access_flags |= IBV_ACCESS_REMOTE_WRITE;*/
	// }
	if (hooks && (hooks->get_rkey || hooks->put_rkey || hooks->get_ibv_pd)) {
		SPDK_PTL_FATAL("Sorry custom spdk_nvme_rdma_hooks not "
			       "supported yet. Contact <gesalous@ics.forth.gr");
	}

	pthread_mutex_lock(&g_rdma_mr_maps_mutex);
	/* Look up existing mem map registration for this pd */
	LIST_FOREACH(map, &g_rdma_utils_mr_maps, link) {
		if (map->pd == pd && map->access_flags == access_flags) {
			map->ref_count++;
			pthread_mutex_unlock(&g_rdma_mr_maps_mutex);
			return map;
		}
	}

	if (hooks) {
		map = spdk_zmalloc(sizeof(*map), 0, NULL, SPDK_ENV_NUMA_ID_ANY, SPDK_MALLOC_DMA);
	} else {
		map = calloc(1, sizeof(*map));
	}
	if (!map) {
		pthread_mutex_unlock(&g_rdma_mr_maps_mutex);
		SPDK_ERRLOG("Memory allocation failed\n");
		return NULL;
	}
	SPDK_PTL_DEBUG("Added custom hooks for the PORTALS case");

	map->pd = pd;
	map->ref_count = 1;
	map->hooks = hooks;
	map->access_flags = access_flags;
	map->map = spdk_mem_map_alloc(0, &g_rdma_map_ops, map);
	if (!map->map) {
		SPDK_ERRLOG("Unable to create memory map\n");
		_rdma_free_mem_map(map);
		pthread_mutex_unlock(&g_rdma_mr_maps_mutex);
		return NULL;
	}
	LIST_INSERT_HEAD(&g_rdma_utils_mr_maps, map, link);

	pthread_mutex_unlock(&g_rdma_mr_maps_mutex);
	SPDK_PTL_DEBUG("Ok created mem_map updated field for PORTALS PD (struct ptl_pd)");
	/*We want to update our ptl_pd object with which mem_map it relates to*/
	ptl_pd = ptl_pd_get_from_ibv_pd(pd);
	ptl_pd_set_mem_map(ptl_pd, map);
	return map;
}

void
spdk_rdma_utils_free_mem_map(struct spdk_rdma_utils_mem_map **_map)
{
	SPDK_PTL_DEBUG("CAUTION XXX TODO XXX ?");
	struct spdk_rdma_utils_mem_map *map;

	if (!_map) {
		return;
	}

	map = *_map;
	if (!map) {
		return;
	}
	*_map = NULL;

	pthread_mutex_lock(&g_rdma_mr_maps_mutex);
	assert(map->ref_count > 0);
	map->ref_count--;
	if (map->ref_count != 0) {
		pthread_mutex_unlock(&g_rdma_mr_maps_mutex);
		return;
	}

	LIST_REMOVE(map, link);
	pthread_mutex_unlock(&g_rdma_mr_maps_mutex);
	if (map->map) {
		spdk_mem_map_free(&map->map);
	}
	_rdma_free_mem_map(map);
}

int spdk_rdma_utils_get_translation(
	struct spdk_rdma_utils_mem_map *map, void *address, size_t length,
	struct spdk_rdma_utils_memory_translation *translation)
{
	uint64_t real_length = length;

	assert(map);
	assert(address);
	assert(translation);
	if (map->hooks && (map->hooks->get_rkey || map->hooks->put_rkey ||
			   map->hooks->get_ibv_pd)) {
		SPDK_PTL_FATAL(
			"Sorry SPDK PORTALS does not YET support custom hooks. As "
			"a result, it cannot support translations based on key");
	}

	if (map->hooks && map->hooks->get_rkey) {
		/*gesalous: not possible path*/
		translation->translation_type = SPDK_RDMA_UTILS_TRANSLATION_KEY;
		translation->mr_or_key.key = spdk_mem_map_translate(
						     map->map, (uint64_t)address, &real_length);
	} else {
		translation->translation_type = SPDK_RDMA_UTILS_TRANSLATION_MR;
		translation->mr_or_key.mr =
			(struct ibv_mr *)spdk_mem_map_translate(
				map->map, (uint64_t)address, &real_length);
		if (spdk_unlikely(!translation->mr_or_key.mr)) {
			SPDK_PTL_FATAL("No translation for ptr %p, size %zu\n",
				       address, length);
			return -EINVAL;
		}
	}

	assert(real_length >= length);
	// SPDK_PTL_DEBUG("Ok retrieved also the translation for ptr: %p and size %zu",address,length);
	return 0;
}

static struct rdma_utils_device *
rdma_add_dev(struct ibv_context *context)
{
	SPDK_PTL_DEBUG("CAUTION UNIMPLEMENTED XXX TODO XXX");
	struct rdma_utils_device *dev;

	dev = calloc(1, sizeof(*dev));
	if (dev == NULL) {
		SPDK_ERRLOG("Failed to allocate RDMA device object.\n");
		return NULL;
	}

	dev->pd = ibv_alloc_pd(context);
	if (dev->pd == NULL) {
		SPDK_ERRLOG("ibv_alloc_pd() failed: %s (%d)\n", spdk_strerror(errno), errno);
		free(dev);
		return NULL;
	}

	dev->context = context;
	dev->ref++;
	TAILQ_INSERT_TAIL(&g_dev_list, dev, tailq);

	return dev;
}

static void
rdma_remove_dev(struct rdma_utils_device *dev)
{
	SPDK_PTL_DEBUG("CAUTION: Default code of rdma_remove_dev XXX TODO XXX");
	if (!dev->removed || dev->ref > 0) {
		return;
	}

	/* Deallocate protection domain only if the device is already removed and
	 * there is no reference.
	 */
	TAILQ_REMOVE(&g_dev_list, dev, tailq);
	ibv_dealloc_pd(dev->pd);
	free(dev);
}

static int
ctx_cmp(const void *_c1, const void *_c2)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	struct ibv_context *c1 = *(struct ibv_context **)_c1;
	struct ibv_context *c2 = *(struct ibv_context **)_c2;

	return c1 < c2 ? -1 : c1 > c2;
}

static int
rdma_sync_dev_list(void)
{
	SPDK_PTL_DEBUG("CAUTION UNIMPLEMENTED XXX TODO XXX");
	struct ibv_context **new_ctx_list;
	int i, j;
	int num_devs = 0;

	/*
	 * rdma_get_devices() returns a NULL terminated array of opened RDMA devices,
	 * and sets num_devs to the number of the returned devices.
	 */
	new_ctx_list = rdma_get_devices(&num_devs);
	if (new_ctx_list == NULL) {
		SPDK_ERRLOG("rdma_get_devices() failed: %s (%d)\n", spdk_strerror(errno), errno);
		return -ENODEV;
	}

	if (num_devs == 0) {
		rdma_free_devices(new_ctx_list);
		SPDK_ERRLOG("Returned RDMA device array was empty\n");
		return -ENODEV;
	}

	/*
	 * Sort new_ctx_list by addresses to update devices easily.
	 */
	qsort(new_ctx_list, num_devs, sizeof(struct ibv_context *), ctx_cmp);

	if (g_ctx_list == NULL) {
		/* If no old array, this is the first call. Add all devices. */
		for (i = 0; new_ctx_list[i] != NULL; i++) {
			rdma_add_dev(new_ctx_list[i]);
		}

		goto exit;
	}

	for (i = j = 0; new_ctx_list[i] != NULL || g_ctx_list[j] != NULL;) {
		struct ibv_context *new_ctx = new_ctx_list[i];
		struct ibv_context *old_ctx = g_ctx_list[j];
		bool add = false, remove = false;

		/*
		 * If a context exists only in the new array, create a device for it,
		 * or if a context exists only in the old array, try removing the
		 * corresponding device.
		 */

		if (old_ctx == NULL) {
			add = true;
		} else if (new_ctx == NULL) {
			remove = true;
		} else if (new_ctx < old_ctx) {
			add = true;
		} else if (old_ctx < new_ctx) {
			remove = true;
		}

		if (add) {
			rdma_add_dev(new_ctx_list[i]);
			i++;
		} else if (remove) {
			struct rdma_utils_device *dev, *tmp;

			TAILQ_FOREACH_SAFE(dev, &g_dev_list, tailq, tmp) {
				if (dev->context == g_ctx_list[j]) {
					dev->removed = true;
					rdma_remove_dev(dev);
				}
			}
			j++;
		} else {
			i++;
			j++;
		}
	}

	/* Free the old array. */
	rdma_free_devices(g_ctx_list);

exit:
	/*
	 * Keep the newly returned array so that allocated protection domains
	 * are not freed unexpectedly.
	 */
	g_ctx_list = new_ctx_list;
	return 0;
}

struct ibv_pd *
spdk_rdma_utils_get_pd(struct ibv_context *context)
{
	struct ptl_context * ptl_context = ptl_cnxt_get_from_ibcnxt(context);
	if (NULL == ptl_context->ptl_pd) {
		SPDK_PTL_DEBUG("PTL PD IS NULLQ creating it");
		ptl_context->ptl_pd = ptl_pd_create(ptl_context);
	}
	return ptl_pd_get_ibv_pd(ptl_context->ptl_pd);
	// struct ptl_context *ptl_context;
	// struct ibv_pd *fake_pd;
	// pthread_mutex_lock(&g_dev_mutex);
	// ptl_context = ptl_cnxt_get_from_ibcnxt(context);
	// fake_pd = ptl_pd_get_ibv_pd(ptl_context);
	// pthread_mutex_unlock(&g_dev_mutex);

	// return fake_pd;
}

void
spdk_rdma_utils_put_pd(struct ibv_pd *pd)
{
	struct ptl_pd *ptl_pd = ptl_pd_get_from_ibv_pd(pd);
	SPDK_PTL_DEBUG("UNIMPLEMENTED XXX TODO XXX, using the default pd is ok ptl_pd: %p", ptl_pd);
	struct rdma_utils_device *dev, *tmp;

	pthread_mutex_lock(&g_dev_mutex);

	TAILQ_FOREACH_SAFE(dev, &g_dev_list, tailq, tmp) {
		if (dev->pd == pd) {
			assert(dev->ref > 0);
			dev->ref--;

			rdma_remove_dev(dev);
		}
	}

	rdma_sync_dev_list();

	pthread_mutex_unlock(&g_dev_mutex);
}

__attribute__((destructor)) static void
_rdma_utils_fini(void)
{
	SPDK_PTL_DEBUG("CAUTION: fini is the default XXX TODO XXX");
	struct rdma_utils_device *dev, *tmp;

	TAILQ_FOREACH_SAFE(dev, &g_dev_list, tailq, tmp) {
		dev->removed = true;
		dev->ref = 0;
		rdma_remove_dev(dev);
	}

	if (g_ctx_list != NULL) {
		rdma_free_devices(g_ctx_list);
		g_ctx_list = NULL;
	}
}

struct spdk_memory_domain *
spdk_rdma_utils_get_memory_domain(struct ibv_pd *pd)
{
	SPDK_PTL_DEBUG("Doing the same steps as the original");
	struct rdma_utils_memory_domain *domain = NULL;
	struct spdk_memory_domain_ctx ctx = {};
	int rc;

	pthread_mutex_lock(&g_memory_domains_lock);

	TAILQ_FOREACH(domain, &g_memory_domains, link) {
		if (domain->pd == pd) {
			domain->ref++;
			pthread_mutex_unlock(&g_memory_domains_lock);
			return domain->domain;
		}
	}

	domain = calloc(1, sizeof(*domain));
	if (!domain) {
		SPDK_ERRLOG("Memory allocation failed\n");
		pthread_mutex_unlock(&g_memory_domains_lock);
		return NULL;
	}

	domain->rdma_ctx.size = sizeof(domain->rdma_ctx);
	domain->rdma_ctx.ibv_pd = pd;
	ctx.size = sizeof(ctx);
	ctx.user_ctx = &domain->rdma_ctx;
	ctx.user_ctx_size = domain->rdma_ctx.size;

	rc = spdk_memory_domain_create(&domain->domain, SPDK_DMA_DEVICE_TYPE_RDMA, &ctx,
				       SPDK_RDMA_DMA_DEVICE);
	if (rc) {
		SPDK_ERRLOG("Failed to create memory domain\n");
		free(domain);
		pthread_mutex_unlock(&g_memory_domains_lock);
		return NULL;
	}

	domain->pd = pd;
	domain->ref = 1;
	TAILQ_INSERT_TAIL(&g_memory_domains, domain, link);

	pthread_mutex_unlock(&g_memory_domains_lock);

	return domain->domain;
}

int
spdk_rdma_utils_put_memory_domain(struct spdk_memory_domain *_domain)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	struct rdma_utils_memory_domain *domain = NULL;

	if (!_domain) {
		return 0;
	}

	pthread_mutex_lock(&g_memory_domains_lock);

	TAILQ_FOREACH(domain, &g_memory_domains, link) {
		if (domain->domain == _domain) {
			break;
		}
	}

	if (!domain) {
		pthread_mutex_unlock(&g_memory_domains_lock);
		return -ENODEV;
	}
	assert(domain->ref > 0);

	domain->ref--;

	if (domain->ref == 0) {
		spdk_memory_domain_destroy(domain->domain);
		TAILQ_REMOVE(&g_memory_domains, domain, link);
		free(domain);
	}

	pthread_mutex_unlock(&g_memory_domains_lock);

	return 0;
}

int32_t
spdk_rdma_cm_id_get_numa_id(struct rdma_cm_id *cm_id)
{

	struct sockaddr	*sa;
	char		addr[64];
	char		ifc[64];
	uint32_t	numa_id;
	int		rc;

	//original
	// sa = rdma_get_local_addr(cm_id);
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(cm_id);
	sa = &ptl_id->fake_cm_id.route.addr.src_addr;
	if (sa == NULL) {
		return SPDK_ENV_NUMA_ID_ANY;
	}
	rc = spdk_net_get_address_string(sa, addr, sizeof(addr));
	if (rc) {
		return SPDK_ENV_NUMA_ID_ANY;
	}
	rc = spdk_net_get_interface_name(addr, ifc, sizeof(ifc));
	if (rc) {
		return SPDK_ENV_NUMA_ID_ANY;
	}
	rc = spdk_read_sysfs_attribute_uint32(&numa_id,
					      "/sys/class/net/%s/device/numa_node", ifc);
	if (rc || numa_id > INT32_MAX) {
		return SPDK_ENV_NUMA_ID_ANY;
	}
	SPDK_PTL_DEBUG("Works the same as in verbs with the appropriate wiring "
		       "NUMA ID is: %d",
		       numa_id);
	return (int32_t)numa_id;
}
