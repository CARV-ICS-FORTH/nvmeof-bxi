/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2020 Intel Corporation. All rights reserved.
 *   Copyright (c) Mellanox Technologies LTD. All rights reserved.
 *   Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */
#include "ptl_cm_id.h"
#include "ptl_config.h"
#include "ptl_connection.h"
#include "ptl_context.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_mem_desc.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include "ptl_print_nvme_commands.h"
#include "ptl_qp.h"
#include "ptl_srq.h"
#include "ptl_uuid.h"
#include "spdk/likely.h"
#include "spdk/log.h"
#include "spdk/nvme_spec.h"
#include "spdk/string.h"
#include "spdk/util.h"
#include "spdk_internal/rdma_provider.h"
#include "spdk_internal/rdma_utils.h"
#include <infiniband/verbs.h>
#include <portals4.h>
#include <portals4_bxiext.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>

// from common.c staff
#define SPDK_PTL_PROVIDER_SRQ_MAGIC_NUMBER 27081983UL
#define SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER 19082018UL
#define SPDK_PTL_CHECK_SRQ(X)                                                  \
  if ((X)->magic_number != SPDK_PTL_PROVIDER_SRQ_MAGIC_NUMBER) {               \
    SPDK_PTL_FATAL("Corrupted PORTALS SRQ");                                   \
  }

#define SPDK_PTL_IS_SGE_LENGTH_ONE(wr)                                         \
  do {                                                                         \
    if ((wr)->num_sge != 1) {                                                  \
      for (int i = 0; i < (wr)->num_sge; i++) {                                \
        SPDK_PTL_DEBUG("Opcode: wr[%d] = %d, addr: %lu length is: %d", i,      \
                       (wr)->opcode, (wr)->sg_list[i].addr,                    \
                       (wr)->sg_list[i].length);                               \
      }                                                                        \
      SPDK_PTL_FATAL(                                                          \
          "Num sges > 1 are under development, sorry requested are: %d",       \
          (wr)->num_sge);                                                      \
    }                                                                          \
  } while (0)

#define SPDK_PTL_CHECK_SGE_LENGTH(wr)                                          \
  do {                                                                         \
    if ((wr)->num_sge > PTL_MAX_SG_LIST) {                                     \
      for (int i = 0; i < (wr)->num_sge; i++) {                                \
        SPDK_PTL_DEBUG("Opcode: wr[%d] = %d, addr: %lu length is: %d", i,      \
                       (wr)->opcode, (wr)->sg_list[i].addr,                    \
                       (wr)->sg_list[i].length);                               \
      }                                                                        \
      SPDK_PTL_FATAL("Num sges are %d larget than MAX: %d: %d", wr->num_sge,   \
                     PTL_MAX_SG_LIST, (wr)->num_sge);                          \
    }                                                                          \
  } while (0)

// static uint64_t rdma_ptl_calculate_crc64(const void *data, size_t length)
// {
// 	uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
// 	const unsigned char *p = data;

// 	for (size_t i = 0; i < length; i++) {
// 		crc ^= (uint64_t)p[i];
// 		for (int j = 0; j < 8; j++) {
// 			if (crc & 1) {
// 				crc = (crc >> 1) ^ 0xC96C5795D7870F42ULL;
// 			} else {
// 				crc >>= 1;
// 			}
// 		}
// 	}
// 	return crc ^ 0xFFFFFFFFFFFFFFFFULL;
// }

struct spdk_portals_provider_srq {
  uint64_t magic_number;
  struct spdk_rdma_provider_srq fake_srq;
  struct ptl_context *ptl_context;
};

struct spdk_portals_provider_qp {
  uint64_t magic_number;
  struct ptl_context *ptl_context;
  struct ptl_cm_id *ptl_id;
  struct spdk_rdma_provider_qp fake_spdk_rdma_qp;
};

struct spdk_rdma_provider_srq *spdk_rdma_provider_srq_create(
    struct spdk_rdma_provider_srq_init_attr *init_attr) {
  assert(init_attr);
  assert(init_attr->pd);
  struct ptl_context *ptl_context;
  struct spdk_portals_provider_srq *portals_srq;
  struct spdk_rdma_provider_srq *fake_rdma_srq;

  ptl_context = ptl_cnxt_get_from_ibvpd(init_attr->pd);

  portals_srq = calloc(1UL, sizeof(*portals_srq));
  if (!portals_srq) {
    SPDK_PTL_FATAL("Can't allocate memory for SRQ handle");
  }
  portals_srq->magic_number = SPDK_PTL_PROVIDER_SRQ_MAGIC_NUMBER;
  portals_srq->ptl_context = ptl_context;
  fake_rdma_srq = &portals_srq->fake_srq;

  if (init_attr->stats) {
    fake_rdma_srq->stats = init_attr->stats;
    fake_rdma_srq->shared_stats = true;
  } else {
    fake_rdma_srq->stats = calloc(1UL, sizeof(*fake_rdma_srq->stats));
    if (!fake_rdma_srq->stats) {
      SPDK_PTL_FATAL("SRQ statistics memory allocation failed");
      free(portals_srq);
      return NULL;
    }
  }

  struct ptl_pd *ptl_pd = ptl_pd_get_from_ibv_pd(init_attr->pd);

#if PTL_USE_MATCHING
  struct ptl_srq *ptl_srq =
      ptl_srq_create(ptl_pd, &init_attr->srq_init_attr, -1 /*unused*/);
#else
  /**
   * IB Verbs SRQ (Shared Receive Queue) to Portals Mapping:
   *
   * In IB/verbs, an ibv_srq is simply a list of buffers that multiple queue
   * pairs can share for receive operations. There is no direct relation between
   * the SRQ and receive event completions.
   *
   * The typical IB flow is:
   * 1. Target creates an ibv_srq
   * 2. Target posts receive buffers to the ibv_srq (before creating any QPs)
   * 3. Multiple QPs can later be associated with this SRQ
   *
   * However, in Portals, a receive list is tightly coupled to both a PTE
   * (Portal Table Entry) and an event queue. This architectural difference
   * requires bridging.
   *
   * Our bridging approach:
   * 1. At this point: Create a ptl_srq along with a ptl_cq, PTE, and associated
   *    infrastructure to emulate the IB SRQ behavior
   * 2. Later in rdma_create_qp: Detect when a QP is being associated with an
   * SRQ and rewire the connections to properly map the IB semantics to Portals
   */
  /*leave context for now we are going to set it up later*/
  /**
   * Currently we use PTL_PT_INDEX but we can surely change to allocate_pte
   * and allow multiplse srqs and more than one reactors. XXX TODO XXX
   */
  int pte = ptl_cnxt_allocate_pte(ptl_cnxt_get());
  if (-1 == pte) {
    SPDK_PTL_FATAL("Failed to get PTL_PT_INDEX PTE");
  }
  struct ptl_srq *ptl_srq =
      ptl_srq_create(ptl_pd, &init_attr->srq_init_attr, pte);
#endif

  fake_rdma_srq->srq = &ptl_srq->fake_srq;

  // if (!rdma_srq->srq) {
  // 	if (!init_attr->stats) {
  // 		free(rdma_srq->stats);
  // 	}
  // 	SPDK_ERRLOG("Unable to create SRQ, errno %d (%s)\n", errno,
  // spdk_strerror(errno)); 	free(rdma_srq); 	return NULL;
  // }
  return fake_rdma_srq;
}

int spdk_rdma_provider_srq_destroy(struct spdk_rdma_provider_srq *rdma_srq) {
  if (!rdma_srq) {
    return 0;
  }

  assert(rdma_srq->srq);
  SPDK_PTL_FATAL("UNIMPLEMENTED");
  free(rdma_srq);
  return -1;
}

static inline bool
rdma_queue_recv_wrs(struct spdk_rdma_provider_recv_wr_list *recv_wrs,
                    struct ibv_recv_wr *first,
                    struct spdk_rdma_provider_wr_stats *recv_stats) {
  SPDK_PTL_FATAL("UNIMPLEMENTED");
  return false;
}

bool spdk_rdma_provider_srq_queue_recv_wrs(
    struct spdk_rdma_provider_srq *rdma_srq, struct ibv_recv_wr *first) {
  // struct spdk_rdma_provider_srq *spdk_ptl_srq = SPDK_CONTAINEROF(rdma_srq,
  // spdk_rdma_provider_srq, fake_srq);
  struct ibv_recv_wr *last;
  struct spdk_rdma_provider_wr_stats *recv_stats;
  struct spdk_rdma_provider_recv_wr_list *recv_wrs;
  bool ret;
  if (NULL == first) {
    SPDK_PTL_FATAL("First is NULL. XXX TODO XXX Nothing to do?");
    return false;
  }
  assert(rdma_srq->stats);
  recv_stats = rdma_srq->stats;
  recv_stats->num_submitted_wrs++;
  last = first;
  while (last->next != NULL) {
    last = last->next;
    recv_stats->num_submitted_wrs++;
  }

  recv_wrs = &rdma_srq->recv_wrs;

  if (recv_wrs->first == NULL) {
    recv_wrs->first = first;
    recv_wrs->last = last;
    ret = true;
  } else {
    recv_wrs->last->next = first;
    recv_wrs->last = last;
    ret = false;
  }
  // SPDK_PTL_DEBUG("Done exact the same steps as in IBV case total "
  // 	       "submitted wrs: %lu current: %lu",
  // 	       recv_stats->num_submitted_wrs,
  // 	       recv_stats->num_submitted_wrs - diff);
  return ret;
}

#if PTL_USE_MATCHING
static int spdk_rdma_provider_ptl_register_match_entry(
    struct ptl_context_op_meta *recv_meta, uint64_t match_bits, int num_sge,
    uint64_t wr_id, int pte, ptl_handle_ni_t nic) {
  int ret;
  recv_meta->recv_op.me.ignore_bits = PTL_UUID_IGNORE_MASK;
  // me.match_bits = ptl_uuid_set_op_type(PTL_UUID_IGNORE_MASK, PTL_SEND_RECV);
  recv_meta->recv_op.me.match_bits = match_bits;
  recv_meta->recv_op.me.match_id.phys.nid = PTL_NID_ANY;
  recv_meta->recv_op.me.match_id.phys.pid = PTL_PID_ANY;
  recv_meta->recv_op.me.min_free = 0;

#if PTL_ENABLE_IOVEC_RECEIVE
  recv_meta->recv_op.me.start = recv_meta->recv_op.io_vector;
  recv_meta->recv_op.me.length = num_sge;
  recv_meta->recv_op.me.options = PTL_SRV_ME_OPTS | PTL_IOVEC;
#else
  recv_meta->recv_op.me.start =
      (ptl_addr_t)recv_meta->recv_op.io_vector[0].iov_base;
  recv_meta->recv_op.me.length = recv_meta->recv_op.io_vector[0].iov_len;
  recv_meta->recv_op.me.options = PTL_SRV_ME_OPTS;
#endif

  recv_meta->recv_op.me.ct_handle = PTL_CT_NONE;
  recv_meta->recv_op.me.uid = PTL_UID_ANY;
  recv_meta->wr_id = wr_id;
  ret = PtlMEAppend(nic,                    /*Network interface handle*/
                    pte,                    /*Portal table index*/
                    &recv_meta->recv_op.me, /*match entry*/
                    PTL_PRIORITY_LIST,      /*List type (PRIORITY or OVERFLOW)*/
                    recv_meta, /*pointer (can be used to store wr_id)*/
                    &recv_meta->recv_op.me_handle /*Returned handle*/
  );
  if (PTL_OK != ret) {
    SPDK_PTL_FATAL("Failed to append memory entry");
  }
  return ret;
}
#else
static int spdk_rdma_provider_ptl_register_list_entry(
    struct ptl_context_op_meta *recv_meta, int num_sge, uint64_t wr_id, int pte,
    ptl_handle_ni_t nic) {
  int ret;
  recv_meta->recv_op.le.ignore_bits = PTL_UUID_IGNORE_MASK;
  recv_meta->recv_op.le.match_id.phys.nid = PTL_NID_ANY;
  recv_meta->recv_op.le.match_id.phys.pid = PTL_PID_ANY;
  recv_meta->recv_op.le.min_free = 0;
#if PTL_ENABLE_IOVEC_RECEIVE
  recv_meta->recv_op.le.start = recv_meta->recv_op.io_vector;
  recv_meta->recv_op.le.length = num_sge;
  recv_meta->recv_op.le.options = PTL_SRV_ME_OPTS | PTL_IOVEC;
#else
  recv_meta->recv_op.le.start =
      (ptl_addr_t)recv_meta->recv_op.io_vector[0].iov_base;
  recv_meta->recv_op.le.length = recv_meta->recv_op.io_vector[0].iov_len;
  recv_meta->recv_op.le.options = PTL_SRV_ME_OPTS;
#endif
  recv_meta->recv_op.le.ct_handle = PTL_CT_NONE;
  recv_meta->recv_op.le.uid = PTL_UID_ANY;
  recv_meta->wr_id = wr_id;
  ret = PtlLEAppend(nic,                    /*Network interface handle*/
                    pte,                    /*Portal table index*/
                    &recv_meta->recv_op.le, /*match entry*/
                    PTL_PRIORITY_LIST,      /*List type (PRIORITY or OVERFLOW)*/
                    recv_meta, /*pointer (can be used to store wr_id)*/
                    &recv_meta->recv_op.le_handle /*Returned handle*/
  );
  if (PTL_OK != ret) {
    SPDK_PTL_FATAL("Failed to append memory list entry reason: {code: %d "
                   "message: %s} for pte: %d",
                   ret, PtlToStr(ret, PTL_STR_ERROR), pte);
  }
  return ret;
}
#endif

/**
 * @brief Cleans up the ibv_recv_wr descriptors. In the vanilla implementation
 * hardware handles the cleanup process. However, since SPDK_PTL translates the
 * ibv_recv_wr into PTLMEAppend entries, SPDK_PTL needs to do the cleanup.
 */
// static bool spdk_ptl_clean_up_recv_wr_desc(struct ibv_recv_wr *recv_wrs) {
//   return true;
// }

int spdk_rdma_provider_srq_flush_recv_wrs(
    struct spdk_rdma_provider_srq *rdma_srq, struct ibv_recv_wr **bad_wr) {
  ptl_handle_ni_t nic;
  struct spdk_portals_provider_srq *portals_srq;
  struct ptl_srq *ptl_srq;
  struct ptl_context_op_meta *recv_meta;
  int ret;
  uint32_t num_of_bufs = 0;

  if (spdk_unlikely(rdma_srq->recv_wrs.first == NULL)) {
    return 0;
  }
  portals_srq =
      SPDK_CONTAINEROF(rdma_srq, struct spdk_portals_provider_srq, fake_srq);
  SPDK_PTL_CHECK_SRQ(portals_srq);
  nic = ptl_cnxt_get_ni_handle(portals_srq->ptl_context);

  // Now it's time to append the entries in portals
  for (struct ibv_recv_wr *wr = rdma_srq->recv_wrs.first; wr != NULL;
       wr = wr->next) {
    ++num_of_bufs;

    if (wr->num_sge > PTL_IOVEC_SIZE) {
      SPDK_PTL_FATAL("IOVECTOR too small size is: %d needs %d", PTL_IOVEC_SIZE,
                     wr->num_sge);
    }

    recv_meta = calloc(1UL, sizeof(*recv_meta));
    recv_meta->obj_type = PTL_RECV_OP;

    for (int i = 0; i < wr->num_sge; i++) {
      recv_meta->recv_op.io_vector[i].iov_base =
          (ptl_addr_t)wr->sg_list[i].addr;
      recv_meta->recv_op.io_vector[i].iov_len = wr->sg_list[i].length;
      // SPDK_PTL_DEBUG("iovector[%d] = : Address = %p, Length = %lu\n",
      // 	       i, recv_meta->recv_op.io_vector[i].iov_base,
      // recv_meta->recv_op.io_vector[i].iov_len);
    }
#if PTL_USE_MATCHING
    recv_meta->cq_id = PTL_UUID_TARGET_COMPLETION_QUEUE_ID;
    ret = spdk_rdma_provider_ptl_register_match_entry(
        recv_meta, PTL_UUID_TARGET_SRQ_MATCH_BITS, wr->num_sge, wr->wr_id,
        ptl_cnxt_get_portal_index(portals_srq->ptl_context), nic);
#else
    ptl_srq =
        SPDK_CONTAINEROF(portals_srq->fake_srq.srq, struct ptl_srq, fake_srq);
    if (ptl_srq->obj_type != PTL_SRQ) {
      SPDK_PTL_FATAL("Corrupted PTL_SRQ");
    }
    recv_meta->cq_id = ptl_srq->ptl_cq->core_cq->cq_id;
    ret = spdk_rdma_provider_ptl_register_list_entry(
        recv_meta, wr->num_sge, wr->wr_id, ptl_srq->ptl_cq->core_cq->pte, nic);
#endif
    if (PTL_OK != ret) {
      SPDK_PTL_FATAL("Failed to register receive buffer");
    }
    // SPDK_PTL_DEBUG(
    //     "(After)Registering receive buffer {addr:%lu, length:%zu}",
    //     (size_t)recv_meta->recv_op.io_vector[0].iov_base,
    //     recv_meta->recv_op.io_vector[0].iov_len);
  }
  SPDK_PTL_DEBUG("Number of receive buffers posted in portals_srq: %p are: %u",
                 portals_srq, num_of_bufs);
  // rc = ibv_post_srq_recv(rdma_srq->srq, rdma_srq->recv_wrs.first, bad_wr);
  rdma_srq->recv_wrs.first = NULL;
  rdma_srq->stats->doorbell_updates++;
  return 0;
}

bool spdk_rdma_provider_qp_queue_recv_wrs(
    struct spdk_rdma_provider_qp *spdk_rdma_qp, struct ibv_recv_wr *first) {

  assert(spdk_rdma_qp);
  assert(first);
  struct spdk_rdma_provider_wr_stats *recv_stats = &spdk_rdma_qp->stats->recv;
  struct spdk_rdma_provider_recv_wr_list *recv_wrs = &spdk_rdma_qp->recv_wrs;
  struct ibv_recv_wr *last;

  recv_stats->num_submitted_wrs++;
  last = first;
  while (last->next != NULL) {
    last = last->next;
    recv_stats->num_submitted_wrs++;
  }

  if (recv_wrs->first == NULL) {
    recv_wrs->first = first;
    recv_wrs->last = last;
    return true;
  } else {
    recv_wrs->last->next = first;
    recv_wrs->last = last;
    return false;
  }
}

int spdk_rdma_provider_qp_flush_recv_wrs(
    struct spdk_rdma_provider_qp *spdk_rdma_qp, struct ibv_recv_wr **bad_wr) {
  struct spdk_portals_provider_qp *portals_qp;
  ptl_handle_ni_t nic;
  ptl_pt_index_t pte;
  struct ptl_context_op_meta *recv_meta;
  int ret;

  portals_qp = SPDK_CONTAINEROF(spdk_rdma_qp, struct spdk_portals_provider_qp,
                                fake_spdk_rdma_qp);
  if (SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER != portals_qp->magic_number) {
    SPDK_PTL_FATAL("Corrupted Portals QP!");
  }

  if (spdk_unlikely(spdk_rdma_qp->recv_wrs.first == NULL)) {
    // SPDK_PTL_DEBUG("Nothing to register for receive?");
    // raise(SIGINT);
    return 0;
  }
  // Now it's time to append the entries in portals
  for (struct ibv_recv_wr *wr = spdk_rdma_qp->recv_wrs.first; wr != NULL;
       wr = wr->next) {
    nic = ptl_cnxt_get_ni_handle(portals_qp->ptl_context);
#if PTL_USE_MATCHING
    /*XXX TODO XXX Check again maybe remove ptl_cnxt_get_portal_index*/
    // pte = ptl_cnxt_get_portal_index(portals_qp->ptl_context);
    pte = portals_qp->ptl_id->ptl_qp->recv_cq->cq_static->pte;
#else
    /*This function is called from the initiator to register buffer for
     * receiving nvme_cpl*/
    pte = portals_qp->ptl_id->ptl_qp->recv_cq->core_cq->pte;
#endif

    if (wr->num_sge > PTL_IOVEC_SIZE) {
      SPDK_PTL_FATAL("io_vector too small size: %d needs %d", PTL_IOVEC_SIZE,
                     wr->num_sge);
    }

    recv_meta = calloc(1UL, sizeof(*recv_meta));
    recv_meta->obj_type = PTL_RECV_OP;
    recv_meta->cq_id = ptl_cq_get_id(portals_qp->ptl_id->ptl_qp->recv_cq);
    for (int i = 0; i < wr->num_sge; i++) {
      recv_meta->recv_op.io_vector[i].iov_base =
          (ptl_addr_t)wr->sg_list[i].addr;
      recv_meta->recv_op.io_vector[i].iov_len = wr->sg_list[i].length;
      SPDK_PTL_DEBUG("SGE no: %d out of: %d: Address = %p, Length = %lu\n", i,
                     wr->num_sge, recv_meta->recv_op.io_vector[i].iov_base,
                     recv_meta->recv_op.io_vector[i].iov_len);
    }

#if PTL_USE_MATCHING
    ret = spdk_rdma_provider_ptl_register_match_entry(
        recv_meta, portals_qp->ptl_id->my_match_bits, wr->num_sge, wr->wr_id,
        pte, nic);
#else
    ret = spdk_rdma_provider_ptl_register_list_entry(recv_meta, wr->num_sge,
                                                     wr->wr_id, pte, nic);
#endif
    if (PTL_OK != ret) {
      SPDK_PTL_FATAL("Failed to append memory entry");
    }
  }
  // SPDK_PTL_DEBUG("MATCH_BITS: Registered memory for a single QP under match
  // bits: %lu", 	       portals_qp->ptl_id->my_match_bits);
  spdk_rdma_qp->recv_wrs.first = NULL;
  spdk_rdma_qp->stats->recv.doorbell_updates++;
  return 0;
}
// common end

struct spdk_rdma_provider_qp *
spdk_rdma_provider_qp_create(struct rdma_cm_id *cm_id,
                             struct spdk_rdma_provider_qp_init_attr *qp_attr) {
  SPDK_PTL_DEBUG("Creating an SPDK RDMA PROVIDER QP");
  struct spdk_portals_provider_qp *spdk_portals_qp;
  struct spdk_rdma_provider_qp *spdk_rdma_qp;
  int rc;
  struct ibv_qp_init_attr attr = {.qp_context = qp_attr->qp_context,
                                  .send_cq = qp_attr->send_cq,
                                  .recv_cq = qp_attr->recv_cq,
                                  .srq = qp_attr->srq,
                                  .cap = qp_attr->cap,
                                  .qp_type = IBV_QPT_RC};

  if (qp_attr->domain_transfer) {
    SPDK_PTL_FATAL("PORTALS provider doesn't support memory domain transfer "
                   "functionality");
    return NULL;
  }

  spdk_portals_qp = calloc(1UL, sizeof(*spdk_portals_qp));
  if (!spdk_portals_qp) {
    SPDK_ERRLOG("qp memory allocation failed");
  }

  spdk_portals_qp->ptl_context = ptl_cnxt_get();
  spdk_portals_qp->ptl_id = ptl_cm_id_get(cm_id);
  spdk_portals_qp->magic_number = SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER;

  spdk_rdma_qp = &spdk_portals_qp->fake_spdk_rdma_qp;

  if (qp_attr->stats) {
    spdk_rdma_qp->stats = qp_attr->stats;
    spdk_rdma_qp->shared_stats = true;
  } else {
    spdk_rdma_qp->stats = calloc(1UL, sizeof(*spdk_rdma_qp->stats));
    if (!spdk_rdma_qp->stats) {
      SPDK_ERRLOG("qp statistics memory allocation failed\n");
      free(spdk_portals_qp);
      return NULL;
    }
  }

  rc = rdma_create_qp(cm_id, qp_attr->pd, &attr);
  if (rc) {
    SPDK_ERRLOG("Failed to create qp, rc %d, errno %s (%d)\n", rc,
                spdk_strerror(errno), errno);
    free(spdk_portals_qp);
    return NULL;
  }
  spdk_rdma_qp->qp = cm_id->qp;
  spdk_rdma_qp->cm_id = cm_id;
  spdk_rdma_qp->domain = spdk_rdma_utils_get_memory_domain(qp_attr->pd);
  if (!spdk_rdma_qp->domain) {
    spdk_rdma_provider_qp_destroy(spdk_rdma_qp);
    return NULL;
  }

  qp_attr->cap = attr.cap;
  return spdk_rdma_qp;
}

int spdk_rdma_provider_qp_accept(struct spdk_rdma_provider_qp *spdk_rdma_qp,
                                 struct rdma_conn_param *conn_param) {
  assert(spdk_rdma_qp != NULL);
  assert(spdk_rdma_qp->cm_id != NULL);
  struct ptl_cm_id *ptl_id = ptl_cm_id_get(spdk_rdma_qp->cm_id);
  SPDK_PTL_DEBUG("CONN_PARAM: At accept got a valid ptl_id queue pair id: %d "
                 "conn_param len: %u",
                 ptl_id->fake_cm_id.qp->qp_num, conn_param->private_data_len);

  SPDK_PTL_DEBUG("CONN_PARAM sending to the target the following parameters");
  SPDK_PTL_DEBUG("CONN_PARAM param.srq = %u param.qp_num = %u "
                 "param.rnr_retry_count = %u param.responder_resources: %u "
                 "param.initiator_depth: %u param.flow_control: %u "
                 "param.private_data_len: %u\n",
                 conn_param->srq, conn_param->qp_num,
                 conn_param->rnr_retry_count, conn_param->responder_resources,
                 conn_param->initiator_depth, conn_param->flow_control,
                 conn_param->private_data_len);
  return rdma_accept(spdk_rdma_qp->cm_id, conn_param);
}

int spdk_rdma_provider_qp_complete_connect(
    struct spdk_rdma_provider_qp *spdk_rdma_qp) {
  // struct rdma_cm_event *fake_event;
  // struct ptl_cm_id *ptl_id = ptl_cm_id_get(spdk_rdma_qp->cm_id);
  /* Nothing to be done for Portals */
  SPDK_PTL_DEBUG("complete connect treat is a no-op");
  // fake_event = ptl_cm_id_create_event(ptl_id,
  // 				    NULL,
  // 				    RDMA_CM_EVENT_ESTABLISHED);
  // ptl_cm_id_add_event(ptl_id, fake_event);

  return 0;
}

void spdk_rdma_provider_qp_destroy(struct spdk_rdma_provider_qp *spdk_rdma_qp) {
  assert(spdk_rdma_qp != NULL);
  struct ptl_qp *ptl_qp;

  struct spdk_portals_provider_qp *portals_qp = SPDK_CONTAINEROF(
      spdk_rdma_qp, struct spdk_portals_provider_qp, fake_spdk_rdma_qp);
  if (portals_qp->magic_number != SPDK_PTL_PROVIDER_QP_MAGIC_NUMBER) {
    SPDK_PTL_FATAL("Corrupted portals_qp");
  }
  ptl_qp = portals_qp->ptl_id->ptl_qp;
  ibv_destroy_qp(&ptl_qp->fake_qp);
  free(portals_qp);
}

int spdk_rdma_provider_qp_disconnect(
    struct spdk_rdma_provider_qp *spdk_rdma_qp) {
  int rc = 0;

  assert(spdk_rdma_qp != NULL);
  SPDK_PTL_DEBUG("Calling disconnect for the queue pair...");
  assert(0);
  spdk_rdma_provider_qp_flush_send_wrs(spdk_rdma_qp, NULL);

  if (spdk_rdma_qp->cm_id) {
    rc = rdma_disconnect(spdk_rdma_qp->cm_id);
    if (rc) {
      if (errno == EINVAL &&
          spdk_rdma_qp->qp->context->device->transport_type ==
              IBV_TRANSPORT_IWARP) {
        /* rdma_disconnect may return an error and set errno to EINVAL in case
         * of iWARP. This behaviour is expected since iWARP handles disconnect
         * event other than IB and qpair is already in error state when we call
         * rdma_disconnect */
        return 0;
      }
      SPDK_PTL_FATAL("rdma_disconnect failed, errno %s (%d)\n",
                     spdk_strerror(errno), errno);
    }
  }

  return rc;
}

static inline uint16_t
spdk_rdma_provider_ptl_extract_CID(void *args, struct ptl_qp *ptl_qp) {
  struct spdk_nvme_cpl *cpl = args;

  // Extract lower 12 bits as the actual CID
  uint16_t local_tag = cpl->cid & 0xFFF;

  // Extract upper 4 bits as generation/metadata
  uint16_t generation = cpl->cid >> 12;

  SPDK_PTL_DEBUG("QPN: %d CID decode: raw=%u local_tag=%u generation=%u",
                 ptl_qp->ptl_cm_id->ptl_qp_num, (unsigned)cpl->cid,
                 (unsigned)local_tag, (unsigned)generation);

  return local_tag;
}

bool spdk_rdma_provider_qp_queue_send_wrs(
    struct spdk_rdma_provider_qp *spdk_rdma_qp, struct ibv_send_wr *first) {

  struct ibv_send_wr *last;
  /*gesalous, new staff*/
  struct ptl_context *ptl_cnxt;
  struct ptl_qp *ptl_qp;
  struct ptl_context_op_meta *op_meta;
  uint16_t nvme_cid;
  uint16_t count_parts = 0;

  assert(spdk_rdma_qp);
  assert(first);
  /**
   * (from vanilla) It calculates the stats in flush_send_wrs where it
   * does also some additional sanity checks.
   */
  spdk_rdma_qp->stats->send.num_submitted_wrs++;
  last = first;
  while (last->next != NULL) {
    last = last->next;
    spdk_rdma_qp->stats->send.num_submitted_wrs++;
  }

  if (IBV_WR_RDMA_READ == first->opcode) {
    goto stitch_requests;
  }

  /*We seek cases of RDMA_WRITES to the initiator + an nvme_cpl*/
  ptl_cnxt = ptl_cnxt_get();
  if (false == ptl_cnxt->is_target) {
    goto stitch_requests;
  }

  ptl_qp = SPDK_CONTAINEROF(spdk_rdma_qp->qp, struct ptl_qp, fake_qp);

  /*sanity check*/
  if ((IBV_WR_SEND == last->opcode || IBV_WR_SEND_WITH_INV == last->opcode) &&
      last->sg_list[0].length != sizeof(struct spdk_nvme_cpl)) {
    SPDK_PTL_FATAL("Last wr should have been an nvme cpl in this write chain");
  }

  nvme_cid =
      spdk_rdma_provider_ptl_extract_CID((void *)last->sg_list[0].addr, ptl_qp);

  op_meta = calloc(1UL, sizeof(*op_meta));
  op_meta->obj_type = PTL_RDMA_WRITE_OP;
  op_meta->wr_id = last->wr_id;
  op_meta->cq_id = ptl_cq_get_id(ptl_qp->send_cq);
  op_meta->signal_app = last->send_flags & IBV_SEND_SIGNALED;
  op_meta->rdma_write_op.total_parts = 0;
  op_meta->rdma_write_op.qp_num = ptl_qp->ptl_cm_id->ptl_qp_num;
  op_meta->rdma_write_op.addr =
      NULL; /*Whatever, XXX TODO XXX remove the addr field?*/

  last = first;
  while (last->next != NULL) {
    count_parts += last->num_sge;
    PTL_OP_META_SET(last, op_meta);
    PTL_SET_NVME_CID(last, nvme_cid);
    last = last->next;
  }
  /*last is the nvme cpl so we do not count as part*/
  PTL_OP_META_SET(last, op_meta);
  PTL_SET_NVME_CID(last, nvme_cid);
  PTL_SET_NVME_CID_PARTS_NO(last, count_parts);
  op_meta->rdma_write_op.total_parts = count_parts + last->num_sge;
  SPDK_PTL_DEBUG("Write chain detected: {nvme_cid: %u parts (for the remote): "
                 "%u, parts (for the local): %u}",
                 PTL_GET_NVME_CID(last), PTL_GET_NVME_CID_PARTS_NO(last),
                 op_meta->rdma_write_op.total_parts);

stitch_requests:
  if (spdk_rdma_qp->send_wrs.first == NULL) {
    spdk_rdma_qp->send_wrs.first = first;
    spdk_rdma_qp->send_wrs.last = last;
    return true;
  } else {
    spdk_rdma_qp->send_wrs.last->next = first;
    spdk_rdma_qp->send_wrs.last = last;
    return false;
  }
}

static void spdk_rdma_print_wr_flags(struct ibv_send_wr *wr) {
  SPDK_PTL_DEBUG("Work Request Details:");
  SPDK_PTL_DEBUG("  Opcode: %s", wr->opcode == IBV_WR_RDMA_WRITE  ? "RDMA_WRITE"
                                 : wr->opcode == IBV_WR_RDMA_READ ? "RDMA_READ"
                                 : wr->opcode == IBV_WR_SEND      ? "SEND"
                                 : wr->opcode == IBV_WR_SEND_WITH_INV
                                     ? "SEND_WITH_INV"
                                     : "UNKNOWN");

  if (wr->send_flags == 0) {
    SPDK_PTL_DEBUG("  Flags: NONE");
  } else {
    char flags[256] = "";
    if (wr->send_flags & IBV_SEND_SIGNALED) {
      strcat(flags, "IBV_SEND_SIGNALED ");
    }
    if (wr->send_flags & IBV_SEND_FENCE) {
      strcat(flags, "IBV_SEND_FENCE ");
    }
    if (wr->send_flags & IBV_SEND_INLINE) {
      strcat(flags, "IBV_SEND_INLINE ");
    }
    if (wr->send_flags & IBV_SEND_SOLICITED) {
      strcat(flags, "IBV_SEND_SOLICITED ");
    }
    SPDK_PTL_DEBUG("  Flags: %s", flags);
  }

  SPDK_PTL_DEBUG("  Number of SGE: %d", wr->num_sge);

  if (wr->opcode == IBV_WR_RDMA_READ || wr->opcode == IBV_WR_RDMA_WRITE) {
    SPDK_PTL_DEBUG("  RDMA Info:");
    SPDK_PTL_DEBUG("    Remote Addr: 0x%lx", wr->wr.rdma.remote_addr);
    SPDK_PTL_DEBUG("    Remote Key (rkey): 0x%x", wr->wr.rdma.rkey);
  }
  SPDK_PTL_DEBUG("  Next WR: %p", wr->next);
}

/**
 * Performs RDMA read operations using Portals PtlGet.
 *
 * NOTE: Currently sends a separate RDMA read message for each scatter-gather
 * element (SGE) as a temporary workaround until full iovec support is available
 * in BXIv3. The operation metadata tracks all parts to properly handle
 * completion when all data has been read.
 *
 * @param ptl_pd The Portals protection domain
 * @param ptl_qp The Portals queue pair
 * @param wr The send work request containing SGE list and remote address
 * @param match_bits Match bits for the Portals operation
 */
static void spdk_rdma_provider_ptl_rdma_read(struct ptl_pd *ptl_pd,
                                             struct ptl_qp *ptl_qp,
                                             struct ibv_send_wr *wr) {
  ptl_process_t destination = {.phys.nid = ptl_qp->ptl_cm_id->remote_nid,
                               .phys.pid = ptl_qp->ptl_cm_id->remote_pid};
  size_t local_offset;
  ptl_md_t md;
  ptl_msg_t msg;
  int rc;
  struct ptl_context_op_meta *rdma_read_meta = NULL;

  ptl_addr_t md_start;
  ptl_handle_md_t md_handle;
  uint64_t remote_addr;

  SPDK_PTL_CHECK_SGE_LENGTH(wr);

  rdma_read_meta = calloc(1UL, sizeof(*rdma_read_meta));
  rdma_read_meta->obj_type = PTL_RDMA_READ_OP;
  rdma_read_meta->wr_id = wr->wr_id;
  rdma_read_meta->signal_app = wr->send_flags & IBV_SEND_SIGNALED;
  rdma_read_meta->rdma_read_op.qp_num = ptl_qp->ptl_cm_id->ptl_qp_num;
  rdma_read_meta->rdma_read_op.total_parts = wr->num_sge;
  rdma_read_meta->cq_id = ptl_qp->recv_cq->core_cq->cq_id;
  remote_addr = wr->wr.rdma.remote_addr;

  for (int i = 0; i < wr->num_sge; i++) {
    struct ptl_mem_desc *ptl_mem_desc =
        ptl_pd->ops.get(ptl_pd->mem_desc_map, wr->sg_list[i].addr,
                        wr->sg_list[i].length, false);
    if (NULL == ptl_mem_desc) {
      SPDK_PTL_FATAL("Failed to find descriptor");
    }
    md_start = ptl_mem_desc->local.local_w_mem_desc.start;
    md_handle = ptl_mem_desc->local.local_w_mem_handle;

    local_offset = wr->sg_list[i].addr - (uint64_t)md_start;
    // SPDK_PTL_DEBUG(
    // 	"send_wrs: Performing an RDMA read from node nid: %d pid: %d "
    // 	"RMA_PTE:%d local offset: %lu match_bits: %lu is it "
    // 	"signaled?: %s qp_num: %d length: %d remote offset: %lu",
    // 	destination.phys.nid, destination.phys.pid,
    // 	ptl_qp->ptl_cm_id->remote_rma_pte, local_offset,
    // 	ptl_qp->ptl_cm_id->rma_match_bits,
    // 	rdma_read_meta ? "YES" : "NO",
    // 	ptl_qp->ptl_cm_id->ptl_qp_num, wr->sg_list[i].length, remote_addr);
    /*XXX TODO XXX, set match bits correct here!XXX TODO XXX*/
#if PTL_USE_MATCHING
    rc = PtlGet(md_handle, local_offset, wr->sg_list[i].length, destination,
                ptl_qp->ptl_cm_id->remote_rma_pte,
                ptl_qp->ptl_cm_id->rma_match_bits, remote_addr, rdma_read_meta);
#else
    memset(&md, 0x00, sizeof(md));
    md.start = (ptl_addr_t)wr->sg_list[i].addr;
    md.length = wr->sg_list[i].length;
    md.options = 0;
    md.ct_handle = PTL_CT_NONE;
    md.eq_handle = ptl_cq_get_queue(ptl_qp->send_cq);
    // SPDK_PTL_DEBUG("Performing an RDMA read. Completion event will be at
    // send_cq: %d", 	       ptl_qp->send_cq->core_cq->cq_id);
    // SPDK_PTL_DEBUG("Performing an RDMA read. recv_cq: %d",
    // ptl_qp->recv_cq->core_cq->cq_id);
    memset(&msg, 0x00, sizeof(msg));
    msg.local_offset = 0;
    msg.ack_req = PTL_ACK_REQ;
    msg.target_id.phys.nid = destination.phys.nid;
    msg.target_id.phys.pid = destination.phys.pid;
    msg.pt_index = ptl_qp->ptl_cm_id->remote_rma_pte;
    ptl_uuid_set_op_type(&msg.hdr_data, NVMeOF_rma);
    ptl_uuid_set_nvme_cid(&msg.hdr_data, 1);
    ptl_uuid_set_cq_id(&msg.hdr_data, ptl_qp->ptl_cm_id->remote_cq_id);
    msg.remote_offset = remote_addr;
    msg.user_ptr = rdma_read_meta;

    SPDK_PTL_DEBUG(
        "send_wrs: Performing an RDMA read from node nid: %d pid: %d "
        "RMA_PTE:%d user_ptr: 0x%lx local addr: 0x%lx is it "
        "signaled?: %s qp_num: %d length: %d remote offset: 0x%lx",
        msg.target_id.phys.nid, msg.target_id.phys.pid, msg.pt_index,
        rdma_read_meta ? (uint64_t)msg.user_ptr : 0, (uint64_t)md.start,
        rdma_read_meta ? "YES" : "NO", ptl_qp->ptl_cm_id->ptl_qp_num,
        wr->sg_list[i].length, remote_addr);
    rc = PtlMsgGetOnce(ptl_cnxt_get_ni_handle(ptl_cnxt_get()),
                       (const ptl_md_t *)&md, (const ptl_msg_t *)&msg);
#endif

    if (PTL_OK != rc) {
      SPDK_PTL_FATAL(
          "Remote RDMA read failed with error code: {%d (reason)=> %s", rc,
          PtlToStr(rc, PTL_STR_ERROR));
    }
    remote_addr += wr->sg_list[i].length;
  }
}

/**
 * Performs RDMA write operations using Portals PtlPut.
 *
 * NOTE: Currently sends a separate RDMA write message for each scatter-gather
 * element (SGE) as a temporary workaround until full iovec support is available
 * in BXIv3. The operation metadata tracks all parts and counts acknowledgments
 * to properly notify the application only when all data has been written.
 *
 * @param ptl_pd The Portals protection domain
 * @param ptl_qp The Portals queue pair
 * @param wr The send work request containing SGE list and remote address
 * @param match_bits Match bits for the Portals operation
 */
static void spdk_rdma_provider_ptl_rdma_write(struct ptl_pd *ptl_pd,
                                              struct ptl_qp *ptl_qp,
                                              struct ibv_send_wr *wr) {
  struct ptl_mem_desc *ptl_mem_desc;
  size_t local_offset;
  ptl_process_t destination = {.phys.nid = ptl_qp->ptl_cm_id->remote_nid,
                               .phys.pid = ptl_qp->ptl_cm_id->remote_pid};
  ptl_addr_t md_start;
  ptl_handle_md_t md_handle;
  uint64_t remote_addr = wr->wr.rdma.remote_addr;
  ptl_md_t md;
  ptl_msg_t msg;
  int rc;

  SPDK_PTL_CHECK_SGE_LENGTH(wr);

  for (int i = 0; i < wr->num_sge; i++) {
    ptl_mem_desc = ptl_pd->ops.get(ptl_pd->mem_desc_map, wr->sg_list[i].addr,
                                   wr->sg_list[i].length, false);
    if (NULL == ptl_mem_desc) {
      SPDK_PTL_FATAL("Failed to find descriptor");
    }
    md_start = ptl_mem_desc->local.local_w_mem_desc.start;
    md_handle = ptl_mem_desc->local.local_w_mem_handle;

    local_offset = wr->sg_list[i].addr - (uint64_t)md_start;

    SPDK_PTL_DEBUG(
        "send_wrs: Performing an RDMA write (sg[%d] out of %d) to node "
        "nid: %d pid: %d RMA_PTE: %d local offset: "
        "%lu length in B: %u remote_addr: 0x%016lx is it signaled?: %s",
        i, wr->num_sge, destination.phys.nid, destination.phys.pid,
        ptl_qp->ptl_cm_id->remote_rma_pte, local_offset, wr->sg_list[i].length,
        remote_addr, PTL_OP_META_GET(wr) ? "YES" : "NO");

#if PTL_USE_MATCHING
    rc = PtlPut(md_handle,
                local_offset, // local offset
                wr->sg_list[i].length, PTL_ACK_REQ,
                destination, // target process
                ptl_qp->ptl_cm_id->remote_rma_pte,
                ptl_qp->ptl_cm_id->rma_match_bits, remote_addr, rdma_write_meta,
                ptl_qp->ptl_cm_id->session_id);
#else
    md.start = (ptl_addr_t)wr->sg_list[i].addr;
    md.length = wr->sg_list[i].length;
    md.options = 0; // PTL_SRV_ME_OPTS;
    md.ct_handle = PTL_CT_NONE;
    md.eq_handle = ptl_cq_get_queue(ptl_qp->send_cq);

    msg.ack_req = PTL_ACK_REQ;
    msg.target_id.phys.nid = ptl_qp->ptl_cm_id->remote_nid;
    msg.target_id.phys.pid = ptl_qp->ptl_cm_id->remote_pid;
    msg.pt_index = ptl_qp->ptl_cm_id->remote_rma_pte;
    ptl_uuid_set_op_type(&msg.hdr_data, NVMeOF_rma);
    ptl_uuid_set_cq_id(&msg.hdr_data, ptl_qp->ptl_cm_id->remote_cq_id);
    ptl_uuid_set_nvme_cid(&msg.hdr_data, PTL_GET_NVME_CID(wr));
    msg.remote_offset = remote_addr;
    msg.user_ptr = PTL_OP_META_GET(wr);
    msg.length = 0;
    msg.local_offset = 0;
    rc = PtlMsgPutOnce(ptl_cnxt_get_ni_handle(ptl_cnxt_get()),
                       (const ptl_md_t *)&md, (const ptl_msg_t *)&msg);
#endif
    if (PTL_OK != rc) {
      SPDK_PTL_FATAL("Remote RDMA write failed Sorry fault code is: %d!", rc);
    }
    remote_addr += wr->sg_list[i].length;
  }
}

/**
 * Parses, counts, and updates the submitted wrs. In debug mode it also checks
 * the case when the target sends a chain of rdma reads which in the current
 * version is still unsupported
 */
bool spdk_rdma_provider_ptl_parse_wr_list(
    struct spdk_rdma_provider_qp *spdk_rdma_qp) {
  struct ibv_send_wr *wr;
  uint32_t count_rdma_reads = 0;

  for (wr = spdk_rdma_qp->send_wrs.first; wr != NULL;
       wr = wr->next /*, ++spdk_rdma_qp->stats->send.num_submitted_wrs*/) {
    if (wr->opcode == IBV_WR_RDMA_READ) {
      ++count_rdma_reads;
    }
  }
  if (count_rdma_reads > 1) {
    SPDK_PTL_FATAL("Sorry unsupported feature with multiple rdma reads");
  }
  return 0;
}

static inline int
spdk_rdma_provider_ptl_decode_cid(uint16_t cid, uint16_t queue_size, int qpn) {
  uint16_t hw_queue_idx = cid / queue_size;
  uint16_t local_tag = cid % queue_size;

  SPDK_PTL_DEBUG("QPN: %d CID decode: raw=%u hw_queue_idx=%u local_tag=%u", qpn,
                 (unsigned)cid, (unsigned)hw_queue_idx, (unsigned)local_tag);
  return 1;
}

static inline void spdk_rdma_provider_ptl_unmark_wr(struct ibv_send_wr *wr) {
  PTL_OP_META_RESET(wr);
  PTL_SET_NVME_CID(wr, UINT16_MAX);
  PTL_SET_NVME_CID_PARTS_NO(wr, UINT16_MAX);
}

int spdk_rdma_provider_qp_flush_send_wrs(
    struct spdk_rdma_provider_qp *spdk_rdma_qp, struct ibv_send_wr **bad_wr) {

  assert(spdk_rdma_qp);
  // assert(bad_wr);
  int rc;
  struct ptl_qp *ptl_qp = ptl_qp_get_from_ibv_qp(spdk_rdma_qp->qp);
  struct ptl_pd *ptl_pd = ptl_qp_get_pd(ptl_qp);
  ptl_process_t target = {.phys.nid = ptl_qp->ptl_cm_id->remote_nid,
                          .phys.pid = ptl_qp->ptl_cm_id->remote_pid};
  ptl_addr_t md_start;
  ptl_handle_md_t md_handle;
  uint64_t local_offset;
  ptl_md_t md;
  ptl_msg_t msg;
  u16 nvme_cid;
  struct ptl_mem_desc *ptl_mem_desc;
  struct ibv_send_wr *next_wr = NULL;

  if (spdk_unlikely(NULL == spdk_rdma_qp->send_wrs.first)) {
    return 0;
  }

  // spdk_rdma_provider_ptl_parse_wr_list(spdk_rdma_qp);/*XXX TODO XXX Probably
  // redundant*/
  SPDK_PTL_DEBUG("send_wrs list start....QPN: %d",
                 ptl_qp->ptl_cm_id->ptl_qp_num);
  for (struct ibv_send_wr *wr = spdk_rdma_qp->send_wrs.first; wr != NULL;
       wr = next_wr) {
    /* Detach before processing: these WRs are recycled, and a stale ->next
     * makes queue_send_wrs() splice an already-sent chain back onto the list,
     * which is then re-flushed forever and the commands never retire. */
    next_wr = wr->next;
    wr->next = NULL;

    // spdk_rdma_print_wr_flags(wr);
    if (wr->opcode == IBV_WR_RDMA_READ) {
      SPDK_PTL_DEBUG("send_wrs: RDMA read. QPN: %d remote PTE: %d signaled? %s",
                     ptl_qp->ptl_cm_id->ptl_qp_num,
                     ptl_qp->ptl_cm_id->remote_rma_pte,
                     wr->send_flags & IBV_SEND_SIGNALED ? "YES" : "NO");
      spdk_rdma_provider_ptl_rdma_read(ptl_pd, ptl_qp, wr);
      continue;
    }

    if (wr->opcode == IBV_WR_RDMA_WRITE) {
      SPDK_PTL_DEBUG("send_wrs RDMA write for QPN: %d PTE: %d. signaled? %s",
                     ptl_qp->ptl_cm_id->ptl_qp_num,
                     ptl_qp->ptl_cm_id->remote_rma_pte,
                     wr->send_flags & IBV_SEND_SIGNALED ? "YES" : "NO");
      if (!PTL_OP_META_IS_SET(wr)) {
        SPDK_PTL_FATAL("RDMA write without a mark!");
      }
      spdk_rdma_provider_ptl_rdma_write(ptl_pd, ptl_qp, wr);
      spdk_rdma_provider_ptl_unmark_wr(wr);
      continue;
    }

    SPDK_PTL_DEBUG("send_wrs RDMA send QPN: %d remote PTE: %d. signaled? %s",
                   ptl_qp->ptl_cm_id->ptl_qp_num,
                   ptl_qp->ptl_cm_id->remote_msg_pte,
                   wr->send_flags & IBV_SEND_SIGNALED ? "YES" : "NO");

    SPDK_PTL_IS_SGE_LENGTH_ONE(wr);
    for (int i = 0; i < wr->num_sge; i++) {

      ptl_mem_desc = ptl_pd->ops.get(ptl_pd->mem_desc_map, wr->sg_list[i].addr,
                                     wr->sg_list[i].length, false);
      if (NULL == ptl_mem_desc) {
        SPDK_PTL_FATAL("MEM desc not found!");
      }
      md_start = ptl_mem_desc->local.local_w_mem_desc.start;
      md_handle = ptl_mem_desc->local.local_w_mem_handle;

      SPDK_PTL_DEBUG(
          "OK: \n%d",
          wr->sg_list[i].length == 64
              ? ptl_print_nvme_cmd(
                    (const struct spdk_nvme_cmd *)wr->sg_list[i].addr,
                    "NVMe-cmd-send-og", ptl_cq_get_id(ptl_qp->send_cq))
              : ptl_print_nvme_cpl(
                    (const struct spdk_nvme_cpl *)wr->sg_list[i].addr,
                    "NVMe-cpl-send-og", ptl_cq_get_id(ptl_qp->send_cq)));

      local_offset = wr->sg_list[i].addr - (uint64_t)md_start;

      SPDK_PTL_DEBUG(
          "send_wrs: %s: Performing a SEND (PtlPut) operation to nid: "
          "%d pid: %d remote pte: %d initiator qp_num: %d "
          "target qp_num: %d local_offset: %lu is it "
          "signaled?: %s",
          wr->sg_list[i].length == 16 ? "NVMe-cpl-send" : "NVMe-cmd-send",
          target.phys.nid, target.phys.pid, ptl_qp->ptl_cm_id->remote_msg_pte,
          ptl_qp->ptl_cm_id->initiator_qp_num, ptl_qp->ptl_cm_id->target_qp_num,
          local_offset, PTL_OP_META_GET(wr) ? "YES" : "NO");
#if PTL_USE_MATCHING
      rc = PtlPut(md_handle,
                  local_offset,          // local offset
                  wr->sg_list[i].length, // length
                  PTL_ACK_REQ,
                  target,                            // target process
                  ptl_qp->ptl_cm_id->remote_msg_pte, // portal table index
                  ptl_qp->ptl_cm_id->recv_match_bits,
                  0, // remote offset, don't care let target decide
                  send_meta, ptl_qp->ptl_cm_id->session_id);
#else
      md.start = (ptl_addr_t)wr->sg_list[i].addr;
      md.length = wr->sg_list[i].length;
      md.ct_handle = PTL_CT_NONE;
      md.eq_handle = ptl_cq_get_queue(ptl_qp->send_cq);
      md.options = 0; // PTL_SRV_ME_OPTS;

      msg.ack_req = PTL_ACK_REQ;
      msg.target_id.phys.nid = ptl_qp->ptl_cm_id->remote_nid;
      msg.target_id.phys.pid = ptl_qp->ptl_cm_id->remote_pid;
      msg.pt_index = ptl_qp->ptl_cm_id->remote_msg_pte;
      ptl_uuid_set_op_type(
          &msg.hdr_data,
          md.length == sizeof(struct spdk_nvme_cmd) ? NVMeOF_cmd : NVMeOF_cpl);
      ptl_uuid_set_cq_id(&msg.hdr_data, ptl_qp->ptl_cm_id->remote_cq_id);

      /*<gesalous> non-matching feat*/
      msg.local_offset = 0;
      msg.remote_offset = 0;
      msg.user_ptr = PTL_OP_META_GET(wr);
      msg.length = 0;
      if (ptl_qp->ptl_cm_id->remote_is_a_kernel_initiator) {
        nvme_cid = spdk_rdma_provider_ptl_extract_CID(
            (void *)wr->sg_list[0].addr, ptl_qp);
        ptl_uuid_set_nvme_cid(&msg.hdr_data, nvme_cid);
        // if (nvme_cid != PTL_GET_NVME_CID(wr)) {
        //   SPDK_PTL_FATAL("Corrupted CID? nvme_cid = %u stored in wr is %u Is
        //   "
        //                  "it marked? %s",
        //                  nvme_cid, PTL_GET_NVME_CID(wr),
        //                  PTL_WR_IS_MARKED(wr) ? "YES" : "NO");
        // }
        /*Some paths (probably ping), do not call enqueue_wrs XXX TODO XXX*/
        ptl_uuid_set_total_parts(
            &msg.hdr_data,
            PTL_OP_META_IS_SET(wr) ? PTL_GET_NVME_CID_PARTS_NO(wr) : 0);
        msg.remote_offset = ptl_qp->ptl_cm_id->remote_nvme_cpl_start_addr +
                            (nvme_cid * sizeof(struct spdk_nvme_cpl));
        SPDK_PTL_DEBUG("Sending nvme completion. {Initiator QPN: %d, Base "
                       "IOVA: %lu, cid: %u, remote_offset = "
                       "%lu}",
                       ptl_qp->ptl_cm_id->initiator_qp_num,
                       ptl_qp->ptl_cm_id->remote_nvme_cpl_start_addr, nvme_cid,
                       msg.remote_offset);
      }

      rc = PtlMsgPutOnce(ptl_cnxt_get_ni_handle(ptl_cnxt_get()),
                         (const ptl_md_t *)&md, (const ptl_msg_t *)&msg);
#endif

      if (rc != PTL_OK) {
        SPDK_PTL_FATAL("PtlPut failed with rc: %d", rc);
      }
      spdk_rdma_provider_ptl_unmark_wr(wr);
    }
  }
  SPDK_PTL_DEBUG("send_wrs list end SUCCESS for QPN: %d",
                 ptl_qp->ptl_cm_id->ptl_qp_num);
  // rc = ibv_post_send(spdk_rdma_qp->qp, spdk_rdma_qp->send_wrs.first, bad_wr);

  spdk_rdma_qp->send_wrs.first = NULL;
  spdk_rdma_qp->send_wrs.last = NULL;
  spdk_rdma_qp->stats->send.doorbell_updates++;
  SPDK_PTL_DEBUG("NVMe: Flushing send requests....DONE\n");
  return 0;
}

bool spdk_rdma_provider_accel_sequence_supported(void) {
  SPDK_PTL_DEBUG("No accel sequence supported in PORTALS");
  return false;
}
