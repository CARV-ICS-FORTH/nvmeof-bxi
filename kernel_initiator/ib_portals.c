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

#include "ib_portals.h"

#define GES_UNIMPL_RATELIMIT_PERIOD  HZ
#define GES_UNIMPL_RATELIMIT_BURST   10

static DEFINE_RATELIMIT_STATE(ges_unimpl_rs, GES_UNIMPL_RATELIMIT_PERIOD, GES_UNIMPL_RATELIMIT_BURST);

#define IB_PORTALS4_UNIMPL(fmt, ...)    \
    do {    \
    if (__ratelimit(&ges_unimpl_rs))    \
    pr_warn("Portals4/ib: UNIMPLEMENTED: %s:%d %s: " fmt "\n",    \
    __FILE__, __LINE__, __func__, ##__VA_ARGS__);    \
    } while (0)

#define IB_PORTALS4_WARN_ONCE(fmt, ...)    \
    do {    \
    static bool __once;    \
    if (!__once) {    \
    __once = true;    \
    pr_warn("Portals4/ib: %s:%d %s: " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    WARN_ON(1);    \
    }    \
    } while (0)


#define IB_PORTALS4_WARN(fmt, ...) \
    pr_warn("%s:%s:%d: " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

// Add near the top of ib_portals.c (after includes)

#include <linux/mutex.h>
#include <linux/list.h>

struct ib_portals_client_entry {
    struct ib_client *client;
    struct list_head node;
};

static LIST_HEAD(ib_portals_client_list);
static DEFINE_MUTEX(ib_portals_client_lock);



/* Mapped to ib_alloc_pd/ib_dealloc_pd */
struct ib_pd *ib_portals_alloc_pd(void *ibdev, gfp_t gfp)
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

struct ib_cq *ib_portals_alloc_cq(void *ibdev, void *cq_context, int cqe, int comp_vector, enum ib_poll_context poll_ctx)
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
struct ib_cq *ib_portals_cq_pool_get(struct ib_device *dev, unsigned int nr_cqe, int comp_vector_hint, 
  enum ib_poll_context poll_ctx)
{
    (void)dev;
    (void)nr_cqe;
    (void)comp_vector_hint;
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

int ib_portals_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg, int sg_nents,unsigned int *sg_offset, unsigned int page_size)
{
  (void)mr;
  (void)sg;
  (void)sg_offset;
  IB_PORTALS4_UNIMPL("Sorry!");
  return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(ib_portals_map_mr_sg);


int ib_portals_map_mr_sg_pi(struct ib_mr *mr, struct scatterlist *data_sg,
		    int data_sg_nents, unsigned int *data_sg_offset,
		    struct scatterlist *meta_sg, int meta_sg_nents,
		    unsigned int *meta_sg_offset, unsigned int page_size)
{
  (void)mr;
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



int ib_portals_register_client(struct  ib_client *client)
{
  struct ib_portals_client_entry *client_entry;

  if (!client) {
      IB_PORTALS4_WARN("Portals4/ib: register_client: NULL ib_client");
      return -EINVAL;
  }

  if (!client->name) {
      IB_PORTALS4_WARN("Portals4/ib: register_client: NULL client name");
      return -EINVAL;
  }
  pr_info("Portals4/ib: register_client name=%s add=%ps remove=%ps get_net_dev_by_params=%ps\n",
            client->name, client->add, client->remove, client->get_net_dev_by_params);

  if (!client->add){
      IB_PORTALS4_WARN("Portals4/ib: client '%s' has no add() callback", client->name);
  }

  if (!client->remove){
      IB_PORTALS4_WARN("Portals4/ib: client '%s' has no remove() callback", client->name);
  }

  client_entry = kzalloc(sizeof(*client_entry), GFP_KERNEL);
  if (!client_entry)
      return -ENOMEM;

  client_entry->client = client;

  mutex_lock(&ib_portals_client_lock);
  list_add_tail(&client_entry->node, &ib_portals_client_list);
  mutex_unlock(&ib_portals_client_lock);

  IB_PORTALS4_WARN("Portals4/ib: registered client '%s'", client->name);
  return 0;
}
EXPORT_SYMBOL_GPL(ib_portals_register_client);


void ib_portals_unregister_client(struct ib_client *client)
{
  struct ib_portals_client_entry *client_entry, *tmp;
  if (!client) {
      IB_PORTALS4_WARN("NULL ib_client");
      return;
  }

  if (!client->name) {
      IB_PORTALS4_WARN("NULL client name");
      return;
  }

  // Semantics: real ib_core would call remove(dev, client_data) for each device
  // still present. Without device tracking yet, just detach from registry.

  mutex_lock(&ib_portals_client_lock);
  list_for_each_entry_safe(client_entry, tmp, &ib_portals_client_list, node) {
  if (client_entry->client == client) {
      if(client_entry->client->remove){
        IB_PORTALS4_UNIMPL("Sorry! I don't know how to call the remove callback");
      }
      list_del(&client_entry->node);
      kfree(client_entry);
      pr_info("Portals4/ib: unregistered client '%s'\n", client->name);
      break;
  }
}
  mutex_unlock(&ib_portals_client_lock);
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
