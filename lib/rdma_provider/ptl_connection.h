#ifndef PTL_CONNECTION_H
#define PTL_CONNECTION_H
#include <rdma/rdma_cma.h>
#include <stdint.h>
typedef enum {PTL_OPEN_CONNECTION = 12, PTL_OPEN_CONNECTION_REPLY, PTL_CLOSE_CONNECTION, PTL_CLOSE_CONNECTION_REPLY} ptl_conn_msg_type_e;

struct ptl_conn_comm_pair_info {
	/*Initiator of the communication info*/
	int src_nid;
	int src_pid;
	/*PTE entry where initiator expects reply*/
	int src_pte;
	/*Target of the communication info*/
	int dst_nid;
	int dst_pid;
	/*PTE entry of the target*/
	int dst_pte;
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
	/*Where initiator has MEs for recv operations*/
	uint64_t recv_match_bits;
	/*Where initiator has an ME for remote read/write operations*/
	uint64_t rma_match_bits;
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

