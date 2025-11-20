#ifndef BXIV3_DEVICE_H
#define BXIV3_DEVICE_H
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <portals4.h>
#include <rdma/ib_verbs.h>
#define PTL_BXIV3_DEVICE_MAX_PTES 256
#define PTL_BXIV3_DEVICE_MAX_INLINE_SEGMENTS 32
struct ptl_bxiv3_device {
  ptl_obj_type_e obj_type;
  ptl_handle_ni_t nicia_handle;
  ptl_ni_limits_t actual;
  ptl_process_t proc_id;
  DECLARE_BITMAP(pte_table, PTL_BXIV3_DEVICE_MAX_PTES);
  spinlock_t pte_table_lock;
  struct ptl_cq_pool *ptl_cq_pool;
  struct kref count;
  /* XXX TODO XXX, list with cm_id/qps? created on this device */
  u32 iface_id;
  struct ib_device fake_ib_dev;
};

struct ptl_bxiv3_device *ptl_bxiv3_dev_create(u32 iface_id);
ptl_pt_index_t ptl_bxiv3_dev_alloc_pte(struct ptl_bxiv3_device *bxiv3_dev);
ptl_pt_index_t ptl_bxiv3_dev_free_pte(struct ptl_bxiv3_device *bxiv3_dev,
                                      ptl_pt_index_t pte);
int ptl_bxiv3_dev_destroy(struct ptl_bxiv3_device *ptl_bxiv3_dev);
#endif
