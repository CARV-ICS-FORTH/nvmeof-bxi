#include "ptl_cq.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include <asm-generic/errno-base.h>
#include <linux/err.h>
#include <portals4.h>
#include <portals4_bxiext.h>

void ptl_eq_callback(void *arg, ptl_handle_eq_t eqh) {
  struct ptl_cq *ptl_cq = arg;
  if (PTL_CQ != ptl_cq->obj_type) {
    PTL_FATAL("Corrupted PTL_CQ");
    return;
  }
  PTL_DEBUG("Ok something came from BXIv3 device: %d",
            ptl_cq->cq_pool->bxiv3_device->iface_id);
}

struct ptl_cq *ptl_cq_create(struct ptl_cq_pool *cq_pool, int nr_cqes,
                             ptl_pt_index_t pte, enum ib_poll_context poll_ctx,
                             cpumask_t *cpu_mask) {
  struct ptl_cq *cq;
  int rc;
  if (poll_ctx != IB_POLL_SOFTIRQ) {
    PTL_FATAL("Sorry! Currently BXIv3 NVMe-oF initiator supports only "
              "IB_POLL_SOFTIRQ mode");
    return ERR_PTR(-EINVAL);
  }
  if (!cq_pool) {
    PTL_FATAL("NULL cq_pool?");
    return ERR_PTR(-EINVAL);
  }

  cq = kzalloc(sizeof(*cq), GFP_KERNEL);
  if (!cq) {
    PTL_FATAL("Out of memory");
    return ERR_PTR(-ENOMEM);
  }

  cq->obj_type = PTL_CQ;
  cq->cq_pool = cq_pool;
  cq->pte = pte;
  cq->nr_cqes = nr_cqes;

  rc = PtlEQAsyncIntrAlloc(cq_pool->bxiv3_device->nicia_handle, cpu_mask,
                           &cq->intr_index);
  if (PTL_OK != rc) {
    PTL_FATAL(
        "Failed to allocate interrupt index for BXIv3 with error code: %d", rc);
    goto err;
  }
  rc = PtlEQAllocAsync(cq_pool->bxiv3_device->nicia_handle, cq->nr_cqes,
                       &cq->eq, ptl_eq_callback, cq, cq->intr_index);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to allocate event queue for pte: %d", pte);
    goto err;
  }
  rc = PtlPTAlloc(cq_pool->bxiv3_device->nicia_handle, 0, cq->eq, cq->pte,
                  &cq->pte);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to allocate PTE: %d with reason: %d", pte, rc);
    goto free_cq;
  }
  PTL_DEBUG("Created cq for PTE: %d with %d number of entries", pte, nr_cqes);
  return cq;
free_cq:
  rc = PtlEQFree(cq->eq);
  if (PTL_OK != rc)
    PTL_WARN("Failed to free event queue with code: %d, continuing", rc);
err:
  kfree(cq);
  return ERR_PTR(-EINVAL);
}

ptl_pt_index_t ptl_cq_destroy(struct ptl_cq *ptl_cq) {
  int rc;
  ptl_pt_index_t pte;

  if (!ptl_cq) {
    PTL_WARN("Attempting to destroy NULL cq");
    return -1;
  }
  pte = ptl_cq->pte;

  rc = PtlEQAsyncIntrFree(ptl_cq->cq_pool->bxiv3_device->nicia_handle,
                          ptl_cq->intr_index);
  if (PTL_OK != rc) {
    PTL_WARN("Failed to destroy interrupt vector with error code: %d. OK, life "
             "goes on",
             rc);
  }
  /* Free the portal table entry */
  rc = PtlPTFree(ptl_cq->cq_pool->bxiv3_device->nicia_handle, ptl_cq->pte);
  if (PTL_OK != rc) {
    PTL_WARN("Failed to free PTE: %d with code: %d, continuing", ptl_cq->pte,
             rc);
  }

  /* Free the event queue */
  rc = PtlEQFree(ptl_cq->eq);
  if (PTL_OK != rc) {
    PTL_WARN("Failed to free event queue with code: %d, continuing", rc);
  }
  PTL_DEBUG("Destroyed cq for PTE: %d", ptl_cq->pte);
  /* Free the cq structure */
  kfree(ptl_cq);
  return pte;
}
