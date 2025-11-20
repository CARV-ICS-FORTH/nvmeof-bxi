/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IB_PORTALS_H
#define _IB_PORTALS_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

/* PD */

struct ib_pd *ib_portals_alloc_pd(struct ib_device *device, gfp_t gfp);
void ib_portals_dealloc_pd(struct ib_pd *pd);

/* CQ */

struct ib_cq *ib_portals_alloc_cq(void *ibdev, void *cq_context, int cqe,
                                  int comp_vector,
                                  enum ib_poll_context poll_ctx);
void ib_portals_free_cq(struct ib_cq *cq);

/* CQ Pool helpers */

struct ib_cq *ib_portals_cq_pool_get(struct ib_device *dev, unsigned int nr_cqe,
                                     int comp_vector_hint,
                                     enum ib_poll_context poll_ctx);
void ib_portals_cq_pool_put(struct ib_cq *cq, int cqe);

/* QP */
int ib_portals_destroy_qp(struct ib_qp *qp);

/* DMA mapping helpers */
void ib_portals_dma_unmap_single(struct ib_device *ibdev, dma_addr_t addr,
                                 size_t size, enum dma_data_direction dir);
dma_addr_t ib_portals_dma_map_single(struct ib_device *ibdev, void *cpu_addr,
                                     size_t size, enum dma_data_direction dir);
int ib_portals_dma_mapping_error(struct ib_device *ibdev, dma_addr_t dma_addr);
int ib_portals_dma_unmap_sg(struct ib_device *ibdev, struct scatterlist *sgl,
                            int nents, enum dma_data_direction dir);

int ib_portals_dma_map_sg(struct ib_device *ibdev, struct scatterlist *sgl,
                          int nents, enum dma_data_direction dir);
void ib_portals_dma_sync_single_for_cpu(struct ib_device *ibdev,
                                        dma_addr_t dma_handle, size_t size,
                                        enum dma_data_direction dir);
void ib_portals_dma_sync_single_for_device(struct ib_device *ibdev,
                                           dma_addr_t dma_handle, size_t size,
                                           enum dma_data_direction dir);

/* Posting send/recv */
int ib_portals_post_send(struct ib_qp *qp, void *wr, void *bad);
int ib_portals_post_recv(struct ib_qp *qp, void *wr, void *bad);

/* CQ processing */
int ib_portals_process_cq_direct(struct ib_cq *cq, int budget);

/* MR helpers */

int ib_portals_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg, int sg_nents,
                         unsigned int *sg_offset, unsigned int page_size);

int ib_portals_map_mr_sg_pi(struct ib_mr *mr, struct scatterlist *data_sg,
                            int data_sg_nents, unsigned int *data_sg_offset,
                            struct scatterlist *meta_sg, int meta_sg_nents,
                            unsigned int *meta_sg_offset,
                            unsigned int page_size);

u32 ib_portals_inc_rkey(u32 rkey);
void ib_portals_update_fast_reg_key(struct ib_mr *mr, u32 rkey);

/* RDMA status helpers */
const char *ib_portals_event_msg(int ev);
const char *ib_portals_wc_status_msg(int status);
int ib_portals_check_mr_status(struct ib_mr *mr, int check, void *status);

/* Client registration */
int ib_portals_register_client(struct ib_client *client);
void ib_portals_unregister_client(struct ib_client *client);

/* Misc */
void ib_portals_drain_qp(struct ib_qp *qp);
int ib_portals_dev_to_node(struct device *dev);

#endif /* _IB_PORTALS_H */
