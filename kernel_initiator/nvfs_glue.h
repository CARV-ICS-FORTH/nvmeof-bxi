#ifndef NVFS_GLUE_H
#define NVFS_GLUE_H

#include <linux/blk-mq.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

struct nvfs_rdma_info; /*opaque; only even pointed to here*/

/* MUST match gds-nvidia-fs src/nvfs-dma.h "struct nvfs_dma_rw_ops" (v1 ABI)
 * field-for-field. nvidia-fs fills this and hands us a pointer at
 * registration; we only read it. */

struct nvfs_dma_rw_ops {
  unsigned long long ft_bmap;
  int (*nvfs_blk_rq_map_sg)(struct request_queue *q, struct request *req,
                            struct scatterlist *sglist);
  int (*nvfs_dma_map_sg_attrs)(struct device *device,
                               struct scatterlist *sglist, int nents,
                               enum dma_data_direction dma_dir,
                               unsigned long attrs);
  int (*nvfs_dma_unmap_sg)(struct device *device, struct scatterlist *sglist,
                           int nents, enum dma_data_direction dma_dir);
  bool (*nvfs_is_gpu_page)(struct page *page);
  unsigned int (*nvfs_gpu_index)(struct page *page);
  unsigned int (*nvfs_device_priority)(struct device *dev,
                                       unsigned int gpu_index);
  int (*nvfs_get_gpu_sglist_rdma_info)(struct scatterlist *sglist, int nents,
                                       struct nvfs_rdma_info *rdma_infop);
};

/* feature bits (subset of nvfs-dma.h enum ft_bits) that we depend on */
#define NVFS_FT_PREP_SGLIST (1ULL << 0)
#define NVFS_FT_MAP_SGLIST (1ULL << 1)
#define NVFS_FT_IS_GPU_PAGE (1ULL << 2)

/* Slot probed by stock nvidia-fs (nvfs-dma.c modules_list "nvme_rdma_v1"). */
int nvme_rdma_v1_register_nvfs_dma_ops(struct nvfs_dma_rw_ops *ops);
void nvme_rdma_v1_unregister_nvfs_dma_ops(void);

/* Runtime accessor: returns true and takes a ref iff nvidia-fs is registered.
 */
bool nvfs_get_ops(void);
void nvfs_put_ops(void);
extern struct nvfs_dma_rw_ops *nvfs_ops;

#endif
