// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/err.h>

#define GES_UNIMPL_RATELIMIT_PERIOD  HZ
#define GES_UNIMPL_RATELIMIT_BURST   10

static DEFINE_RATELIMIT_STATE(ges_unimpl_rs, GES_UNIMPL_RATELIMIT_PERIOD, GES_UNIMPL_RATELIMIT_BURST);

#define IB_PORTALS4_UNIMPL(fmt, ...)    \
    do {    \
    if (__ratelimit(&ges_unimpl_rs))    \
    pr_warn("Portals4/ib: UNIMPLEMENTED: %s: " fmt "\n",    \
    __func__, ##__VA_ARGS__);    \
    } while (0)

#define IB_PORTALS4_WARN_ONCE(fmt, ...)    \
    do {    \
    static bool __once;    \
    if (!__once) {    \
    __once = true;    \
    pr_warn("Portals4/ib: %s: " fmt "\n", __func__, ##__VA_ARGS__); \
    WARN_ON(1);    \
    }    \
    } while (0)


/* Mapped to ib_alloc_pd/ib_dealloc_pd */
struct ib_portals_pd *ib_portals_alloc_pd(void *ibdev, gfp_t gfp)
{
  (void)ibdev;
  (void)gfp;
  IB_PORTALS4_UNIMPL("Sorry!");
  return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(ib_portals_alloc_pd);



void ib_portals_dealloc_pd(struct ib_pd *pd)
{
    (void)pd;
    IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_dealloc_pd);

/* CQ */
struct ib_cq *ib_portals_alloc_cq(void *ibdev, void *cq_context, int cqe, int comp_vector)
{
    (void)ibdev;
    (void)cq_context;
    (void)cqe;
    (void)comp_vector;
    IB_PORTALS4_UNIMPL("Sorry!");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(ib_portals_alloc_cq);

void ib_portals_free_cq(struct ib_cq *cq)
{
    (void)cq;
    IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_free_cq);

/* Some code uses a CQ pool helper; make them no-ops compatible */
struct ib_cq *ib_portals_cq_pool_get(void *ibdev, int cqe, int comp_vector)
{
    (void)ibdev;
    (void)cqe;
    (void)comp_vector;
    IB_PORTALS4_UNIMPL("Sorry!");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(ib_portals_cq_pool_get);


void ib_portals_cq_pool_put(struct ib_cq *cq, int cqe)
{
    (void)cq;
    (void)cqe;
    IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_cq_pool_put);

/* QP + MR pools */
int ib_portals_mr_pool_init(struct ib_qp *qp, void *pool, int n, int max_sge, bool sig)
{
    (void)qp;
    (void)pool;
    (void)n;
    (void)max_sge;
    (void)sig;
    IB_PORTALS4_UNIMPL("Sorry!");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_init);

void ib_portals_mr_pool_destroy(struct ib_qp *qp, void *pool)
{
    (void)qp;
    (void)pool;
    IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_destroy);

void ib_portals_mr_pool_put(struct ib_qp *qp, void *pool, struct ib_mr *mr)
{
    (void)qp;
    (void)pool;
    (void)mr;
    IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_put);

struct ib_mr *ib_portals_mr_pool_get(struct ib_qp *qp, void *pool)
{
    (void)qp;
    (void)pool;
    IB_PORTALS4_UNIMPL("Sorry!");
    return ERR_PTR(-EOPNOTSUPP);
}
EXPORT_SYMBOL_GPL(ib_portals_mr_pool_get);

/* QP */
int ib_portals_destroy_qp(struct ib_qp *qp)
{
    (void)qp;
    IB_PORTALS4_UNIMPL("Sorry!");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_destroy_qp);

/* DMA helpers (pretend success) */
void ib_portals_dma_unmap_single(void *ibdev, dma_addr_t addr, size_t size, enum dma_data_direction dir) 
{ 
    (void)ibdev;
    (void)addr;
    (void)size;
    (void)dir;
    IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_dma_unmap_single);

dma_addr_t ib_portals_dma_map_single(void *ibdev, void *cpu_addr, size_t size, enum dma_data_direction dir)
{
    (void)ibdev;
    (void)cpu_addr;
    (void)size;
    (void)dir;
    IB_PORTALS4_UNIMPL("Sorry!");
    return (dma_addr_t)0;
}
EXPORT_SYMBOL_GPL(ib_portals_dma_map_single);

int ib_portals_dma_mapping_error(void *ibdev, dma_addr_t dma_addr)
{
    (void)ibdev;
    (void)dma_addr;
    IB_PORTALS4_UNIMPL("Sorry!");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_dma_mapping_error);

int ib_portals_dma_unmap_sg(void *ibdev, struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
    (void)ibdev;
    (void)sgl;
    (void)nents;
    (void)dir;
    IB_PORTALS4_UNIMPL("Sorry!");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_dma_unmap_sg);

int ib_portals_dma_map_sg(void *ibdev, struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
    (void)ibdev;
    (void)sgl;
    (void)nents;
    (void)dir;
    IB_PORTALS4_UNIMPL("Sorry!");
    return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_dma_map_sg);

void ib_portals_dma_sync_single_for_cpu(void *ibdev, dma_addr_t dma_handle, size_t size, enum dma_data_direction dir) 
{ 
  (void)ibdev;
  (void)dma_handle;
  (void)size;
  (void)dir;
  IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_dma_sync_single_for_cpu);

void ib_portals_dma_sync_single_for_device(void *ibdev, dma_addr_t dma_handle, size_t size, enum dma_data_direction dir)
{
  (void)ibdev;
  (void)dma_handle;
  (void)size;
  (void)dir;
  IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_dma_sync_single_for_device);

/* Posting send/recv: always succeed, no CQEs are generated here */
int ib_portals_post_send(struct ib_qp *qp, void *wr, void *bad)
{
  (void)qp;
  (void)wr;
  (void)bad;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_post_send);

int ib_portals_post_recv(struct ib_qp *qp, void *wr, void *bad)
{
  (void)qp;
  (void)wr;
  (void)bad;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_post_recv);

/* Completion processing: return 0 or a benign value */
int ib_portals_process_cq_direct(struct ib_cq *cq, int budget)
{
  (void)cq;
  (void)budget;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_process_cq_direct);

/* MR map helpers used in FRWR path */
int ib_portals_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg, int nents, void *sg_offset, size_t *length)
{
  (void)mr;
  (void)sg;
  (void)nents;
  (void)sg_offset;
  (void)length;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_map_mr_sg);

int ib_portals_map_mr_sg_pi(struct ib_mr *mr, struct scatterlist *sg, int nents, void *meta, size_t *length)
{
  (void)mr;
  (void)sg;
  (void)nents;
  (void)meta;
  (void)length;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_map_mr_sg_pi);

u32 ib_portals_inc_rkey(u32 rkey) {

  IB_PORTALS4_UNIMPL("Sorry!");
  return rkey;
}
EXPORT_SYMBOL_GPL(ib_portals_inc_rkey);

void ib_portals_update_fast_reg_key(struct ib_mr *mr, u32 rkey)
{
  (void)mr;
  (void)rkey;
  IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_update_fast_reg_key);

/* Misc helpers seen in rdma.c */
const char *ib_portals_event_msg(int ev) { 
  (void)ev;
  return "IB_IB_PORTALS4_EV"; 
}
EXPORT_SYMBOL_GPL(ib_portals_event_msg);

const char *ib_portals_wc_status_msg(int status) { 
  (void)status;
  return "IB_IB_PORTALS4_WC"; 
}
EXPORT_SYMBOL_GPL(ib_portals_wc_status_msg);

int ib_portals_check_mr_status(struct ib_mr *mr, int check, void *status)
{
  (void)mr;
  (void)check;
  (void)status;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_check_mr_status);

int ib_portals_register_client(void *client)
{
  (void)client;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_register_client);

void ib_portals_unregister_client(void *client)
{
  (void)client;
  IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_unregister_client);

/* Draining */
void ib_portals_drain_qp(struct ib_qp *qp)
{ 
  (void)qp;
  IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_drain_qp);

/* ibdev_to_node(ctrl->device->dev) analog */
int ib_portals_dev_to_node(struct device *dev) { 
  (void)dev;
  return numa_node_id(); 
}
EXPORT_SYMBOL_GPL(ib_portals_dev_to_node);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IB portals implementation over Portals4");
MODULE_AUTHOR("Giorgis Saloustros (Chatzis) gesalous@ics.forth.gr");
