#ifndef PTL_CONNECTION_H
#define PTL_CONNECTION_H
#include "ptl_config.h"
#include <rdma/rdma_cma.h>
#include <stdint.h>
typedef enum {NVMeOF_cmd = 0, NVMeOF_cpl, PTL_OPEN_CONNECTION, PTL_OPEN_CONNECTION_REPLY, PTL_CLOSE_CONNECTION, PTL_CLOSE_CONNECTION_REPLY, PTL_NUM_MSGS} ptl_conn_msg_type_e;

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
#if PTL_ENABLE_MATCHING
	/*Where initiator has MEs for recv operations*/
	uint64_t recv_match_bits;
	/*Where initiator has an ME for remote read/write operations*/
	uint64_t rma_match_bits;
#else
	/*PTL entry where to send the nvme-cpls*/
	int nvme_cpl_pte;
	/*PTL entry where to send the target can send the rma operations*/
	int nvme_rma_ops_pte;
#endif
	/*In which completion queue id initiator has subscribed for notifications*/
	int cq_id;
	int initiator_qp_num;
	struct rdma_conn_param conn_param;
};

struct ptl_conn_open_reply {
	uint64_t uuid;
	/*Where the target has buffer for receive operations*/
	uint64_t srq_match_bits;
	/*Where I wait for recv events and staff*/
	int cq_id;
	int status;
	/*PTE where target has assigned the queue (qpair->IO queue)*/
	int target_dp_pte;
	struct rdma_conn_param conn_param;
};

struct ptl_conn_close {
	uint64_t uuid;
};

struct ptl_conn_close_reply {
	uint64_t uuid;
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

