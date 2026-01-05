#include "ptl_rte_hash_map.h"
#include "ptl_cq.h"
#include "ptl_config.h"
#include "ptl_log.h"
#include "ptl_mem_desc.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include <rte_hash.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define HUGEPAGE_2M_SIZE   (2ULL * 1024 * 1024)
#define HUGEPAGE_2M_MASK   (~(HUGEPAGE_2M_SIZE - 1ULL))


struct ptl_pd_mem_desc_map {
	struct rte_hash *hash_map;
	uint32_t num_entries;
};


static inline uint64_t ptl_rte_hugepage_2m_start(uint64_t addr)
{
	return addr & HUGEPAGE_2M_MASK;
}

struct ptl_pd_mem_desc_map *ptl_rte_map_create(uint32_t num_entries, const char *name)
{
	struct ptl_pd_mem_desc_map *map = calloc(1UL, sizeof(*map));
	struct rte_hash_parameters hash_params = {
		.name = name,
		.entries = 4096,
		.key_len = sizeof(uint64_t), // Example: key is a 64-bit integer
		.hash_func = NULL,
		.hash_func_init_val = 0,
		.socket_id = rte_socket_id(),
		.extra_flag = RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY, // Or RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY for multi-writer
	};

	// 3. Create the Hash Table

	map->hash_map = rte_hash_create(&hash_params);
	if (map->hash_map == NULL) {
		SPDK_PTL_FATAL("Failed to create rte_hash map");
	}

	return map;
}



bool ptl_rte_map_add(struct ptl_pd_mem_desc_map *map, struct ptl_mem_desc *mem_desc)
{

	if (NULL == map) {
		SPDK_PTL_FATAL("Mem desc map is NULL");
	}
	if (mem_desc->obj_type == PTL_MEM_DESC_REMOTE) {
		rte_hash_add_key_data(map->hash_map, &mem_desc->remote.remote_wr_me.start, mem_desc);
		return true;
	}

	SPDK_PTL_DEBUG("RTE_MAP: Registering memory start: %lu length: %lu is it remote? %s",
		       (size_t)mem_desc->local.local_w_mem_desc.start,
		       mem_desc->local.local_w_mem_desc.length, (mem_desc->obj_type == PTL_MEM_DESC_LOCAL) ? "YES" : "NO");

	for (size_t address = (size_t)mem_desc->local.local_w_mem_desc.start;
	     address < (size_t)mem_desc->local.local_w_mem_desc.start + mem_desc->local.local_w_mem_desc.length;
	     address += HUGEPAGE_2M_SIZE) {
		rte_hash_add_key_data(map->hash_map, &address, mem_desc);
		SPDK_PTL_DEBUG("Adding MD starting at: %lu", address);
	}
	return true;
}


struct ptl_mem_desc *ptl_rte_map_get(struct ptl_pd_mem_desc_map *map, uint64_t address,
				     size_t length,
				     bool is_remote_operation)
{
	struct ptl_mem_desc *mem_desc;
	uint64_t start_address = ptl_rte_hugepage_2m_start(address);

	int rc = rte_hash_lookup_data(map->hash_map, &start_address, (void **)&mem_desc);
	if (rc >= 0) {
		return mem_desc;
	}
	SPDK_PTL_FATAL("Memory descriptor not found! for address %lu and starting address: %lu", address,
		       start_address);
}

bool ptl_rte_map_destroy(struct ptl_pd_mem_desc_map *mem_desc_map)
{
	SPDK_PTL_FATAL("Unimplemented");
	return false;
}

