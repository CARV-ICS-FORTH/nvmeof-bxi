#ifndef PTL_CONNECTION_H
#define PTL_CONNECTION_H
#include "ptl_object_types.h"
#include <linux/socket.h>
#include <linux/types.h>
#include <portals4.h>
#include <rdma/rdma_cm.h>
#define PTL_SPDK_PROTOCOL_VERSION 1UL

/**
 * PTE for the connection server*/
#define PTL_CP_SERVER_PTE 0
/**
 * PTE where the kernel appends an LE for the target to
 * perform RMA operations
 */
#define PTL_RMA_PTE 1
#define PTL_CP_SERVER_CQ_ENTRIES 128
typedef enum {
	NVMeOF_cmd = 0,
	NVMeOF_cpl,
	NVMeOF_rma,
	PTL_OPEN_CONNECTION,
	PTL_OPEN_CONNECTION_REPLY,
	PTL_CLOSE_CONNECTION,
	PTL_CLOSE_CONNECTION_REPLY,
	PTL_NUM_MSGS
} ptl_conn_msg_type_e;

struct ptl_conn_comm_pair_info {
	/*Initiator of the communication info*/
	struct src {
		int nid;
		int pid;
		/*PTE entry where initiator's control plane server expects reply*/
		int pte;
	} src, dest;
} __attribute((packed));

struct ptl_conn_msg_header {
	ptl_conn_msg_type_e msg_type;
	u32 pad;
	u64 version;
	/*Due to rdma conn_param not all messages are of fixed size*/
	u64 total_msg_size;
	struct ptl_conn_comm_pair_info peer_info;
} __attribute((packed));

struct ptl_conn_open {
	struct sockaddr src_addr;
	/*PTE where there are buffers for recv operations (either NVMe-cpls)*/
	int msg_pte;
	/*PTE where there are buffers for RMA operations or -1 if not supported*/
	int rma_pte;
	int cq_id;
	int initiator_qp_num;
	/*extensions for nvme cpls, start*/
	int is_kernel_initiator;
	u64 nvme_cpl_start_addr;
	size_t nvme_cpl_queue_size;
	/*extensions for nvme cpls, end*/
	struct rdma_conn_param conn_param;
} __attribute((packed));

struct ptl_conn_open_reply {
	u16 initiator_qp_num;
	u16 target_qp_num;
	int msg_pte;
	int rma_pte;
	int cq_id;
	int status;
	struct rdma_conn_param conn_param;
} __attribute((packed));

struct ptl_conn_close {
	u16 initiator_qp_num;
	u16 target_qp_num;
} __attribute((packed));

struct ptl_conn_close_reply {
	u16 initiator_qp_num;
	u16 target_qp_num;
	int status;
} __attribute((packed));

struct ptl_conn_msg {
	struct ptl_conn_msg_header msg_header;
	union {
		struct ptl_conn_open conn_open;
		struct ptl_conn_open_reply conn_open_reply;
		struct ptl_conn_close conn_close;
		struct ptl_conn_close_reply conn_close_reply;
	};
} __attribute((packed));


struct ptl_conn_send_buffer {
	ptl_obj_type_e object_type;
	ptl_md_t md;
	ptl_handle_md_t md_handle;
	struct ptl_conn_msg conn_msg;
} __attribute((packed));

struct ptl_conn_recv_buffer {
	ptl_obj_type_e object_type;
	struct ptl_conn_msg *conn_msg;
	ptl_le_t le;
	ptl_handle_le_t leh;
	struct list_head head;
};
#endif

