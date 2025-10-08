#ifndef PTL_RTE_HASH_MAP_H
#define PTL_RTE_HASH_MAP_H

#include "ptl_pd.h"
#include <stdbool.h>
#include <stdint.h>
struct ptl_pd_mem_desc;
struct ptl_pd_mem_desc_map;

struct ptl_pd_mem_desc_map *ptl_rte_map_create(uint32_t num_entries);

bool ptl_rte_map_add(struct ptl_pd_mem_desc_map *map, struct ptl_pd_mem_desc *mem_desc);

struct ptl_pd_mem_desc *ptl_rte_map_get(struct ptl_pd_mem_desc_map *map, uint64_t address,
					size_t length,
					bool is_remote_operation);

bool ptl_rte_map_destroy(struct ptl_pd_mem_desc_map *mem_desc_map);
#endif
