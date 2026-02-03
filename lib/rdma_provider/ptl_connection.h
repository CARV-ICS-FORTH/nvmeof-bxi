#ifndef PTL_CONNECTION_H
#define PTL_CONNECTION_H
#include "ptl_config.h"
#include <netinet/in.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <sys/socket.h>
typedef enum {NVMeOF_cmd = 0, NVMeOF_cpl, NVMeOF_rma, PTL_OPEN_CONNECTION, PTL_OPEN_CONNECTION_REPLY, PTL_CLOSE_CONNECTION, PTL_CLOSE_CONNECTION_REPLY, PTL_NUM_MSGS} ptl_conn_msg_type_e;

struct ptl_conn_comm_pair_info {
	/*Initiator of the communication info*/
	struct src {
		int nid;
		int pid;
		/*PTE entry where initiator's control plane server expects reply*/
		int pte;
	} src, dest;
};

struct ptl_conn_msg_header {
	ptl_conn_msg_type_e msg_type;
	uint64_t version;
	/*Due to rdma conn_param not all messages are of fixed size*/
	uint64_t total_msg_size;
	struct ptl_conn_comm_pair_info peer_info;
};

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
	struct rdma_conn_param conn_param;
};

struct ptl_conn_open_reply {
	/*Contains initiator target qp nums*/
	uint64_t session_id;
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
};

struct ptl_conn_close {
	uint64_t session_id;
};

struct ptl_conn_close_reply {
	uint64_t session_id;
	int status;
};

struct ptl_conn_msg {
	struct ptl_conn_msg_header msg_header;
	union {
		struct ptl_conn_open conn_open;
		struct ptl_conn_open_reply conn_open_reply;
		struct ptl_conn_close conn_close;
		struct ptl_conn_close_reply conn_close_reply;
	};
};
#endif

