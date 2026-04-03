#ifndef PTL_CONTEXT_H
#define PTL_CONTEXT_H
#include "../../include/spdk/nvme_spec.h"
#include "ptl_config.h"
#include "ptl_object_types.h"
#include <infiniband/verbs.h>
#include <portals4.h>
#include <portals4_bxiext.h>
#include <stdbool.h>
#define PTL_CONTEXT_SERVER_PID 0
#define PTL_IOVEC_SIZE 2
struct ptl_context;
struct ibv_context;
struct ibv_pd;

struct ptl_context_recv_op {
	ptl_iovec_t io_vector[PTL_IOVEC_SIZE];
#if PTL_USE_MATCHING
	ptl_me_t me;
	ptl_handle_me_t me_handle;
#else
	ptl_le_t le;
	ptl_handle_le_t le_handle;
#endif
	uint64_t bytes_received;
	int initiator_qp_num;
	int target_qp_num;
	/*In which cqid I wait for the receive event*/
	int cq_id;
	bool receive_done;
};

struct ptl_context_send_op {
	uint64_t crc_checksum;
	void *addr;
	int qp_num;
	int length;
	bool signal_app;
};

struct ptl_context_rdma_write_op {
	uint32_t total_parts;
	uint32_t parts_acked;
	void *addr;
	int qp_num;
	int length;
};

struct ptl_context_rdma_read_op {
	uint32_t total_parts;
	uint32_t parts_acked;
};

struct ptl_context_op_meta {
	ptl_obj_type_e obj_type;
#if !PTL_USE_MATCHING
	// ptl_md_t md;
	// ptl_msg_t msg;
#endif
	uint64_t wr_id;
#if PTL_ENABLE_BIND_PER_OP
	ptl_md_t md_desc[PTL_MAX_SG_LIST];
	ptl_handle_md_t md_handle[PTL_MAX_SG_LIST];
#endif
	uint16_t nvme_cid;
	int cq_id;
	union {
		struct ptl_context_send_op send_op;
		struct ptl_context_recv_op recv_op;
		struct ptl_context_rdma_write_op rdma_write_op;
		struct ptl_context_rdma_write_op rdma_read_op;//?
	};
	bool signal_app;
};


struct ptl_context {
	ptl_obj_type_e object_type;
	ptl_handle_ni_t ni_handle;
	ptl_pt_index_t portals_idx_send_recv;
	ptl_pt_index_t portals_idx_rma;
	/*gesalous, portals staff*/
	struct ptl_pd *ptl_pd;
	struct ibv_context fake_ibv_cnxt;
	struct ibv_cq fake_cq;
	/**
	 * Keeps which PTEs have not been assigned to a shared receive queue
	 * and their correspoding size. 0 free, 1 in use
	**/
	uint8_t *pte_table;
	uint32_t pte_table_size;
	pthread_mutex_t pte_table_lock;
	// struct spdk_rdma_provider_srq *srq;
	int pid;
	int nid;
#if !PTL_USE_MATCHING
	ptl_handle_eq_t rma_event_queue;
	ptl_pt_index_t rma_pte_handle;
#endif
	bool is_target;
	bool initialized;
};

struct ptl_context *ptl_cnxt_get(void);
struct ibv_context *ptl_cnxt_get_ibv_context(struct ptl_context *cnxt);
struct ptl_context *ptl_cnxt_get_from_ibcnxt(struct ibv_context *ib_cnxt);
struct ptl_context *ptl_cnxt_get_from_ibvpd(struct ibv_pd *ib_pd);


ptl_pt_index_t ptl_cnxt_get_portal_index(struct ptl_context *cnxt);
ptl_handle_ni_t ptl_cnxt_get_ni_handle(struct ptl_context *cnxt);

static inline int ptl_cnxt_get_nid(struct ptl_context *cnxt)
{
	return cnxt->nid;
}

static inline int ptl_cnxt_get_pid(struct ptl_context *cnxt)
{
	return cnxt->pid;
}


/**
  * Allocates a free pte typically for use for a new srq
**/
int ptl_cnxt_allocate_pte(struct ptl_context *cnxt);

int ptl_cnxt_free_pte(struct ptl_context *ptl_cnxt, int pte);

int ptl_cnxt_get_pte(struct ptl_context *cnxt, int pte);

/**
  * Returns the PTE responsdile (that has an LE) for RMA operations
*/
int ptl_cnxt_get_rma_pte(struct ptl_context *cnxt);

#endif

