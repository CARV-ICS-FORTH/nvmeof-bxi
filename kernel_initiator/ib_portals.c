// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

#include <linux/err.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>

#include "asm-generic/errno-base.h"
#include "ib_portals.h"
#include "linux/container_of.h"
#include "linux/types.h"
#include "portals4_bxiext.h"
#include "ptl_bxiv3_dev_map.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cq.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"


#define GES_UNIMPL_RATELIMIT_PERIOD HZ
#define GES_UNIMPL_RATELIMIT_BURST 10

static DEFINE_RATELIMIT_STATE(ges_unimpl_rs, GES_UNIMPL_RATELIMIT_PERIOD,
                              GES_UNIMPL_RATELIMIT_BURST);

#define IB_PORTALS4_UNIMPL(fmt, ...)                                           \
  do {                                                                         \
    if (__ratelimit(&ges_unimpl_rs))                                           \
      pr_warn("Portals4/ib: UNIMPLEMENTED: %s:%d %s: " fmt "\n", __FILE__,     \
              __LINE__, __func__, ##__VA_ARGS__);                              \
    BUG();                                                                     \
  } while (0)

#define IB_PORTALS4_WARN_ONCE(fmt, ...)                                        \
  do {                                                                         \
    static bool __once;                                                        \
    if (!__once) {                                                             \
      __once = true;                                                           \
      pr_warn("Portals4/ib: %s:%d %s: " fmt "\n", __FILE__, __LINE__,          \
              __func__, ##__VA_ARGS__);                                        \
      WARN_ON(1);                                                              \
    }                                                                          \
  } while (0)

#define IB_PORTALS4_WARN(fmt, ...)                                             \
  pr_warn("%s:%s:%d: " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

// Add near the top of ib_portals.c (after includes)
#include <linux/list.h>
#include <linux/mutex.h>

struct ib_portals_client_entry {
	struct ib_client *client;
	struct list_head node;
};

static LIST_HEAD(ib_portals_client_list);
static DEFINE_MUTEX(ib_portals_client_lock);

/* Mapped to ib_alloc_pd/ib_dealloc_pd */
struct ib_pd *ib_portals_alloc_pd(struct ib_device *dev, unsigned int flags)
{
	struct ptl_pd *ptl_pd = ptl_pd_alloc(dev, flags);
	return &ptl_pd->fake_pd;
}

EXPORT_SYMBOL_GPL(ib_portals_alloc_pd);

void ib_portals_dealloc_pd(struct ib_pd *pd)
{
	(void)pd;
	IB_PORTALS4_UNIMPL("Sorry!");
}
EXPORT_SYMBOL_GPL(ib_portals_dealloc_pd);

/* CQ */

struct ib_cq *ib_portals_alloc_cq(void *ibdev, void *cq_context, int cqe,
                                  int comp_vector,
                                  enum ib_poll_context poll_ctx)
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
struct ib_cq *ib_portals_cq_pool_get(struct ib_device *dev, unsigned int nr_cqe,
                                     int comp_vector_hint,
                                     enum ib_poll_context poll_ctx)
{

	struct ptl_cq *ptl_cq;
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(dev, struct ptl_bxiv3_device, fake_ib_dev);
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);

	ptl_cq = ptl_cq_pool_get(bxiv3_dev->ptl_cq_pool, nr_cqe, poll_ctx);
	PTL_DEBUG("Get CQ from the pool successfully ...");
	return &ptl_cq->fake_cq;
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
void ib_portals_dma_unmap_single(struct ib_device *ibdev, dma_addr_t addr,
                                 size_t size, enum dma_data_direction dir)
{
	(void)ibdev;
	(void)addr;
	(void)size;
	(void)dir;
	IB_PORTALS4_UNIMPL("Sorry!");
}

EXPORT_SYMBOL_GPL(ib_portals_dma_unmap_single);

dma_addr_t ib_portals_dma_map_single(struct ib_device *ibdev, void *cpu_addr,
                                     size_t size, enum dma_data_direction dir)
{
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	struct device *device;
	dma_addr_t dma_addr;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	dma_addr = dma_map_single(device, cpu_addr, size, dir);
	// PTL_DEBUG("CPU Virtual Address (decimal): %llu <---> DMA Address (decimal): "
	//           "%llu of size: %lu",
	//           (unsigned long long)cpu_addr, (unsigned long long)dma_addr, size);
	return dma_addr;
}
EXPORT_SYMBOL_GPL(ib_portals_dma_map_single);

int ib_portals_dma_mapping_error(struct ib_device *ibdev, dma_addr_t dma_addr)
{
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	struct device *device;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	return dma_mapping_error(device, dma_addr);
}
EXPORT_SYMBOL_GPL(ib_portals_dma_mapping_error);

int ib_portals_dma_unmap_sg(struct ib_device *ibdev, struct scatterlist *sgl,
                            int nents, enum dma_data_direction dir)
{
	(void)ibdev;
	(void)sgl;
	(void)nents;
	(void)dir;
	IB_PORTALS4_UNIMPL("Sorry!");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(ib_portals_dma_unmap_sg);

int ib_portals_dma_map_sg(struct ib_device *ibdev, struct scatterlist *sgl,
                          int nents, enum dma_data_direction dir)
{
	(void)ibdev;
	(void)sgl;
	(void)nents;
	(void)dir;
	IB_PORTALS4_UNIMPL("Sorry!");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(ib_portals_dma_map_sg);

void ib_portals_dma_sync_single_for_cpu(struct ib_device *ibdev,
                                        dma_addr_t dma_handle, size_t size,
                                        enum dma_data_direction dir)
{
	(void)ibdev;
	(void)dma_handle;
	(void)size;
	(void)dir;
	IB_PORTALS4_UNIMPL("Sorry!");
}

EXPORT_SYMBOL_GPL(ib_portals_dma_sync_single_for_cpu);

void ib_portals_dma_sync_single_for_device(struct ib_device *ibdev,
                                           dma_addr_t dma_handle, size_t size,
                                           enum dma_data_direction dir)
{
	(void)ibdev;
	(void)dma_handle;
	(void)size;
	(void)dir;
	IB_PORTALS4_UNIMPL("Sorry!");
}

EXPORT_SYMBOL_GPL(ib_portals_dma_sync_single_for_device);

/* Posting send/recv: always succeed, no CQEs are generated here */
int ib_portals_post_send(struct ib_qp *qp, struct ib_send_wr *wr, struct ib_send_wr **bad_wr)
{
	(void)qp;
	(void)wr;
	(void)bad_wr;
	IB_PORTALS4_UNIMPL("Sorry!");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(ib_portals_post_send);

int ib_portals_post_recv(struct ib_qp *qp, struct ib_recv_wr *recv_wr,
                         struct ib_recv_wr **bad_wr)
{
	struct ptl_qp *ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	struct ib_recv_wr *wr;
	int i = 0;
	PTL_CHECK(ptl_qp, PTL_QP);
	PTL_DEBUG("Hey receive queue of this queue pair: %d is at PTE: %d",
	          ptl_qp->qpn, ptl_qp->recv_cq->pte);

	for (wr = recv_wr; wr != NULL; wr = wr->next) {

		PTL_DEBUG("WR[%d]: addr=%p, wr_id=%llu, num_sge=%d\n", i, recv_wr,
		          recv_wr->wr_id, recv_wr->num_sge);

		if (wr->num_sge > 1) {
			PTL_FATAL("Oops sorry! I cannot yet support sgl lists");
		}
		// Print scatter-gather list entries
		for (int j = 0; j < recv_wr->num_sge; j++) {
			PTL_DEBUG("SGE[%d]: addr=0x%llx, length=%u, lkey=0x%x\n", j,
			          recv_wr->sg_list[j].addr, recv_wr->sg_list[j].length, recv_wr->sg_list[j].lkey);
		}
		i++;
	}
	PTL_DEBUG("Total WRs: %d\n", i);
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

int ib_portals_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg, int sg_nents,
                         unsigned int *sg_offset, unsigned int page_size)
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
                            unsigned int *meta_sg_offset,
                            unsigned int page_size)
{
	(void)mr;
	IB_PORTALS4_UNIMPL("Sorry!");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(ib_portals_map_mr_sg_pi);

u32 ib_portals_inc_rkey(u32 rkey)
{

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
const char *ib_portals_event_msg(int ev)
{
	(void)ev;
	return "IB_IB_PORTALS4_EV";
}

EXPORT_SYMBOL_GPL(ib_portals_event_msg);

const char *ib_portals_wc_status_msg(int status)
{
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

int ib_portals_register_client(struct ib_client *client)
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
	pr_info("Portals4/ib: register_client name=%s add=%ps remove=%ps "
	        "get_net_dev_by_params=%ps\n",
	        client->name, client->add, client->remove,
	        client->get_net_dev_by_params);

	if (!client->add) {
		IB_PORTALS4_WARN("Portals4/ib: client '%s' has no add() callback",
		                 client->name);
	}

	if (!client->remove) {
		IB_PORTALS4_WARN("Portals4/ib: client '%s' has no remove() callback",
		                 client->name);
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
			if (client_entry->client->remove) {
				IB_PORTALS4_UNIMPL(
				        "Sorry! I don't know how to call the remove callback");
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
int ib_portals_dev_to_node(struct device *dev)
{
	(void)dev;
	return numa_node_id();
}

EXPORT_SYMBOL_GPL(ib_portals_dev_to_node);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IB portals implementation over Portals4");
MODULE_AUTHOR("Giorgis Saloustros (Chatzis) gesalous@ics.forth.gr");
