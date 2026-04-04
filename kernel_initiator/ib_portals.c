// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/nvme.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

#include "asm-generic/errno-base.h"
#include "ib_portals.h"
#include "linux/bxi3/ptl.h"
#include "linux/container_of.h"
#include "linux/gfp_types.h"
#include "linux/scatterlist.h"
#include "linux/types.h"
#include "mr_portals_pool.h"
#include "portals4.h"
#include "portals4_bxiext.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cm_id.h"
#include "ptl_connection.h"
#include "ptl_cq.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include "ptl_recv_op.h"
#include "ptl_uuid.h"

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

/*
 * Just Mimicing the behaviour.
 * Drivers that don't need a DMA mapping at the RDMA layer, set dma_device to
 * NULL. This causes the ib_dma* helpers to just stash the kernel virtual
 * address into the dma address.
 */
static inline bool ib_portals_uses_virt_dma(struct ib_device *dev)
{
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(dev, struct ptl_bxiv3_device, fake_ib_dev);
	struct device *device;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	return IS_ENABLED(CONFIG_INFINIBAND_VIRT_DMA) && !device;
}

int ib_portals_dma_virt_map_sg(struct ib_device *ibdev, struct scatterlist *sg,
                               int nents)
{
	PTL_WARN("Caution got into this VIRT_DMA staff");
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	struct scatterlist *s;
	int i;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);

	for_each_sg(sg, s, nents, i) {
		sg_dma_address(s) = (uintptr_t)sg_virt(s);
		sg_dma_len(s) = s->length;
	}
	return nents;
}

/*Again just mimicing the behaviour*/
static inline int ib_portals_dma_map_sg_attrs(struct ib_device *ibdev,
                                              struct scatterlist *sg, int nents,
                                              enum dma_data_direction direction,
                                              unsigned long dma_attrs)
{
	struct device *device;
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);

	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	if (ib_portals_uses_virt_dma(ibdev)) {
		return ib_portals_dma_virt_map_sg(ibdev, sg, nents);
	}

	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;
	return dma_map_sg_attrs(device, sg, nents, direction, dma_attrs);
}

/* Mapped to ib_alloc_pd/ib_dealloc_pd */
struct ib_pd *ib_portals_alloc_pd(struct ib_device *dev, unsigned int flags)
{
	struct ptl_pd *ptl_pd = ptl_pd_alloc(dev, flags);
	return &ptl_pd->fake_pd;
}

EXPORT_SYMBOL_GPL(ib_portals_alloc_pd);

void ib_portals_dealloc_pd(struct ib_pd *pd)
{
	struct ptl_pd *ptl_pd = container_of(pd, struct ptl_pd, fake_pd);
	PTL_CHECK(ptl_pd, PTL_PD);
	ptl_pd_destroy(ptl_pd);
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
	struct ptl_cq *ptl_cq = container_of(cq, struct ptl_cq, fake_cq);
	PTL_CHECK(ptl_cq, PTL_CQ);
	ptl_cq_pool_put(ptl_cq->cq_pool, ptl_cq);
	PTL_DEBUG("Returned PTL_CQ (in PTE: %d) to the pool successfully ...", ptl_cq->pte);
}

EXPORT_SYMBOL_GPL(ib_portals_cq_pool_put);

/* QP */

int ib_portals_destroy_qp(struct ib_qp *qp)
{
	struct ptl_qp *ptl_qp;
	struct ptl_bxiv3_qp_map_entry *entry;
	ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);
	/**
	 * List of things to destory/clean:
	 *  1) ptl_id -->later there is an explicit call for it
	 *  2) ptl_pd --> it's dummy but there are explicit calls to free it ib_portals_dealloc_pd. Do nothing here.
	 *  3) send_cq, recv_cq: If it belongs to a cq_pool do not touch it. Otherwise, destroy it here
	 *  4) ptl_mr_list?
	 *  5) rma_le, rma_leh has been destroyed during drain qp
	 *  6) recv_op_meta the buffer that accepts the nvme_cpl. Destroy it here.
	 */
	if (NULL == ptl_qp->recv_cq->cq_pool) {
		ptl_cq_destroy(ptl_qp->recv_cq);
		ptl_bxiv3_dev_free_pte(ptl_qp->ptl_id->bxiv3_dev, ptl_qp->recv_cq->pte);
	}
	if (NULL == ptl_qp->send_cq->cq_pool && ptl_qp->recv_cq != ptl_qp->send_cq) {
		ptl_cq_destroy(ptl_qp->send_cq);
		ptl_bxiv3_dev_free_pte(ptl_qp->ptl_id->bxiv3_dev, ptl_qp->send_cq->pte);
	}

	spin_lock(&ptl_qp->ptl_id->bxiv3_dev->qp_map_lock);

	hash_for_each_possible(ptl_qp->ptl_id->bxiv3_dev->qp_map, entry, node, ptl_qp->qpn) {
		if (entry->key == ptl_qp->qpn) {
			ptl_qp = entry->ptl_qp;
			// Remove the entry from the hash table
			hash_del(&entry->node);
			break; // Exit immediately once found and removed
		}
	}
	spin_unlock(&ptl_qp->ptl_id->bxiv3_dev->qp_map_lock);
	kfree(ptl_qp->recv_op_meta);
	kfree(ptl_qp);
	return 0;
}

EXPORT_SYMBOL_GPL(ib_portals_destroy_qp);

/* DMA helpers (pretend success) */
void ib_portals_dma_unmap_single(struct ib_device *ibdev, dma_addr_t addr,
                                 size_t size, enum dma_data_direction dir)
{
	struct ptl_bxiv3_device *bxiv3_dev;
	struct device *device;


	if (!ibdev) {
		PTL_FATAL("Invalid parameter: ibdev is NULL");
		return;
	}

	bxiv3_dev = container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);

	/* If using virtual DMA, nothing to unmap */
	if (ib_portals_uses_virt_dma(ibdev)) {
		PTL_DEBUG("Virtual DMA - no-op unmap");
		return;
	}

	PTL_DEBUG("DMA_UNMAP_single: iova=%llx, length=%lu", addr, size);
	/* Otherwise, call the real DMA unmap */
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	dma_unmap_single(device, addr, size, dir);
}

EXPORT_SYMBOL_GPL(ib_portals_dma_unmap_single);

/*Checked for ib_portals_uses_virt_dma*/
dma_addr_t ib_portals_dma_map_single(struct ib_device *ibdev, void *cpu_addr,
                                     size_t size, enum dma_data_direction dir)
{
	PTL_DEBUG("DMA MAP single");
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	struct device *device;
	dma_addr_t dma_addr;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);

	if (ib_portals_uses_virt_dma(ibdev)) {
		return (uintptr_t)cpu_addr;
	}
	dma_addr = dma_map_single(device, cpu_addr, size, dir);
	// PTL_DEBUG("CPU Virtual Address (decimal): %llu <---> DMA Address (decimal):
	// "
	//           "%llu of size: %lu",
	//           (unsigned long long)cpu_addr, (unsigned long long)dma_addr,
	//           size);
	return dma_addr;
}
EXPORT_SYMBOL_GPL(ib_portals_dma_map_single);

/*Checked for ib_portals_uses_virt_dma*/
int ib_portals_dma_mapping_error(struct ib_device *ibdev, dma_addr_t dma_addr)
{
	PTL_DEBUG("DMA CHECK for MAPPING ERROR");
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	struct device *device;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	if (ib_uses_virt_dma(ibdev)) {
		return 0;
	}
	return dma_mapping_error(device, dma_addr);
}
EXPORT_SYMBOL_GPL(ib_portals_dma_mapping_error);

int ib_portals_dma_unmap_sg(struct ib_device *ibdev, struct scatterlist *sgl,
                            int nents, enum dma_data_direction dir)
{
	struct ptl_bxiv3_device *bxiv3_dev;
	struct device *device;
	struct scatterlist *s;
	int i;

	for_each_sg(sgl, s, nents, i) {
		PTL_DEBUG("DMA_UNMAP_sg: SG[%d]: virt_addr=%p, iova=%llx, length=%u", i, sg_virt(s),
		          sg_dma_address(s), s->length);
	}

	if (!ibdev || !sgl) {
		PTL_WARN("Invalid parameters: ibdev=%p, sgl=%p", ibdev, sgl);
		return 0;
	}

	bxiv3_dev = container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);

	/* If using virtual DMA, nothing to unmap */
	if (ib_portals_uses_virt_dma(ibdev)) {
		PTL_DEBUG("Virtual DMA - no-op unmap");
		return 0;
	}

	/* Otherwise, call the real DMA unmap */
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	dma_unmap_sg(device, sgl, nents, dir);

	return 0;
}

EXPORT_SYMBOL_GPL(ib_portals_dma_unmap_sg);

int ib_portals_dma_map_sg(struct ib_device *ibdev, struct scatterlist *sgl,
                          int nents, enum dma_data_direction dir)
{
	struct scatterlist *s;
	int ret;
	int i;
	// /* Print the scatter-gather list before mapping */
	// for_each_sg(sgl, s, nents, i) {
	//      PTL_DEBUG("DMA_MAP_sg: SG[%d]: virt_addr=%p, length=%u", i, sg_virt(s), s->length);
	// }

	ret = ib_portals_dma_map_sg_attrs(ibdev, sgl, nents, dir, 0);
	/* Print the scatter-gather list after mapping */
	if (ret > 0) {
		for_each_sg(sgl, s, nents, i) {
			PTL_DEBUG("  Post-map SG[%d out of %d]: virt_addr=%p dma_addr=0x%llx, dma_length=%u", i, nents, sg_virt(s), sg_dma_address(s), sg_dma_len(s));
		}
	} else {
		PTL_FATAL("DMA MAP sg failed, returned %d", ret);
	}
	return ret;
}

EXPORT_SYMBOL_GPL(ib_portals_dma_map_sg);

/*Checked for ib_portals_uses_virt_dma*/
void ib_portals_dma_sync_single_for_cpu(struct ib_device *ibdev,
                                        dma_addr_t dma_handle, size_t size,
                                        enum dma_data_direction dir)
{
	PTL_DEBUG("DMA SYNC for CPU");
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	struct device *device;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	if (!ib_portals_uses_virt_dma(ibdev)) {
		return dma_sync_single_for_cpu(device, dma_handle, size, dir);
	}
}
EXPORT_SYMBOL_GPL(ib_portals_dma_sync_single_for_cpu);

void ib_portals_dma_sync_single_for_device(struct ib_device *ibdev,
                                           dma_addr_t dma_handle, size_t size,
                                           enum dma_data_direction dir)
{
	PTL_DEBUG("DMA SYNC for DEVICE");
	struct ptl_bxiv3_device *bxiv3_dev =
	        container_of(ibdev, struct ptl_bxiv3_device, fake_ib_dev);
	struct device *device;
	PTL_CHECK(bxiv3_dev, PTL_BXIV3_DEVICE);
	device = PtlGetDriverDev(bxiv3_dev->nicia_handle);
	if (!ib_portals_uses_virt_dma(ibdev)) {
		return dma_sync_single_for_device(device, dma_handle, size, dir);
	}
}
EXPORT_SYMBOL_GPL(ib_portals_dma_sync_single_for_device);


static void ib_portals_send_nvmeof_cmd(struct ptl_qp *ptl_qp, struct ib_send_wr *send_wr)
{
	struct ptl_send_op *send_op = NULL;
	ptl_msg_t msg = {};
	ptl_md_t md = {};
	u64 dma_addr;
	u32 length;
	int rc;

	if (send_wr->num_sge > 1) {
		PTL_FATAL("Cannot handle num_sge > 1 for an nvme command");
	}
	struct ib_sge *sge = &send_wr->sg_list[0];

	if (IB_SEND_SIGNALED == send_wr->send_flags) {
		PTL_DEBUG("Creating notification event when delivery of this nvme_cmd completes");
		send_op = kzalloc(sizeof(*send_op), GFP_KERNEL);
		if (NULL == send_op) {
			PTL_FATAL("Out of memory");
		}
		send_op->object_type = PTL_SEND_OP;
		send_op->wr_cqe = send_wr->wr_cqe;
		send_op->ptl_qp = ptl_qp;
		send_op->wr_id = send_wr->wr_id;
		atomic_long_fetch_add(1, &send_op->ptl_qp->pending_nvme_cmds);
	}
	dma_addr = sge->addr;/*DMA/Physical address*/
	length = sge->length;/*Length in bytes (usually 64 for capsule)*/

	md.start = dma_addr;
	md.length = length;
	md.options = 0;
	md.eq_handle = ptl_qp->recv_cq->eq;
	md.ct_handle = PTL_CT_NONE;
	md.bxi_cq = PTL_BXI3_DEFAULT_CQ;

	msg.length = sge->length;
	msg.ack_req = send_op ? PTL_ACK_REQ : PTL_NO_ACK_REQ;
	msg.target_id.phys.nid = ptl_qp->ptl_id->remote_nid;
	msg.target_id.phys.pid = ptl_qp->ptl_id->remote_pid;
	msg.pt_index = ptl_qp->ptl_id->remote_msg_pte;
	msg.user_ptr = send_op;
	ptl_uuid_set_op_type(&msg.hdr_data, NVMeOF_cmd);
	ptl_uuid_set_cq_id(&msg.hdr_data, ptl_qp->ptl_id->remote_cq_id);
	ptl_uuid_set_initiator_qp_num(&msg.hdr_data, ptl_qp->ptl_id->initiator_qp_num);
	ptl_uuid_set_target_qp_num(&msg.hdr_data, ptl_qp->ptl_id->target_qp_num);
	PTL_DEBUG("Send NVMeOF cmd: Target qp num: %d initiator qp num: %d cq_id: %d",
	          ptl_qp->ptl_id->target_qp_num,
	          ptl_qp->ptl_id->initiator_qp_num,
	          ptl_qp->ptl_id->remote_cq_id);

	rc = PtlMsgPutOnce(ptl_qp->ptl_id->bxiv3_dev->nicia_handle, &md, &msg);
	if (PTL_OK != rc) {
		PTL_FATAL("Failed to send NVMeOF command with error: %s", PtlToStr(rc, PTL_STR_ERROR));
	}
	PTL_DEBUG("Send NVMeOF command to {nid:%d, pid: %d, pte: %d} for nvme "
	          "command address: SGE[0]: addr=0x%llx, len=%u successfully",
	          msg.target_id.phys.nid, msg.target_id.phys.pid, msg.pt_index,
	          dma_addr, length);
}

int ib_portals_post_send(struct ib_qp *qp, struct ib_send_wr *wr,
                         struct ib_send_wr **bad_wr)
{
	struct ptl_qp *ptl_qp;
	struct ib_send_wr *curr;
	ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);


	/* Iterate through the linked list of work requests */
	for (curr = wr; curr != NULL; curr = curr->next) {

		switch (curr->opcode) {
		case IB_WR_REG_MR:
			PTL_DEBUG("OPCODE: IB_WR_REG_MR treat is a no-op, XXX TODO XXX use it when matching is enabled in the future");
			break;

		case IB_WR_SEND:
			ib_portals_send_nvmeof_cmd(ptl_qp, curr);
			break;

		case IB_WR_RDMA_WRITE:
			PTL_FATAL("OPCODE: IB_WR_RDMA_WRITE, unhandled, Sorry!");
			break;

		case IB_WR_RDMA_READ:
			PTL_FATAL("OPCODE: IB_WR_RDMA_READ, unhandled,  Sorry!");
			break;

		case IB_WR_LOCAL_INV:
			PTL_FATAL("OPCODE: IB_WR_LOCAL_INV, unhandled,  Sorry!");
			break;

		default:
			PTL_FATAL("Unsupported opcode: %d, unhandled", curr->opcode);
			*bad_wr = curr;
			return -EINVAL;
		}

	}
	PTL_DEBUG("Bye bye from ib_postals_post_send");

	return 0;

}

EXPORT_SYMBOL_GPL(ib_portals_post_send);

int ib_portals_post_recv(struct ib_qp *qp, struct ib_recv_wr *recv_wr,
                         struct ib_recv_wr **bad_wr)
{
	struct ptl_qp *ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	struct ib_recv_wr *wr;
	struct ptl_recv_op *recv_op_meta;
	u64 recv_op_meta_idx;
	// int rc;
	int i = 0;
	PTL_CHECK(ptl_qp, PTL_QP);
	PTL_DEBUG("Hey receive queue of this queue pair: %d is at PTE: %d",
	          ptl_qp->qpn, ptl_qp->recv_cq->pte);

	for (wr = recv_wr; wr != NULL; wr = wr->next) {

		PTL_DEBUG("WR[%d]: addr=%llx, wr_id=%llu, num_sge=%d for QPN: %d", i, recv_wr->sg_list[0].addr,
		          recv_wr->wr_id, recv_wr->num_sge, ptl_qp->qpn);

		if (wr->num_sge > 1) {
			PTL_FATAL("Oops sorry! I cannot yet support sgl lists");
		}
		// Print scatter-gather list entries
		for (int j = 0; j < recv_wr->num_sge; j++) {
			PTL_DEBUG("Registering memory for recv: SGE[%d]: addr=0x%llx, length=%u, "
			          "lkey=0x%x\n",
			          j, recv_wr->sg_list[j].addr, recv_wr->sg_list[j].length,
			          recv_wr->sg_list[j].lkey);
			recv_op_meta_idx = (recv_wr->sg_list[j].addr - ptl_qp->ptl_id->nvme_cpl_start) / sizeof(struct nvme_completion);
			PTL_DEBUG(
			        "nvme_cpl: recv_op_meta_idx is: %llu is idx valid? %s for qpn: %d",
			        recv_op_meta_idx,
			        recv_op_meta_idx < ptl_qp->recv_op_meta_size ? "YES" : "NO",
			        ptl_qp->qpn);
			recv_op_meta = &ptl_qp->recv_op_meta[recv_op_meta_idx];
			if (recv_op_meta->is_set) {
				PTL_FATAL("Position idx: %llu already set", recv_op_meta_idx);
			}

			recv_op_meta->object_type = PTL_RECV_OP;
			recv_op_meta->le.start = recv_wr->sg_list[j].addr;
			recv_op_meta->le.length = recv_wr->sg_list[j].length;
			recv_op_meta->le.cpu_start =
			        NULL; /*don't know don't care I suppose.XXX TODO XXX*/
			recv_op_meta->le.options = PTL_SRV_ME_OPTS;
			recv_op_meta->le.min_free = 0;
			recv_op_meta->wr_id = recv_wr->wr_id;
			recv_op_meta->wr_cqe = recv_wr->wr_cqe;
			recv_op_meta->ptl_qp = ptl_qp;

			// rc = PtlLEAppend(ptl_qp->ptl_id->bxiv3_dev->nicia_handle,
			//               ptl_qp->recv_cq->pte, (const ptl_le_t *)&recv_op->le,
			//               PTL_PRIORITY_LIST, recv_op, &recv_op->leh);
			//    if (PTL_OK != rc) {
			//      PTL_FATAL("LEAppend failed. Reason: %s", PtlToStr(rc, PTL_STR_ERROR));
			// }
			/*<gesalous> non-matching feat*/
			/*Just put the meta in the custom list in the ptl_qp*/
			recv_op_meta->is_set = true;
		}
		i++;
	}
	return 0;
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
	struct ptl_mr *ptl_mr;
	struct scatterlist *s;
	u64 total_length = 0;
	u64 first_addr = 0;
	int mapped_nents;
	bool skipped;
	int i;

	PTL_DEBUG("=== ib_portals_map_mr_sg called ===");
	ptl_mr = container_of(mr, struct ptl_mr, fake_mr);
	PTL_CHECK(ptl_mr, PTL_MR);

	if (!mr || !sg || sg_nents <= 0) {
		PTL_FATAL("Invalid parameters: mr=%p, sg=%p, sg_nents=%d", mr, sg, sg_nents);
		return -EINVAL;
	}

	if (sg_dma_address(&sg[0])) {
		PTL_DEBUG("DMA_MAP_sg: already mapped skipping...");
		mapped_nents = sg_nents;
		skipped = true;
	} else {
		/* DMA-map the scatter-gather list FIRST */
		mapped_nents = ib_portals_dma_map_sg(mr->device, sg, sg_nents, DMA_BIDIRECTIONAL);
		if (mapped_nents <= 0) {
			PTL_FATAL("DMA mapping failed");
			return -ENOMEM;
		}
		skipped = false;
	}
	/* Calculate total length and get first DMA address */
	for_each_sg(sg, s, sg_nents, i) {
		u64 dma_addr = sg_dma_address(s);
		u32 dma_len = sg_dma_len(s);

		if (i == 0) {
			first_addr = dma_addr;
		}

		total_length += dma_len;

		PTL_DEBUG("%s  [%d] dma_addr: 0x%llx, dma_len: %u for device: %s",
		          skipped ? "Already mapped" : "Mapped",
		          i, dma_addr, dma_len,
		          dev_name(PtlGetDriverDev(
		                           ptl_mr->bxi3_device->nicia_handle)));
	}

	/* Populate the MR fields */
	mr->iova = first_addr;
	mr->length = total_length;

	PTL_DEBUG("=== MR populated: iova=0x%llx, length=%llu === sg_nents are: %d mapped_nents: %d",
	          mr->iova, mr->length, sg_nents, mapped_nents);

	return mapped_nents;
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
	/*Just mimicing the vanilla. Portals do not need it*/
	return PTL_MAGIC_FAKE_MR_KEY;
	// const u32 mask = 0x000000ff;
	// return ((rkey + 1) & mask) | (rkey & ~mask);
}

EXPORT_SYMBOL_GPL(ib_portals_inc_rkey);

void ib_portals_update_fast_reg_key(struct ib_mr *mr, u32 rkey)
{
	struct ptl_mr *ptl_mr = container_of(mr, struct ptl_mr, fake_mr);
	PTL_CHECK(ptl_mr, PTL_MR);
	mr->rkey = rkey;
	mr->lkey = rkey;
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
	if (!client_entry) {
		return -ENOMEM;
	}

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
	struct ptl_qp *ptl_qp;
	int rc;
	ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);
	PTL_DEBUG("Waiting for all pending nvme cmds to complete for "
	          "ptl_qp {initiator_qp_num: %d, target_qp_num: %d ...}",
	          ptl_qp->ptl_id->initiator_qp_num,
	          ptl_qp->ptl_id->target_qp_num);
	while (atomic_long_read(&ptl_qp->pending_nvme_cmds) != 0) {
		msleep(1); /*Sleep for 1 millisecond*/
	}
	PTL_DEBUG("Waiting for all pending nvme cmds to complete for "
	          "ptl_qp {initiator_qp_num: %d, target_qp_num: %d}... DONE",
	          ptl_qp->ptl_id->initiator_qp_num,
	          ptl_qp->ptl_id->target_qp_num);
	PTL_DEBUG("Unlinking LE for "
	          "ptl_qp {initiator_qp_num: %d, target_qp_num: %d}...",
	          ptl_qp->ptl_id->initiator_qp_num,
	          ptl_qp->ptl_id->target_qp_num);
	rc = PtlLEUnlink(ptl_qp->rma_leh);
	if (PTL_OK != rc) {
		PTL_FATAL("Failed to unlink receive buffer for ptl_qp: {initiator_qp_num: "
		          "%d, target_qp_num: %d}. Reason: %s",
		          ptl_qp->ptl_id->initiator_qp_num, ptl_qp->ptl_id->target_qp_num,
		          PtlToStr(rc, PTL_STR_ERROR));
	}
	PTL_DEBUG("Unlinking LE for "
	          "ptl_qp {initiator_qp_num: %d, target_qp_num: %d}...DONE. Drain successfull",
	          ptl_qp->ptl_id->initiator_qp_num,
	          ptl_qp->ptl_id->target_qp_num);
}

EXPORT_SYMBOL_GPL(ib_portals_drain_qp);

/* ibdev_to_node(ctrl->device->dev) analog */
int ib_portals_dev_to_node(struct device *dev)
{
	(void)dev;
	return numa_node_id();
}

EXPORT_SYMBOL_GPL(ib_portals_dev_to_node);


/*<gesalous> non-matching feat*/
int ib_portals_enable_rma_ops(struct ib_qp *qp, struct ib_cq *cq)
{
	struct ptl_qp *ptl_qp;
	int rc;

	ptl_qp = container_of(qp, struct ptl_qp, fake_qp);
	PTL_CHECK(ptl_qp, PTL_QP);

	memset(&ptl_qp->rma_le, 0x00, sizeof(ptl_qp->rma_le));
	ptl_qp->rma_le.ignore_bits = 0;
	ptl_qp->rma_le.match_bits = 0;
	ptl_qp->rma_le.match_id.phys.nid = PTL_NID_ANY;
	ptl_qp->rma_le.match_id.phys.pid = PTL_PID_ANY;
	ptl_qp->rma_le.min_free = 0;
	ptl_qp->rma_le.start = 0;
	ptl_qp->rma_le.length = PTL_SIZE_MAX;
	ptl_qp->rma_le.uid = PTL_UID_ANY;
	ptl_qp->rma_le.ct_handle = PTL_CT_NONE;
	ptl_qp->rma_le.options = PTL_RMA_LE_OPTS;
	rc = PtlLEAppend(ptl_qp->ptl_id->bxiv3_dev->nicia_handle,
	                 ptl_qp->recv_cq->pte, &ptl_qp->rma_le,
	                 PTL_PRIORITY_LIST, ptl_qp, &ptl_qp->rma_leh);
	if (PTL_OK != rc) {
		PTL_FATAL("Failed to expose address space for rma + nvme_cpl operations. Reason: %s", PtlToStr(rc, PTL_STR_ERROR));
	}
	ptl_qp->recv_op_meta = kzalloc(ptl_qp->ptl_id->nvme_completion_queue_size * sizeof(struct ptl_recv_op), GFP_KERNEL);
	ptl_qp->recv_op_meta_size = ptl_qp->ptl_id->nvme_completion_queue_size;
	PTL_DEBUG("Enable RMA ops for PTE: %d for QPN: %d", ptl_qp->recv_cq->pte, ptl_qp->qpn);
	return 1;
}
EXPORT_SYMBOL_GPL(ib_portals_enable_rma_ops);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IB portals implementation over Portals4");
MODULE_AUTHOR("Giorgis Saloustros (Chatzis) gesalous@ics.forth.gr");
