#include "ptl_pd.h"
#include "ptl_log.h"
#include "ptl_mem_desc.h"
#include "ptl_object_types.h"

struct ptl_pd_mem_desc_map {
	struct ptl_mem_desc **entries;
	uint32_t num_entries;
	uint32_t max_entries;
};

struct ptl_pd_mem_desc_map *ptl_pd_map_create(uint32_t num_entries)
{
	SPDK_PTL_DEBUG("Creating mem_desc_map");
	struct ptl_pd_mem_desc_map *map = calloc(1UL, sizeof(struct ptl_pd_mem_desc_map));
	map->entries = calloc(num_entries, sizeof(struct ptl_mem_desc *));
	map->max_entries = num_entries;
	return map;
}

static bool ptl_pd_map_add(struct ptl_pd_mem_desc_map *map, struct ptl_mem_desc *mem_desc)
{
	if (NULL == map) {
		SPDK_PTL_FATAL("Mem desc map is NULL");
	}
	if (map->num_entries >= map->max_entries) {
		SPDK_PTL_FATAL("Sorry no room to add another portals memory descriptor");
		return false;
	}
	map->entries[map->num_entries++] = mem_desc;
	return true;
}

static struct ptl_mem_desc *ptl_pd_map_get(struct ptl_pd_mem_desc_map *map, uint64_t address,
		size_t length,
		bool is_remote_operation)
{
	uint32_t i;
	uint64_t end_address = address + length;

	if (is_remote_operation) {
		goto remote;
	}

	for (i = 0; i < map->num_entries; i++) {
		if ((uint64_t)map->entries[i]->local.local_w_mem_desc.start <= address &&
		    (uint64_t)end_address <= (uint64_t)map->entries[i]->local.local_w_mem_desc.start +
		    map->entries[i]->local.local_w_mem_desc.length) {
			SPDK_PTL_DEBUG("Found *LOCAL* mem desc for portals!");
			return map->entries[i];
		}
	}
	SPDK_PTL_FATAL("OOPSIE! local memory descriptor for write operation not found!");
	return NULL;
remote:
	for (i = 0; i < map->num_entries; i++) {
		if ((uint64_t)map->entries[i]->remote.remote_wr_me.start <= address &&
		    (uint64_t)end_address <= (uint64_t)map->entries[i]->remote.remote_wr_me.start +
		    map->entries[i]->local.local_w_mem_desc.length) {
			SPDK_PTL_DEBUG("Found *REMOTE* mem desc for portals!");
			return map->entries[i];
		}
	}
	SPDK_PTL_FATAL("OOPSIE! remote memory descriptor for write operation not found!");
	return NULL;
}

static bool ptl_pd_map_destroy(struct ptl_pd_mem_desc_map *mem_desc_map)
{
	SPDK_PTL_FATAL("Sorry unimplemented XXX TODO XXX");
	return false;
}


struct ptl_pd *ptl_pd_create(struct ptl_context *ptl_context, struct ptl_pd_mem_desc_map_ops * ops)
{
	struct ptl_pd *ptl_pd = calloc(1UL, sizeof(*ptl_pd));
	if (NULL == ptl_pd) {
		SPDK_PTL_FATAL("Failed to allocate memory for portalds pd");
	}
	ptl_pd->object_type = PTL_PD;
	ptl_pd->ptl_cnxt = ptl_context;

	if (NULL == ops) {
		ptl_pd->ops.create = ptl_pd_map_create;
		ptl_pd->ops.add = ptl_pd_map_add;
		ptl_pd->ops.get = ptl_pd_map_get;
		ptl_pd->ops.destroy = ptl_pd_map_destroy;
	}
	return ptl_pd;
}



