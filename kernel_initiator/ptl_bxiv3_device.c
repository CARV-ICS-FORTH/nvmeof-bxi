#include "ptl_bxiv3_device.h"
#include "asm-generic/errno-base.h"
#include "linux/err.h"
#include "linux/kref.h"
#include "linux/list.h"
#include "linux/spinlock.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include <linux/slab.h>
#include <portals4.h>

struct ptl_bxiv3_device *ptl_bxiv3_dev_create(u32 iface_id) {
  struct ptl_bxiv3_device *bxiv3_dev;
  int rc;

  bxiv3_dev = kzalloc(sizeof(*bxiv3_dev), GFP_KERNEL);
  if (!bxiv3_dev) {
    PTL_FATAL("Out of memory");
    return ERR_PTR(-ENOMEM);
  }

  bxiv3_dev->obj_type = PTL_BXIV3_DEVICE;
  spin_lock_init(&bxiv3_dev->pte_table_lock);
  bxiv3_dev->iface_id = iface_id;
  kref_init(&bxiv3_dev->count);

  PTL_DEBUG("Initializing BXIv3 device with id: %d...", iface_id);
  rc = PtlNIInit(iface_id, PTL_NI_MATCHING | PTL_NI_PHYSICAL, PTL_PID_ANY, NULL,
                 &bxiv3_dev->actual, &bxiv3_dev->nicia_handle);
  if (PTL_OK != rc) {
    PTL_DEBUG("PtlNIInit() failed with code: %d no ifcace: %d", rc, iface_id);
    goto clean_up;
  }

  rc = PtlGetPhysId(bxiv3_dev->nicia_handle, &bxiv3_dev->proc_id);
  if (PTL_OK != rc) {
    PTL_FATAL("Failed to get physical id of #iface: %d reason: %d", iface_id,
              rc);
    goto clean_up;
  }
  PTL_INFO("Initialized interface: %d with nid: %d or (>>7:%d) and pid: %d "
           "successfully",
           iface_id, bxiv3_dev->proc_id.phys.nid,
           bxiv3_dev->proc_id.phys.nid >> 7, bxiv3_dev->proc_id.phys.pid);
  bxiv3_dev->ptl_cq_pool = ptl_cq_pool_create(bxiv3_dev);
  return bxiv3_dev;
clean_up:
  kfree(bxiv3_dev);
  return ERR_PTR(-rc);
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
  int rc;

  if (!bxiv3_dev)
    return -EINVAL;

  /* Destroy the pool */
  rc = ptl_cq_pool_destroy(bxiv3_dev->ptl_cq_pool);
  if (rc) {
    PTL_WARN("Failed to destroy CQ pool: %d", rc);
    goto error;
  }

  PTL_DEBUG("ptl_cq_pool destroyed");
  /* Finalize the NI handle */
  PtlNIFini(bxiv3_dev->nicia_handle);
  kfree(bxiv3_dev);
  return 0;
error:
  return -EBUSY;
}
