#ifndef PTL_CONNECTION_H
#define PTL_CONNECTION_H
#include "ptl_config.h"
#include <netinet/in.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <sys/socket.h>
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
	uint32_t pad;
	uint64_t version;
	/*Due to rdma conn_param not all messages are of fixed size*/
	uint64_t total_msg_size;
	struct ptl_conn_comm_pair_info peer_info;
} __attribute((packed));

struct ptl_conn_open {
	struct sockaddr src_addr;
	/*PTE where there are buffers for recv operations (either NVMe-cpls)*/
	int msg_pte;
	/*PTE where there are buffers for RMA operations or -1 if not supported*/
	int rma_pte;
#if PTL_USE_MATCHING
	/*Where initiator has MEs for recv operations*/
	uint64_t recv_match_bits;
	/*Where initiator has an ME for remote read/write operations*/
	uint64_t rma_match_bits;
#endif
	/*In which completion queue id initiator has subscribed for notifications*/
	int cq_id;
	int initiator_qp_num;
	/*extensions for nvme cpls, start*/
	int is_kernel_initiator;
	uint64_t nvme_cpl_start_addr;
	size_t nvme_cpl_queue_size;
	/*extensions for nvme cpls, end*/
	struct rdma_conn_param conn_param;
} __attribute((packed));

struct ptl_conn_open_reply {
	/*Contains initiator target qp nums*/
	uint16_t initiator_qp_num;
	uint16_t target_qp_num;
	/*PTE where there are buffers for receive operations (NVMe-cmd)*/
	int msg_pte;
	/*
	 * PTE where there are buffers for RMA  ops (it will be -1 RMA from initiator to target not allowed).
	 * We leave it as a possible future extension.
	 * */
	int rma_pte;
#if PTL_USE_MATCHING
	/*Where the target has buffer for receive operations*/
	uint64_t srq_match_bits;
#endif
	/*Where I wait for recv events and staff*/
	int cq_id;
	int status;
	struct rdma_conn_param conn_param;
} __attribute((packed));

struct ptl_conn_close {
	uint16_t initiator_qp_num;
	uint16_t target_qp_num;
} __attribute((packed));

struct ptl_conn_close_reply {
	uint16_t initiator_qp_num;
	uint16_t target_qp_num;
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
#endif

