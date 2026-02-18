#ifndef BXIV3_DEVICE_H
#define BXIV3_DEVICE_H
#include "ptl_connection.h"
#include "ptl_cq_pool.h"
#include "ptl_object_types.h"
#include "ptl_qp.h"
#include <linux/hashtable.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <portals4.h>
#include <portals4_bxiext.h>
#include <rdma/ib_verbs.h>
#define PTL_BXIV3_DEVICE_MAX_PTES 256
#define PTL_BXIV3_DEVICE_MAX_INLINE_SEGMENTS 32

#define PTL_BXIV3_DEVICE_CONNECTION_NUM_RECV_BUFFERS 32

#define PTL_SRV_ME_OPTS   (PTL_ME_OP_PUT | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_MAY_ALIGN | PTL_ME_IS_ACCESSIBLE | PTL_ME_NO_TRUNCATE | PTL_ME_USE_ONCE)
#define PTL_BXIV3_QP_HT_BITS 8    /* 2^8 = 256 buckets for the queue pair map*/

struct ptl_bxiv3_qp_map_entry {
	int key;
	struct ptl_qp *ptl_qp;
	struct hlist_node node;
};


struct ptl_bxiv3_device {
	ptl_obj_type_e object_type;
	ptl_handle_ni_t nicia_handle;
	ptl_ni_limits_t actual;
	ptl_process_t proc_id;
	/*Warning, currently there is a single interrupt index per NIC*/
	ptl_eq_intr_index_t intr_index;
	DECLARE_BITMAP(pte_table, PTL_BXIV3_DEVICE_MAX_PTES);
	spinlock_t pte_table_lock;
	struct ptl_cq_pool *ptl_cq_pool;

	/*Event queue only for the connection staff (PTL_OPEN_CONNECTION_REQUEST,...)*/
	struct ptl_cq *conn_mgmt_eq;
	/*List of receive buffers for the connetion server*/
	struct list_head conn_buffer_list;

	/*RMA related staff*/
	/*Event queue for the rma operations from the target to the initiator*/
	struct ptl_cq *rma_operations_eq;
	ptl_le_t rma_le;
	ptl_handle_le_t rma_leh;
	int rma_pte;

	struct kref count;
	/* XXX TODO XXX, list with cm_id/qps? created on this device */
	u32 iface_id;
	struct ib_device fake_ib_dev;
	DECLARE_HASHTABLE(qp_map, PTL_BXIV3_QP_HT_BITS);
	spinlock_t qp_map_lock;
};

struct ptl_bxiv3_device *ptl_bxiv3_dev_create(u32 iface_id);
ptl_pt_index_t ptl_bxiv3_dev_alloc_pte(struct ptl_bxiv3_device *bxiv3_dev);
ptl_pt_index_t ptl_bxiv3_dev_free_pte(struct ptl_bxiv3_device *bxiv3_dev,
                                      ptl_pt_index_t pte);
int ptl_bxiv3_dev_destroy(struct ptl_bxiv3_device *ptl_bxiv3_dev);
#endif
