#include "ptl_bxiv3_device.h"
#include "ib_portals.h"
#include "linux/bxi3/ptl.h"
#include "portals4_bxiext.h"
#include "ptl_connection.h"
#include "ptl_cq.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include <asm-generic/errno-base.h>
#include <linux/dma-direction.h>
#include <linux/err.h>
#include <linux/gfp_types.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <portals4.h>
#include <rdma/ib_verbs.h>

/**
 * Maximum size of the buffers to receive messages regarding the connection
 * protocol
 */
#define RDMA_PTL_MSG_BUFFER_SIZE 512UL

static void ptl_bxiv3_fill_fake_dev(struct ptl_bxiv3_device *bxiv3_dev) {
  struct ib_device_attr *attrs = &bxiv3_dev->fake_ib_dev.attrs;
  //  device_attr->node_guid = 0x0002c90300fed670;
  // device_attr->sys_image_guid = 0x0002c90300fed673;
  attrs->max_mr_size = 18446744073709551615UL;
  attrs->page_size_cap = 0xfffffe00;
  attrs->vendor_id = 0x000002c9;
  attrs->vendor_part_id = 0x00001003;
  attrs->hw_ver = 0x00000001;
  attrs->max_qp = 131000;
  attrs->max_qp_wr = 16351;
  attrs->device_cap_flags = 0x05361c76;
  attrs->max_sge_rd = 30;
  attrs->max_cq = 65408;
  attrs->max_cqe = 4194303;
  attrs->max_mr = 524032;
  attrs->max_pd = 32764;
  /*
   * Since I am an initiator I am not allowed me the initiator to perform rdma
   * read or writes to the target. Period. Set it to 0 for this purpose.
   */
  attrs->max_qp_rd_atom = 128;
  attrs->max_ee_rd_atom = 0;
  attrs->max_res_rd_atom = 2096000;
  attrs->max_qp_init_rd_atom = 128;
  attrs->max_ee_init_rd_atom = 0;
  attrs->atomic_cap = 1; // Assuming 1 corresponds to the enum value
  attrs->max_ee = 0;
  attrs->max_rdd = 0;
  attrs->max_mw = 0;
  attrs->max_raw_ipv6_qp = 0;
  attrs->max_raw_ethy_qp = 0;
  attrs->max_mcast_grp = 8192;
  attrs->max_mcast_qp_attach = 248;
  attrs->max_total_mcast_qp_attach = 2031616;
  attrs->max_ah = 2147483647;

  attrs->max_srq = 256;     // Max Shared Receive Queues
  attrs->max_srq_wr = 4096; // Max SRQ work requests
  attrs->max_srq_sge = 32;  // Max SGE for SRQ
  /*
   * These parameters advertise the maximum number of non-contiguous pages the
   * BXI NIC can register in a single Keyed SGL. This value directly influences
   * the block layer's request splitting and NVMe command count.
   *
   * When IOMMU is enabled, dma_map_sg often maps non-contiguous physical pages
   * into a single contiguous IOVA, allowing efficient Keyed SGL usage (similar
   * to Verbs). Without this, we fall back to Extended SGLs, which incur an
   * extra RDMA Read from the target and higher memory overhead on the
   * initiator.
   *
   * Note: Per NVMe protocol, the maximum is 256. Setting this too low (e.g., 1)
   * will cause significant IOPS overhead due to excessive command
   * fragmentation.
   */
  attrs->max_pi_fast_reg_page_list_len = PTL_BXIV3_MAX_FR_PAGES;
  attrs->max_fast_reg_page_list_len = PTL_BXIV3_MAX_FR_PAGES;
}

static void ptl_bxiv3_device_enable_rma(struct ptl_bxiv3_device *bxiv3_dev,
                                        int pte, struct ptl_cq *cq) {
  int rc;
  /** To enable RMA operations we create a persistent list entry that exposes
   * all the address space. However this is 100% safe because:
   * 1. The IOMMU sets a separate page table for the NIC so it does not have all
   * the kernel addresses.
   * 2. The driver registers and unregisters pages per operation so the target
   * cannot access stuff that it shouldn't.
   **/
  bxiv3_dev->rma_pte = pte;
  memset(&bxiv3_dev->rma_le, 0x00, sizeof(bxiv3_dev->rma_le));
  bxiv3_dev->rma_le.ignore_bits = 0;
  bxiv3_dev->rma_le.match_bits = 0;
  bxiv3_dev->rma_le.match_id.phys.nid = PTL_NID_ANY;
  bxiv3_dev->rma_le.match_id.phys.pid = PTL_PID_ANY;
  bxiv3_dev->rma_le.min_free = 0;
  bxiv3_dev->rma_le.start = 0;
  bxiv3_dev->rma_le.length = PTL_SIZE_MAX;
  bxiv3_dev->rma_le.uid = PTL_UID_ANY;
  bxiv3_dev->rma_le.ct_handle = PTL_CT_NONE;
  bxiv3_dev->rma_le.options = PTL_RMA_LE_OPTS;
  rc = PtlLEAppend(bxiv3_dev->nicia_handle, bxiv3_dev->rma_pte,
                   &bxiv3_dev->rma_le, PTL_PRIORITY_LIST, bxiv3_dev,
                   &bxiv3_dev->rma_leh);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to expose address space for rma operations. Reason: %s",
              PtlToStr(rc, PTL_STR_ERROR));
  }
}

static void ptl_bxiv3_dev_print_ni_limits(const struct ptl_ni_limits *limits) {
  PTL_DEBUG("ptl_ni_limits:");
  PTL_DEBUG("  max_entries           = %d", limits->max_entries);
  PTL_DEBUG("  max_unexpected_headers= %d", limits->max_unexpected_headers);
  PTL_DEBUG("  max_mds               = %d", limits->max_mds);
  PTL_DEBUG("  max_cts               = %d", limits->max_cts);
  PTL_DEBUG("  max_eqs               = %d", limits->max_eqs);
  PTL_DEBUG("  max_pt_index          = %d", limits->max_pt_index);
  PTL_DEBUG("  max_iovecs            = %d", limits->max_iovecs);
  PTL_DEBUG("  max_list_size         = %d", limits->max_list_size);
  PTL_DEBUG("  max_triggered_ops     = %d", limits->max_triggered_ops);
  PTL_DEBUG("  max_msg_size          = %llu", limits->max_msg_size);
  PTL_DEBUG("  max_atomic_size       = %llu", limits->max_atomic_size);
  PTL_DEBUG("  max_fetch_atomic_size = %llu", limits->max_fetch_atomic_size);
  PTL_DEBUG("  max_waw_ordered_size  = %llu", limits->max_waw_ordered_size);
  PTL_DEBUG("  max_war_ordered_size  = %llu", limits->max_war_ordered_size);
  PTL_DEBUG("  max_volatile_size     = %llu", limits->max_volatile_size);
  PTL_DEBUG("  features              = 0x%x", limits->features);
  PTL_DEBUG("  bxi_max_cqs           = %u", limits->bxi_max_cqs);
  PTL_DEBUG("  bxi_compute_line      = %u", limits->bxi_compute_line);
  PTL_DEBUG("  cq_mode               = %d", (int)limits->cq_mode);
  PTL_DEBUG("  host_cq_size          = %llu", limits->host_cq_size);
}

struct ptl_bxiv3_device *ptl_bxiv3_dev_create(u32 iface_id) {
  struct ptl_bxiv3_device *bxiv3_dev;
  struct ptl_conn_recv_buffer *recv_buffer;
  struct ptl_conn_recv_buffer *tmp;
  struct ptl_conn_recv_buffer *buf;
  ptl_ni_limits_t desired;
  cpumask_t cpu_mask;
  int rc;
  int ret;
  int node;
  int cpu;

  bxiv3_dev = kzalloc(sizeof(*bxiv3_dev), GFP_KERNEL);
  if (!bxiv3_dev) {
    PTL_FATAL("Out of memory");
    return ERR_PTR(-ENOMEM);
  }

  bxiv3_dev->object_type = PTL_BXIV3_DEVICE;
  ptl_bxiv3_fill_fake_dev(bxiv3_dev);
  spin_lock_init(&bxiv3_dev->pte_table_lock);
  /*Reserve PTL_CP_SERVER_PTE for the connection management*/
  set_bit(PTL_CP_SERVER_PTE, bxiv3_dev->pte_table);
  /*<gesalous> non-matching feat*/
  /*Reserve for RMA operations, deactivate this feat for the non-matching case*/
  // set_bit(PTL_RMA_PTE, bxiv3_dev->pte_table);
  bxiv3_dev->iface_id = iface_id;
  kref_init(&bxiv3_dev->count);

  PTL_DEBUG("Initializing BXIv3 device for iface id: %d...", iface_id);

  memset(&desired, 0, sizeof(desired));
  // desired.max_entries = 52987;
  // desired.max_unexpected_headers = 52987;
  // desired.max_mds = 52987;
  // desired.max_cts = 1024;
  // desired.max_eqs = 1024;
  // desired.max_pt_index = 511;
  // desired.max_iovecs = 1073741823;
  // desired.max_list_size = 52987;
  // desired.max_triggered_ops = 52987;
  // desired.max_msg_size = 68719476735;
  // desired.max_atomic_size = 0;
  // desired.max_fetch_atomic_size = 0;
  // desired.max_waw_ordered_size = 0;
  // desired.max_war_ordered_size = 0;
  // desired.max_volatile_size = 60;
  desired.features =
      PTL_BXI3_DEBUG | PTL_BXI3_SERVICE; // XXX TODO XXX check again
  // desired.bxi_max_cqs = 1;
  // desired.bxi_compute_line = 0;
  // desired.cq_mode = 0;
  // desired.host_cq_size = 0;

  rc = PtlNIInit(iface_id, PTL_NI_NO_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY,
                 NULL, &bxiv3_dev->actual, &bxiv3_dev->nicia_handle);
  if (PTL_OK != rc) {
    PTL_WARN("PtlNIInit() failed with code: %d no ifcace: %d", rc, iface_id);
    ret = -EIO;
    goto clean_up;
  }
  ptl_bxiv3_dev_print_ni_limits(&bxiv3_dev->actual);

  rc = PtlGetPhysId(bxiv3_dev->nicia_handle, &bxiv3_dev->proc_id);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to get physical id of #iface: %d reason: %d", iface_id,
              rc);
    ret = -EIO;
    goto clean_up;
  }
  PTL_DEBUG("Initialized device %d with info {nid: %u pid: %u}", iface_id,
            bxiv3_dev->proc_id.phys.nid, bxiv3_dev->proc_id.phys.pid);

  /*Create the ptl_cq for PTL_CP_SERVER_PTE*/
  cpumask_clear(&cpu_mask);
  node = 0; // TODO: replace by NIC's numa node
  cpu = cpumask_first_and(cpumask_of_node(node), cpu_online_mask);
  if (cpu >= nr_cpu_ids)
    cpu = cpumask_first(cpu_online_mask); // fallback
  cpumask_set_cpu(cpu, &cpu_mask);

  rc = PtlEQAsyncIntrAlloc(bxiv3_dev->nicia_handle, &cpu_mask,
                           &bxiv3_dev->intr_index);
  if (PTL_OK != rc) {
    PTL_FATAL(
        "Failed to allocate interrupt index for BXIv3 with error code: %d", rc);
    goto clean_up;
  }
  bxiv3_dev->conn_mgmt_eq =
      ptl_cq_create(NULL, bxiv3_dev, PTL_CP_SERVER_CQ_ENTRIES,
                    PTL_CP_SERVER_PTE, IB_POLL_SOFTIRQ);
  if (IS_ERR(bxiv3_dev->conn_mgmt_eq)) {
    PTL_FATAL("Creation of ptl_cq for connection management failed");
    ret = -EIO;
    goto clean_up;
  }
  PTL_DEBUG("Created PTL_CQ for the connection management successfully");

  PTL_DEBUG("Initializing recv buffers for connection management... ");
  INIT_LIST_HEAD(&bxiv3_dev->conn_buffer_list);
  for (u32 i = 0; i < PTL_BXIV3_DEVICE_CONNECTION_NUM_RECV_BUFFERS; i++) {
    PTL_DEBUG("Recv buffer size: %lu allocation size: %lu",
              sizeof(*recv_buffer), RDMA_PTL_MSG_BUFFER_SIZE);
    recv_buffer = kzalloc(sizeof(*recv_buffer), GFP_KERNEL);
    if (NULL == recv_buffer) {
      PTL_FATAL("Out of mem");
      ret = -ENOMEM;
      goto rollback;
    }
    recv_buffer->object_type = PTL_CONN_RECV_BUFFER;
    if (RDMA_PTL_MSG_BUFFER_SIZE <= sizeof(*recv_buffer->conn_msg)) {
      PTL_FATAL("RDMA_PTL_MSG_BUFFER_SIZE too small. Value is: %lu needs at "
                "least %lu",
                RDMA_PTL_MSG_BUFFER_SIZE, sizeof(*recv_buffer->conn_msg));
    }
    recv_buffer->conn_msg = kzalloc(RDMA_PTL_MSG_BUFFER_SIZE, GFP_KERNEL);
    if (NULL == recv_buffer->conn_msg) {
      PTL_FATAL("Out of mem");
      ret = -ENOMEM;
      goto rollback;
    }

    recv_buffer->le.cpu_start = recv_buffer->conn_msg;
    recv_buffer->le.length = RDMA_PTL_MSG_BUFFER_SIZE;
    recv_buffer->le.start = ib_portals_dma_map_single(
        &bxiv3_dev->fake_ib_dev, recv_buffer->le.cpu_start,
        recv_buffer->le.length, DMA_FROM_DEVICE);
    if (ib_portals_dma_mapping_error(&bxiv3_dev->fake_ib_dev,
                                     recv_buffer->le.start)) {
      PTL_FATAL("Failed dma mapping");
      ret = -EIO;
      goto rollback;
    }

    recv_buffer->le.ct_handle = PTL_CT_NONE;
    recv_buffer->le.uid = PTL_UID_ANY;
    recv_buffer->le.match_id.phys.nid = PTL_NID_ANY;
    recv_buffer->le.match_id.phys.pid = PTL_PID_ANY;
    recv_buffer->le.match_id.rank = PTL_RANK_ANY;
    recv_buffer->le.options = PTL_SRV_ME_OPTS;
    recv_buffer->le.min_free = 0; /* minimum free buffer required */
    /* BXI extension  */
    recv_buffer->le.bxi_cq =
        0; /* or a CQ index if you use multi-CQ XXX TODO XXX What is this? */
    PTL_DEBUG("Appending LE: CPU_START: 0x%lx PHYSICAL START: 0x%lx",
              (unsigned long)recv_buffer->le.cpu_start,
              (unsigned long)recv_buffer->le.start);

    rc = PtlLEAppend(bxiv3_dev->nicia_handle, PTL_CP_SERVER_PTE,
                     &recv_buffer->le, PTL_PRIORITY_LIST, recv_buffer,
                     &recv_buffer->leh);
    if (rc != PTL_OK) {
      PTL_FATAL("Failed with reason: %s", PtlToStr(rc, PTL_STR_ERROR));
      ret = -EIO;
      goto rollback;
    }
    list_add(&recv_buffer->head, &bxiv3_dev->conn_buffer_list);
    PTL_DEBUG("Registered buffer in iface_id: %d and #PTE:%d with "
              "buffer no: %u of size: %llu",
              bxiv3_dev->iface_id, PTL_CP_SERVER_PTE, i,
              recv_buffer->le.length);
  }
  bxiv3_dev->ptl_cq_pool = ptl_cq_pool_create(bxiv3_dev);

  /*<gesalous> deactivate these steps for the non-matching feat*/
  // PTL_DEBUG("Creating the completion queue for the RMA operations...");
  // bxiv3_dev->rma_operations_eq = ptl_cq_create(NULL, bxiv3_dev,
  // PTL_CP_SERVER_CQ_ENTRIES, PTL_RMA_PTE, IB_POLL_SOFTIRQ);
  // PTL_DEBUG("Creating the completion queue for the RMA
  // operations...SUCCESS"); ptl_bxiv3_device_enable_rma(bxiv3_dev, PTL_RMA_PTE,
  // bxiv3_dev->rma_operations_eq);

  /* QP Map hashtable init */
  hash_init(bxiv3_dev->qp_map);
  spin_lock_init(&bxiv3_dev->qp_map_lock);
  PTL_INFO("Initialized interface: %d with nid: %d or (>>7:%d) and pid: %d "
           "successfully",
           iface_id, bxiv3_dev->proc_id.phys.nid,
           bxiv3_dev->proc_id.phys.nid >> 7, bxiv3_dev->proc_id.phys.pid);
  return bxiv3_dev;
rollback:
  list_for_each_entry_safe(buf, tmp, &bxiv3_dev->conn_buffer_list, head) {
    list_del(&buf->head);
    /* Unlink the ME from the portal table */
    PtlMEUnlink(buf->leh);
    /* Unmap DMA buffer */
    ib_portals_dma_unmap_single(&bxiv3_dev->fake_ib_dev, buf->le.start,
                                buf->le.length, DMA_TO_DEVICE);
    /* Free the buffer */
    kfree(buf);
  }
  ptl_cq_destroy(bxiv3_dev->conn_mgmt_eq);
  PtlNIFini(bxiv3_dev->nicia_handle);
clean_up:
  kfree(bxiv3_dev);
  return ERR_PTR(ret);
}

ptl_pt_index_t ptl_bxiv3_dev_alloc_pte(struct ptl_bxiv3_device *bxiv3_dev) {
  int bit;
  spin_lock(&bxiv3_dev->pte_table_lock);
  bit = find_first_zero_bit(bxiv3_dev->pte_table, PTL_BXIV3_DEVICE_MAX_PTES);
  if (bit >= PTL_BXIV3_DEVICE_MAX_PTES) {
    bit = -1;
    goto out;
  }

  set_bit(bit, bxiv3_dev->pte_table);
out:
  spin_unlock(&bxiv3_dev->pte_table_lock);
  return bit;
}

ptl_pt_index_t ptl_bxiv3_dev_free_pte(struct ptl_bxiv3_device *bxiv3_dev,
                                      ptl_pt_index_t pte) {
  ptl_pt_index_t ret = 0;

  if (!bxiv3_dev)
    return -EINVAL;

  if (pte < 0 || pte >= PTL_BXIV3_DEVICE_MAX_PTES)
    return -EINVAL;

  spin_lock(&bxiv3_dev->pte_table_lock);
  if (!test_bit(pte, bxiv3_dev->pte_table)) {
    ret = -EINVAL;
    goto out;
  }

  clear_bit(pte, bxiv3_dev->pte_table);
  ret = 0;
out:
  spin_unlock(&bxiv3_dev->pte_table_lock);
  return ret;
}

int ptl_bxiv3_dev_destroy(struct ptl_bxiv3_device *bxiv3_dev) {
  struct ptl_conn_recv_buffer *tmp;
  struct ptl_conn_recv_buffer *buf;
  struct ptl_bxiv3_qp_map_entry *e;
  struct hlist_node *tmp_qp_map_entry;
  int bkt;
  int rc;

  if (!bxiv3_dev)
    return -EINVAL;

  rc = PtlEQAsyncIntrFree(bxiv3_dev->nicia_handle, bxiv3_dev->intr_index);
  if (PTL_OK != rc) {
    PTL_WARN("Failed to destroy interrupt vector with error code: %d. OK, life "
             "goes on",
             rc);
  }
  /* Destroy the pool */
  rc = ptl_cq_pool_destroy(bxiv3_dev->ptl_cq_pool);
  if (rc) {
    PTL_WARN("Failed to destroy CQ pool: %d", rc);
    goto error;
  }

  PTL_DEBUG("ptl_cq_pool destroyed successfully");
  rc = ptl_cq_destroy(bxiv3_dev->conn_mgmt_eq);
  if (rc) {
    PTL_WARN("Sorry ptl_cq probably busy");
    goto error;
  }

  /*release the buffers*/
  list_for_each_entry_safe(buf, tmp, &bxiv3_dev->conn_buffer_list, head) {
    list_del(&buf->head);
    /* Unmap DMA buffer */
    ib_portals_dma_unmap_single(&bxiv3_dev->fake_ib_dev, buf->le.start,
                                buf->le.length, DMA_TO_DEVICE);
    /* Free the buffer */
    kfree(buf);
  }
  /* Clean up QP table */
  spin_lock(&bxiv3_dev->qp_map_lock);
  hash_for_each_safe(bxiv3_dev->qp_map, bkt, tmp_qp_map_entry, e, node) {
    hash_del(&e->node);
    /* If you own the ptl_qp lifetime here, free/destroy it as well */
    /* ptl_qp_destroy(e->qp); or similar */
    kfree(e);
  }
  spin_unlock(&bxiv3_dev->qp_map_lock);

  /* Finalize the NI LAST. PtlNIFini() tears down the NI's internal state,
   * including the portal table that PtlPTFree()/PtlEQFree() write to. Running it
   * before ptl_cq_pool_destroy()/ptl_cq_destroy() left every later Portals call
   * operating on a freed NI - a use-after-free that oopsed inside PtlPTFree
   * ("supervisor write to a not-present page") on every rmmod. Warn instead of
   * goto error: at this point everything else is already torn down. */
  rc = PtlNIFini(bxiv3_dev->nicia_handle);
  if (PTL_OK != rc) {
    PTL_WARN("Failed to shut down nicia handle of iface id: %d with code: %d",
             bxiv3_dev->iface_id, rc);
  }

  kfree(bxiv3_dev);
  return 0;
error:
  return -EBUSY;
}
