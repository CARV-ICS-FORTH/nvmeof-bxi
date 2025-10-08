#include "ptl_rte_hash_map.h"
#include "ptl_log.h"
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

struct ptl_pd_mem_desc_map *ptl_rte_map_create(uint32_t num_entries)
{
	struct ptl_pd_mem_desc_map *map = calloc(1UL, sizeof(*map));
	struct rte_hash_parameters hash_params = {
		.name = "mem_desc_map",
		.entries = 1024,
		.key_len = sizeof(uint64_t), // Example: key is a 64-bit integer
		.hash_func = NULL,
		.hash_func_init_val = 0,
		.socket_id = rte_socket_id(),
		.extra_flag = 0, // Or RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY for multi-writer
	};

	// 3. Create the Hash Table

	map->hash_map = rte_hash_create(&hash_params);
	if (map->hash_map == NULL) {
		SPDK_PTL_FATAL("Failed to create rte_hash map");
	}

	return map;
}

bool ptl_rte_map_add(struct ptl_pd_mem_desc_map *map, struct ptl_pd_mem_desc *mem_desc)
{
	if (NULL == map) {
		SPDK_PTL_FATAL("Mem desc map is NULL");
	}
	SPDK_PTL_DEBUG("Registering memory start: %lu length: %lu", mem_desc->local_w_mem_desc.start,
		       mem_desc->local_w_mem_desc.length);

	for (size_t address = (size_t)mem_desc->local_w_mem_desc.start;
	     address < (size_t)mem_desc->local_w_mem_desc.start + mem_desc->local_w_mem_desc.length;
	     address += HUGEPAGE_2M_SIZE) {
		rte_hash_add_key_data(map->hash_map, &address, mem_desc);
		SPDK_PTL_DEBUG("Adding MD starting at: %lu", address);
	}

	return true;
}

struct ptl_pd_mem_desc *ptl_rte_map_get(struct ptl_pd_mem_desc_map *map, uint64_t address,
					size_t length,
					bool is_remote_operation)
{
	struct ptl_pd_mem_desc *mem_desc;
	uint64_t start_address = ptl_rte_hugepage_2m_start(address);

	int rc = rte_hash_lookup_data(map->hash_map, &start_address, (void **)&mem_desc);
	if (rc < 0) {
		SPDK_PTL_FATAL("Cannot find start address: %lu of address: %lu", start_address, address);
	}
	return mem_desc;
}

bool ptl_rte_map_destroy(struct ptl_pd_mem_desc_map *mem_desc_map)
{
	SPDK_PTL_FATAL("Unimplemented");
	return false;
}
