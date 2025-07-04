#include "deque.h"
#include "dlist.h"
#include "ptl_object_types.h"
#include "ptl_cm_id.h"
#include "ptl_config.h"
#include "ptl_connection.h"
#include "ptl_context.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_macros.h"
#include "ptl_pd.h"
#include "ptl_qp.h"
#include "ptl_uuid.h"
#include "rdma_cm_ptl_event_channel.h"
#include "spdk/util.h"
#include <asm-generic/errno-base.h>
#include <dlfcn.h>
#include <infiniband/verbs.h>
#include <netinet/in.h>
#include <portals4.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include <spdk/nvmf_spec.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#define RDMA_PTL_MAX_CONNECTIONS 128UL

#define RDMA_PTL_IGNORE 0xFFFFFFFFFFFFFFFFUL

#define RDMA_PTL_MATCH 0

#define RDMA_PTL_CLIENT_MAILBOX_BUFFERS 16UL

#define RDMA_PTL_SRV_ME_OPTS                                                                                                 \
	PTL_ME_OP_PUT | PTL_ME_EVENT_LINK_DISABLE | PTL_ME_MAY_ALIGN | PTL_ME_IS_ACCESSIBLE | PTL_ME_MANAGE_LOCAL | \
		PTL_ME_NO_TRUNCATE | PTL_LE_USE_ONCE

#define RDMA_PTL_MSG_BUFFER_SIZE 256UL

#define PTL_CP_SERVER_LOCK(X) do { \
    int ret = pthread_mutex_lock(X); \
    if (ret != 0) { \
        SPDK_PTL_FATAL("pthread_mutex_lock failed: %s", strerror(ret)); \
    } \
} while(0)

#define PTL_CP_SERVER_UNLOCK(X) do { \
    int ret = pthread_mutex_unlock(X); \
    if (ret != 0) { \
        SPDK_PTL_FATAL("pthread_mutex_lock failed: %s", strerror(ret)); \
    } \
} while(0)

volatile int is_target;

static void rdma_ptl_write_event_to_fd(int fd)
{
	uint64_t value = 1;
	if (write(fd, &value, sizeof(value)) !=
	    sizeof(value)) {
		perror("write to eventfd, reason:");
		SPDK_PTL_FATAL("Failed to write eventfd");
	}
}


static struct sockaddr *rdma_cm_find_matching_local_ip(struct sockaddr *address,
		struct sockaddr *result)
{
	struct ifaddrs *ifaddr, *ifa;

	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs, Reason");
		SPDK_PTL_FATAL("Sorry");
		return NULL;
	}

	struct sockaddr_in *dst = (struct sockaddr_in *)address;

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) { continue; }
		if (ifa->ifa_addr->sa_family != AF_INET) { continue; }
		if (ifa->ifa_flags & IFF_LOOPBACK) { continue; }

		struct sockaddr_in *src = (struct sockaddr_in *)ifa->ifa_addr;
		struct sockaddr_in *netmask = (struct sockaddr_in *)ifa->ifa_netmask;

		if ((src->sin_addr.s_addr & netmask->sin_addr.s_addr) ==
		    (dst->sin_addr.s_addr & netmask->sin_addr.s_addr)) {

			memcpy(result, ifa->ifa_addr, sizeof(struct sockaddr));
			goto exit;
		}
	}
exit:
	freeifaddrs(ifaddr);
	return result;
}

/**
 * Used for faking connection setup
 */
static struct spdk_nvmf_rdma_request_private_data private_data = {
	.recfmt = 0,
	.hrqsize = 128,
	.hsqsize = 128,
	.qid = 1
};



typedef enum {
	PTL_CP_SERVER_NOT_RUNNING = 10,
	PTL_CP_SERVER_BOOTING,
	PTL_CP_SERVER_RUNNING
} rdma_ptl_cp_server_status_e;


struct rdma_ptl_send_buffer {
	ptl_handle_md_t md_handle;
	struct ptl_conn_msg conn_msg;
};


static struct rdma_ptl_cp_server {
	pthread_mutex_t init_lock;
	/*Dynamically allocated array of receive buffers for new connection messages (new, close, etc)*/
	struct ptl_conn_msg *recv_buffers;
	const char *role;
	ptl_handle_le_t *le_handle;
	uint32_t num_conn_info;
	ptl_handle_eq_t eq_handle;
	ptl_pt_index_t pt_index;
	pthread_t cp_server_cnxt;
	volatile rdma_ptl_cp_server_status_e status;
	uint64_t protocol_version;
} ptl_control_plane_server = {.init_lock = PTHREAD_MUTEX_INITIALIZER, .status = PTL_CP_SERVER_NOT_RUNNING, .role = "Unknown"};

static struct rdma_ptl_connections {
	pthread_mutex_t conn_map_lock;
	struct ptl_cm_id *ptl_id[RDMA_PTL_MAX_CONNECTIONS];
} conn_map = {.conn_map_lock = PTHREAD_MUTEX_INITIALIZER};

static bool rdma_ptl_conn_map_add(struct ptl_cm_id *ptl_id)
{
	bool ret;
	PTL_CP_SERVER_LOCK(&conn_map.conn_map_lock);
	for (uint32_t i = 0; i < RDMA_PTL_MAX_CONNECTIONS; i++) {
		if (conn_map.ptl_id[i]) {
			continue;
		}
		conn_map.ptl_id[i] = ptl_id;
		SPDK_PTL_DEBUG("[%s] CP server: Added new connection in map with qp_num: %d",
			       ptl_control_plane_server.role, ptl_id->ptl_qp_num);
		ret = true;
		goto exit;
	}
	ret = false;
	SPDK_PTL_FATAL("[%s] CP server: No room for new connections", ptl_control_plane_server.role);
exit:
	PTL_CP_SERVER_UNLOCK(&conn_map.conn_map_lock);
	return ret;/*No more space for new connections*/
}


static bool rdma_ptl_conn_map_remove(struct ptl_cm_id *ptl_id)
{
	bool ret = false;
	PTL_CP_SERVER_LOCK(&conn_map.conn_map_lock);
	for (uint32_t i = 0; i < RDMA_PTL_MAX_CONNECTIONS; i++) {
		if (conn_map.ptl_id[i] == NULL) {
			continue;
		}
		if (conn_map.ptl_id[i]->ptl_qp_num != ptl_id->ptl_qp_num) {
			continue;
		}
		SPDK_PTL_DEBUG("[%s] CP server: removing connection with qp_num = %d",
			       ptl_control_plane_server.role, ptl_id->ptl_qp_num);
		conn_map.ptl_id[i] = NULL;
		ret = true;
		goto exit;
	}
	ret = false;
	SPDK_PTL_FATAL("[%s] CP server: Did not find connection with qp num: %d",
		       ptl_control_plane_server.role, ptl_id->ptl_qp_num);
exit:
	PTL_CP_SERVER_UNLOCK(&conn_map.conn_map_lock);
	return ret;
}

static struct ptl_cm_id *rdma_ptl_conn_map_find_from_qp_num(int qp_num)
{
	struct ptl_cm_id *ptl_id = NULL;
	PTL_CP_SERVER_LOCK(&conn_map.conn_map_lock);
	for (uint32_t i = 0; i < RDMA_PTL_MAX_CONNECTIONS; i++) {
		if (NULL == conn_map.ptl_id[i]) {
			continue;
		}

		if (conn_map.ptl_id[i]->ptl_qp_num == qp_num) {
			ptl_id = conn_map.ptl_id[i];
			goto exit;
		}
	}
	SPDK_PTL_FATAL("[%s] CP server: Connection with qp num: %d not found",
		       ptl_control_plane_server.role, qp_num);
exit:
	PTL_CP_SERVER_UNLOCK(&conn_map.conn_map_lock);
	return ptl_id;
}


static void rdma_ptl_fill_comm_pair_info(struct ptl_conn_comm_pair_info *in,
		struct ptl_conn_comm_pair_info *out)
{
	out->src_nid = in->dst_nid;
	out->src_pid = in->dst_pid;
	out->src_pte = in->dst_pte;
	out->dst_nid = in->src_nid;
	out->dst_pid = in->src_pid;
	out->dst_pte = in->src_pte;
}

static bool rdma_ptl_print_sockaddr(const struct sockaddr *addr)
{
	char ip_str[INET6_ADDRSTRLEN];  // Big enough for both IPv4 and IPv6
	uint16_t port;

	if (!addr) {
		SPDK_PTL_FATAL("NULL address");
	}

	// Handle based on address family
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
		inet_ntop(AF_INET, &(addr_in->sin_addr), ip_str, INET6_ADDRSTRLEN);
		port = ntohs(addr_in->sin_port);
		SPDK_PTL_DEBUG("IPv4 Address: %s, Port: %u", ip_str, port);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
		inet_ntop(AF_INET6, &(addr_in6->sin6_addr), ip_str, INET6_ADDRSTRLEN);
		port = ntohs(addr_in6->sin6_port);
		printf("IPv6 Address: %s, Port: %u, Flow Info: %u, Scope ID: %u\n",
		       ip_str, port, addr_in6->sin6_flowinfo, addr_in6->sin6_scope_id);
		break;
	}
	case AF_UNIX: {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		printf("Unix Domain Socket Path: %s\n", addr_un->sun_path);
		break;
	}
	default:
		SPDK_PTL_FATAL("Unknown address family: %d", addr->sa_family);
	}
	return true;
}


static void rdma_cm_ptl_send_request(struct rdma_ptl_send_buffer *send_buffer,
				     struct ptl_conn_comm_pair_info *dest)
{

	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	ptl_process_t target;
	ptl_md_t md;
	int rc;

	target.phys.nid = dest->dst_nid;
	target.phys.pid = dest->dst_pid;
	SPDK_PTL_DEBUG("[%s] CP server Sending message of type: %d and total "
		       "size in B: %lu to nid: %d pid: %d pte: %d",
		       ptl_control_plane_server.role,
		       send_buffer->conn_msg.msg_header.msg_type,
		       send_buffer->conn_msg.msg_header.total_msg_size,
		       target.phys.nid, target.phys.pid, dest->dst_pte);

	/* Create memory descriptor for the connection info */
	memset(&md, 0, sizeof(ptl_md_t));
	md.start = &send_buffer->conn_msg;
	md.length = send_buffer->conn_msg.msg_header.total_msg_size;
	md.options = 0;
	md.eq_handle = ptl_control_plane_server.eq_handle;
	md.ct_handle = PTL_CT_NONE;

	rc = PtlMDBind(ptl_cnxt_get_ni_handle(ptl_cnxt), &md, &send_buffer->md_handle);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("PtlMDBind failed with code: %d\n", rc);
	}


	// SPDK_PTL_DEBUG("[%s] MD of the request done. Performing the actual PtlPut to target nid:%d pid:%d portals_index: %d",
	// 	       ptl_control_plane_server.role,
	// 	       target.phys.nid, target.phys.pid, PTL_CP_SERVER_PTE);
	/* Send connection info to server using PtlPut */
	rc = PtlPut(send_buffer->md_handle,/* MD handle */
		    0,/* local offset */
		    send_buffer->conn_msg.msg_header.total_msg_size,/* length */
		    PTL_ACK_REQ,/* acknowledgment requested */
		    target, /* target process */
		    dest->dst_pte,  /* portal table index */
		    0, /* match bits */
		    0, /* remote offset */
		    send_buffer, /* user ptr */
		    0);

	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("PtlPut failed with code: %d\n", rc);
	}
}

/**
 * @brief Processes an open connection request
 */
static void rdma_ptl_handle_open_conn(struct ptl_cm_id *listen_id,
				      struct ptl_conn_msg *request_open_conn)
{
	assert(listen_id);
	assert(request_open_conn);
	struct rdma_cm_event *fake_event = {0};
	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	struct ptl_conn_open *conn_open = &request_open_conn->conn_open;
	struct ptl_qp * ptl_qp;
	struct ptl_conn_comm_pair_info comm_pair_info;

	struct ptl_cm_id * ptl_id = ptl_cm_id_create(listen_id->ptl_channel, listen_id->ptl_context);

	ptl_id->uuid = ptl_uuid_set_target_qp_num(ptl_id->uuid, ptl_id->ptl_qp_num);
	ptl_id->uuid = ptl_uuid_set_initiator_qp_num(ptl_id->uuid,
		       request_open_conn->conn_open.initiator_qp_num);
	SPDK_PTL_DEBUG("[%s] CP server: Got an open connection request from "
		       "nid: %d pid: %d pte: %d Creating "
		       "the fake event and send the OPEN_CONNECTION_REPLY pair is: target qp num: %d initiator qp num: %d",
		       ptl_control_plane_server.role,
		       request_open_conn->msg_header.peer_info.src_nid,
		       request_open_conn->msg_header.peer_info.src_pid,
		       request_open_conn->msg_header.peer_info.src_pte,
		       ptl_id->ptl_qp_num, request_open_conn->conn_open.initiator_qp_num);

	comm_pair_info.src_nid = ptl_cnxt_get_nid(ptl_cnxt);
	comm_pair_info.src_pid = ptl_cnxt_get_pid(ptl_cnxt);
	comm_pair_info.src_pte = PTL_PT_INDEX;
	comm_pair_info.dst_nid = request_open_conn->msg_header.peer_info.src_nid;
	comm_pair_info.dst_pid = request_open_conn->msg_header.peer_info.src_pid;
	comm_pair_info.dst_pte = PTL_PT_INDEX;
	ptl_qp = ptl_qp_create(ptl_cnxt->ptl_pd, listen_id->cq, listen_id->cq, &comm_pair_info);
	ptl_id->ptl_qp = ptl_qp;
	ptl_qp->ptl_cm_id = ptl_id;

	ptl_id->fake_cm_id.qp = &ptl_qp->fake_qp;
	memcpy(&ptl_id->fake_cm_id.route.addr.dst_addr, &conn_open->src_addr, sizeof(conn_open->src_addr));

	ptl_id->recv_match_bits = conn_open->recv_match_bits;
	ptl_id->rma_match_bits = conn_open->rma_match_bits;
	ptl_id->remote_cq_id = conn_open->cq_id;
	SPDK_PTL_DEBUG("MATCH_BITS: The remote guy has MEs for recv in match_bits: "
		       "%lu and for RMA: %lu and has subscribed in cq_id: %d",
		       ptl_id->recv_match_bits, ptl_id->rma_match_bits, ptl_id->remote_cq_id);
	ptl_id->uuid = ptl_uuid_set_cq_num(ptl_id->uuid, ptl_id->remote_cq_id);

	rdma_cm_find_matching_local_ip(&ptl_id->fake_cm_id.route.addr.dst_addr,
				       &ptl_id->fake_cm_id.route.addr.src_addr);
	ptl_id->fake_cm_id.qp->qp_num = ptl_id->ptl_qp_num;

	/*extract conn_param*/
	ptl_id->conn_param = conn_open->conn_param;
	ptl_id->conn_param.private_data =
		ptl_id->conn_param.private_data_len
		? calloc(1UL, ptl_id->conn_param.private_data_len)
		: NULL;

	if (ptl_id->conn_param.private_data) {
		SPDK_PTL_DEBUG("CONN_PARAM: Deserializing conn_param private data of len: %u",
			       ptl_id->conn_param.private_data_len);
		memcpy((void *)ptl_id->conn_param.private_data,
		       (char *)conn_open + sizeof(*conn_open),
		       ptl_id->conn_param.private_data_len);
	}

	// SPDK_PTL_DEBUG("RDMA_LISTEN(): Create a fake connection event to establish
	// queue pair");
	/*At the target side, someone wants to connect with us*/
	ptl_id->cm_id_state = PTL_CM_CONNECTING;
	fake_event =
		ptl_cm_id_create_event(ptl_id, listen_id, RDMA_CM_EVENT_CONNECT_REQUEST);

	rdma_ptl_conn_map_add(ptl_id);
	ptl_cm_id_add_event(ptl_id, fake_event);
	rdma_ptl_write_event_to_fd(ptl_id->ptl_channel->fake_channel.fd);
	assert(ptl_id->ptl_channel);

	// SPDK_PTL_DEBUG("CP server: Ok created the fake
	// RDMA_CM_EVENT_CONNECT_REQUEST triggering " 	       "it through channel's async fd.
	// QP num is: %d", 	       fake_event->id->qp->qp_num);
	uint64_t value = 1;
	if (write(listen_id->ptl_channel->fake_channel.fd, &value, sizeof(value)) !=
	    sizeof(value)) {
		perror("write to eventfd, reason:");
		SPDK_PTL_FATAL("Failed to write eventfd");
	}
	/*Caution: TARGET sends the OPEN_CONNECTION_REPLY message to the initiator in rdma_accept()*/
}


static void rdma_ptl_handle_open_conn_reply(struct ptl_cm_id *listen_id,
		struct ptl_conn_msg *conn_msg)
{
	assert(listen_id != NULL);
	struct rdma_cm_event *fake_event;
	struct ptl_conn_open_reply *open_conn_reply = &conn_msg->conn_open_reply;
	struct ptl_cm_id *connection_id;

	if (PTL_OK != open_conn_reply->status) {
		SPDK_PTL_FATAL("[%s], CP server: connection failed with code: %d", ptl_control_plane_server.role,
			       open_conn_reply->status);
	}
	/*XXX TODO XXX*/
	int qp_num =
		is_target
		? ptl_uuid_get_target_qp_num(conn_msg->conn_open_reply.uuid)
		: ptl_uuid_get_initiator_qp_num(conn_msg->conn_open_reply.uuid);
	SPDK_PTL_DEBUG(
		"[%s] CP server: Got open connection reply! connection id is: %lu "
		"initiator qp num: %d target qp num: %d. Creating also the "
		"fake_event. OPEN CONNECTION *DONE*",
		ptl_control_plane_server.role, open_conn_reply->uuid, qp_num,
		ptl_uuid_get_target_qp_num(conn_msg->conn_open_reply.uuid));
	connection_id = rdma_ptl_conn_map_find_from_qp_num(qp_num);
	if (connection_id == NULL) {
		SPDK_PTL_FATAL("[%s] CP server: Could not find connection with qp num: %d",
			       ptl_control_plane_server.role, qp_num);
	}
	connection_id->uuid = open_conn_reply->uuid;
	/*Inform what are the srq bits of the target*/
	connection_id->recv_match_bits = open_conn_reply->srq_match_bits;
	connection_id->rma_match_bits = UINT64_MAX;
	connection_id->remote_cq_id = open_conn_reply->cq_id;
	connection_id->uuid = ptl_uuid_set_cq_num(connection_id->uuid, connection_id->remote_cq_id);
	SPDK_PTL_DEBUG("MATCH_BITS: Target match bits for its srq are: %lu setting "
		       "rma_match_bits to: %lu NO RMA operations from initiator to "
		       "target allowed. Target waits receive events in cq id: %d",
		       connection_id->recv_match_bits, connection_id->rma_match_bits, connection_id->remote_cq_id);

	memcpy(&connection_id->conn_msg, conn_msg, sizeof(*conn_msg));

	/* Create fake RDMA event for compatibility */
	// SPDK_PTL_DEBUG("[%s] CP server Got open connection reply creating the fake
	// event", 	       ptl_control_plane_server.role);
	SPDK_PTL_DEBUG("CONN_PARAM Deserializing in handle_open_conn_reply params of "
		       "the target");
	connection_id->conn_param = open_conn_reply->conn_param;
	if (open_conn_reply->conn_param.private_data_len) {
		SPDK_PTL_DEBUG("CONN_PARAM Deserializing in handle_open_conn_reply private data of the target");
		if (connection_id->conn_param.private_data) {
			free((void *)connection_id->conn_param.private_data);
		}
		connection_id->conn_param.private_data = calloc(1UL, open_conn_reply->conn_param.private_data_len);
		char *private_data = (char *)open_conn_reply + sizeof(*open_conn_reply);
		memcpy((void*)connection_id->conn_param.private_data, private_data,
		       open_conn_reply->conn_param.private_data_len);
	}
	/*We are at the initiator side here, which has just received OPEN_CONNECTION_REPLY*/
	connection_id->cm_id_state = PTL_CM_CONNECTED;
	fake_event = ptl_cm_id_create_event(connection_id, NULL, RDMA_CM_EVENT_ESTABLISHED);
	ptl_cm_id_add_event(connection_id, fake_event);
	rdma_ptl_write_event_to_fd(connection_id->ptl_channel->fake_channel.fd);
	/* the conn_msg will be recycled by the main control plain server loop*/
}



static void rdma_ptl_handle_close_conn(struct ptl_conn_msg *request)
{
	struct ptl_conn_close *conn_close = &request->conn_close;
	struct rdma_cm_event *fake_event;
	struct ptl_cm_id * connection_id;
	struct rdma_ptl_send_buffer *reply_buf;

	if (ptl_control_plane_server.protocol_version != request->msg_header.version) {
		SPDK_PTL_FATAL("[%s], PROTOCOL versions mismatch client uses: %lu %s: %lu",
			       ptl_control_plane_server.role, request->msg_header.version, ptl_control_plane_server.role,
			       ptl_control_plane_server.protocol_version);
	}

	int initiator_qp_num = ptl_uuid_get_initiator_qp_num(conn_close->uuid);
	if (initiator_qp_num == 0) {
		SPDK_PTL_FATAL("initiator qp num == 0: Nida does not assign 0 qp numbers!");
	}

	int target_qp_num = ptl_uuid_get_target_qp_num(conn_close->uuid);
	if (target_qp_num == 0) {
		SPDK_PTL_FATAL("target qp num == 0: Nida does not assign 0 qp numbers!");
	}


	connection_id = rdma_ptl_conn_map_find_from_qp_num(is_target ? target_qp_num : initiator_qp_num);
	if (NULL == connection_id) {
		SPDK_PTL_FATAL("Could not find in connection map queue pair with number: %d",
			       is_target ? target_qp_num : initiator_qp_num);
	}

	SPDK_PTL_DEBUG(
		"[%s] Got close connection request for connection id: %lu for "
		"initiator_qp_num: %d target_qp_num: %d. Found connection, "
		"creating the fake RDMA_CM_EVENT_DISCONNECTED and send the "
		"CLOSE_CONNECTION_REPLY",
		ptl_control_plane_server.role, conn_close->uuid, initiator_qp_num,
		target_qp_num);
	// SPDK_PTL_DEBUG("[%s] CP server: Found connection id with target qp num: %d to close",
	// 	       ptl_control_plane_server.role, target_qp_num);
	connection_id->cm_id_state = PTL_CM_DISCONNECTING;
	fake_event = ptl_cm_id_create_event(connection_id, NULL, RDMA_CM_EVENT_DISCONNECTED);

	ptl_cm_id_add_event(connection_id, fake_event);
	rdma_ptl_write_event_to_fd(connection_id->ptl_channel->fake_channel.fd);
	// SPDK_PTL_DEBUG("[%s] CP server: Created the fake RDMA_CM_EVENT_DISCONNECTED to trigger close process",
	// 	       ptl_control_plane_server.role);

	/*Allocate the buffer for the open connection request*/
	if (posix_memalign((void **)&reply_buf, 4096, sizeof(*reply_buf))) {
		SPDK_PTL_FATAL("Failed to allocate ptl_conn_request");
	}
	reply_buf->conn_msg.msg_header.version = PTL_SPDK_PROTOCOL_VERSION;
	reply_buf->conn_msg.msg_header.msg_type = PTL_CLOSE_CONNECTION_REPLY;
	reply_buf->conn_msg.msg_header.total_msg_size = sizeof(reply_buf->conn_msg);
	reply_buf->conn_msg.conn_close_reply.status = PTL_OK;
	reply_buf->conn_msg.conn_close_reply.uuid = connection_id->uuid;
	rdma_ptl_fill_comm_pair_info(&request->msg_header.peer_info,
				     &reply_buf->conn_msg.msg_header.peer_info);
	rdma_cm_ptl_send_request(reply_buf, &reply_buf->conn_msg.msg_header.peer_info);
}


static void rdma_ptl_handle_close_conn_reply(struct ptl_conn_msg *conn_msg)
{
	struct ptl_conn_close_reply *close_reply = &conn_msg->conn_close_reply;
	struct rdma_cm_event *fake_event;
	int initiator_qp_num = ptl_uuid_get_initiator_qp_num(close_reply->uuid);
	int target_qp_num = ptl_uuid_get_target_qp_num(close_reply->uuid);


	if (initiator_qp_num == 0) {
		SPDK_PTL_FATAL("Corrupted initiator qp num, Nida does not assign 0 qp numbers!");
	}

	if (target_qp_num == 0) {
		SPDK_PTL_FATAL("Corrupted target qp num, Nida does not assign 0 qp numbers!");
	}

	struct ptl_cm_id * connection_id = rdma_ptl_conn_map_find_from_qp_num(
			is_target ? target_qp_num : initiator_qp_num);
	if (connection_id == NULL) {
		SPDK_PTL_FATAL("Cannot find connection id with qp num: %d",
			       is_target ? target_qp_num : initiator_qp_num);
	}

	if (PTL_OK != close_reply->status) {
		SPDK_PTL_FATAL("[%s], connection close failed with code: %d", ptl_control_plane_server.role,
			       close_reply->status);
	}

	SPDK_PTL_DEBUG("[%s] Got a close connection reply! connection id is: %lu aldready done staff nothing to do",
		       ptl_control_plane_server.role, close_reply->uuid);
	/*At the initiator bye bye connection*/
	connection_id->cm_id_state = PTL_CM_DISCONNECTED;
	fake_event = ptl_cm_id_create_event(connection_id, NULL, RDMA_CM_EVENT_DISCONNECTED);
	ptl_cm_id_add_event(connection_id, fake_event);
	rdma_ptl_write_event_to_fd(connection_id->ptl_channel->fake_channel.fd);
}

/**
 * @brief Control plane server function that handles incoming Portals events
 *
 * This function implements the main event loop for the RDMA/Portals control
 * plane server. Its purpose it listen for new connection requests in order to
 * create a map from queue pair number to a (pid, nid) pair. This is needed to
 * be comIt continuously polls the event queue for incoming messages, processes
 * PUT operations, and manages the recycling of receive buffers through list
 * entries.
 *
 * The function performs the following operations:
 * - Waits for events using PtlEQWait()
 * - Validates that received events are PUT operations
 * - Logs information about the initiator (NID and PID)
 * - Re-registers receive buffers by creating new list entries
 *
 * @param args Pointer to function arguments (unused in current implementation)
 * @return void* Returns NULL (thread function requirement)
 *
 * @note This function runs in an infinite loop and should be launched as a
 * separate thread
 * @warning Function will terminate with SPDK_PTL_FATAL if non-PUT events are
 * received or if any Portals operations fail
 */
static void *rdma_run_ptl_cp_server(void *args)
{
	assert(args);
	struct ptl_cm_id *listen_id = args;
	struct ptl_conn_msg * conn_msg;
	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	struct rdma_ptl_send_buffer *send_buffer;
	ptl_event_t event;
	int rc;


	SPDK_PTL_DEBUG("[%s] CP server started, waiting for new connections",
		       ptl_control_plane_server.role);
	ptl_control_plane_server.status = PTL_CP_SERVER_RUNNING;
	while (1) {
		/* Wait for events on the control plane event queue */
		rc = PtlEQWait(ptl_control_plane_server.eq_handle, &event);

		if (rc != PTL_OK) {
			SPDK_PTL_FATAL(
				"PtlEQWait failed in control plane server with code: %d\n", rc);
		}

		send_buffer = event.user_ptr;

		if (event.type == PTL_EVENT_AUTO_UNLINK) {
			SPDK_PTL_DEBUG("[%s] CP server: Got an autounlink event continue...",
				       ptl_control_plane_server.role);
			continue;
		}
		if (event.type == PTL_EVENT_SEND) {
			continue;
		}

		if (event.type == PTL_EVENT_ACK &&
		    (send_buffer->conn_msg.msg_header.msg_type ==  PTL_OPEN_CONNECTION_REPLY  ||
		     send_buffer->conn_msg.msg_header.msg_type == PTL_CLOSE_CONNECTION_REPLY  ||
		     send_buffer->conn_msg.msg_header.msg_type == PTL_OPEN_CONNECTION  ||
		     send_buffer->conn_msg.msg_header.msg_type == PTL_CLOSE_CONNECTION)) {
			SPDK_PTL_DEBUG("[%s] CP server: Send operation of msg with type: %d arrived, do the cleanup...",
				       ptl_control_plane_server.role, send_buffer->conn_msg.msg_header.msg_type);
			PtlMDRelease(send_buffer->md_handle);
			free(send_buffer);
			continue;
		}

		if (event.type == PTL_EVENT_ACK) {
			SPDK_PTL_FATAL("[%s] CP server: What is this?: %d",
				       ptl_control_plane_server.role, send_buffer->conn_msg.msg_header.msg_type);
		}

		/* Verify that the event is a PUT operation */
		if (event.type != PTL_EVENT_PUT) {
			SPDK_PTL_FATAL(
				"[%s] Unexpected event type received in control plane server: %d", ptl_control_plane_server.role,
				event.type);
		}

		struct ptl_conn_msg *msg = event.start;
		/*Who is it?*/
		if (event.rlength != msg->msg_header.total_msg_size) {
			SPDK_PTL_FATAL("[%s] CP server: Wrong size received "
				       "got: %lu should have been: %lu",
				       ptl_control_plane_server.role,
				       event.rlength,
				       msg->msg_header.total_msg_size);
		}


		assert(event.start);
		conn_msg = event.start;

		// SPDK_PTL_DEBUG("[%s] CP server: Received connection related message size is: %lu conn_msg size: %lu",
		// 	       ptl_control_plane_server.role, event.rlength, sizeof(*conn_msg));

		if (PTL_OPEN_CONNECTION == conn_msg->msg_header.msg_type) {
			rdma_ptl_handle_open_conn(listen_id, conn_msg);
		} else if (PTL_OPEN_CONNECTION_REPLY == conn_msg->msg_header.msg_type) {
			rdma_ptl_handle_open_conn_reply(listen_id, conn_msg);
		} else if (PTL_CLOSE_CONNECTION == conn_msg->msg_header.msg_type) {
			rdma_ptl_handle_close_conn(conn_msg);
		} else if (PTL_CLOSE_CONNECTION_REPLY == conn_msg->msg_header.msg_type) {
			rdma_ptl_handle_close_conn_reply(conn_msg);
		} else {
			SPDK_PTL_FATAL("[%s] Unknown message type: %d", ptl_control_plane_server.role,
				       conn_msg->msg_header.msg_type);
		}

		/* Re-register the receive buffer by appending a new list entry */
		ptl_le_t le;
		memset(&le, 0, sizeof(ptl_le_t));
		le.ignore_bits = RDMA_PTL_IGNORE;
		le.match_bits = RDMA_PTL_MATCH;
		le.match_id.phys.nid = PTL_NID_ANY;
		le.match_id.phys.pid = PTL_PID_ANY;
		le.min_free = 0;
		le.start = event.start;
		le.length = RDMA_PTL_MSG_BUFFER_SIZE;
		le.ct_handle = PTL_CT_NONE;
		le.uid = PTL_UID_ANY;
		le.options = PTL_SRV_ME_OPTS;

		rc = PtlLEAppend(ptl_cnxt_get_ni_handle(ptl_cnxt),
				 PTL_CP_SERVER_PTE, &le, PTL_PRIORITY_LIST,
				 event.user_ptr, event.user_ptr);
		if (rc != PTL_OK) {
			SPDK_PTL_FATAL(
				"PtlLEAppend failed in control plane server with code: %d\n",
				rc);
		}
		SPDK_PTL_DEBUG("Re-registered control plane buffer");
	}
	return NULL;
}

/**
 * @brief Called from rdma_listen to setup a new Event Queue for Portal Index
 * PTL_CONTROL_PLANE_PT_INDEX. When client calls rdma_connect they send a
 * message to this EQ to introduce themselves and enable server to build a map
 * containing the queue pair num to nid,pid as required by the Portals API.
 * static void rdma_ptl_control_eq(void) {}
 **/
static void rdma_ptl_boot_cp_server(struct  ptl_cm_id *cm_id, const char *role)
{

	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	ptl_le_t le;
	int rc;

	PTL_CP_SERVER_LOCK(&ptl_control_plane_server.init_lock);
	if (ptl_control_plane_server.status == PTL_CP_SERVER_RUNNING) {
		// SPDK_PTL_WARN("CP server for connections already running, nothing to boot");
		goto exit;
	}
	ptl_control_plane_server.role = role;
	is_target = strcmp("TARGET", role) == 0 ? 1 : 0;
	ptl_control_plane_server.protocol_version = PTL_SPDK_PROTOCOL_VERSION;
	ptl_control_plane_server.num_conn_info = PTL_CONTROL_PLANE_NUM_RECV_BUFFERS;
	rc = posix_memalign((void **)&ptl_control_plane_server.recv_buffers, 4096,
			    PTL_CONTROL_PLANE_NUM_RECV_BUFFERS * RDMA_PTL_MSG_BUFFER_SIZE);
	if (rc != 0) {
		perror("Reason of posix_memalign failure:");
		SPDK_PTL_FATAL("posix_memalign failed: %d", rc);
	}
	memset(ptl_control_plane_server.recv_buffers, 0x00,
	       ptl_control_plane_server.num_conn_info * RDMA_PTL_MSG_BUFFER_SIZE);

	ptl_control_plane_server.le_handle = calloc(ptl_control_plane_server.num_conn_info,
					     sizeof(ptl_handle_le_t));
	/*Create event queue for control plane messages*/
	rc = PtlEQAlloc(ptl_cnxt_get_ni_handle(ptl_cnxt), PTL_CONTROL_PLANE_NUM_RECV_BUFFERS,
			&ptl_control_plane_server.eq_handle);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("PtlEQAlloc for the control plane failed with code: %d\n", rc);
	}
	/*Bind it to the portal index*/
	rc = PtlPTAlloc(ptl_cnxt_get_ni_handle(ptl_cnxt), 0, ptl_control_plane_server.eq_handle,
			PTL_CP_SERVER_PTE, &ptl_control_plane_server.pt_index);
	if (rc != PTL_OK) {
		SPDK_PTL_FATAL("Error allocating portal for connection server %d reason: %d",
			       PTL_CP_SERVER_PTE, rc);
	}

	for (uint32_t i = 0; i < ptl_control_plane_server.num_conn_info; i++) {

		memset(&le, 0, sizeof(ptl_le_t));
		le.ignore_bits = RDMA_PTL_IGNORE;
		le.match_bits = RDMA_PTL_MATCH;
		le.match_id.phys.nid = PTL_NID_ANY;
		le.match_id.phys.pid = PTL_PID_ANY;
		le.min_free = 0;
		le.start = &ptl_control_plane_server.recv_buffers[i];
		le.length = RDMA_PTL_MSG_BUFFER_SIZE;
		le.ct_handle = PTL_CT_NONE;
		le.uid = PTL_UID_ANY;
		le.options = PTL_SRV_ME_OPTS;

		// Append LE for receiving control messages
		rc = PtlLEAppend(ptl_cnxt_get_ni_handle(ptl_cnxt), PTL_CP_SERVER_PTE, &le,
				 PTL_PRIORITY_LIST, &ptl_control_plane_server.le_handle[i], &ptl_control_plane_server.le_handle[i]);
		if (rc != PTL_OK) {
			SPDK_PTL_FATAL("PtlLEAppend failed in control plane server with code: %d\n", rc);
		}
	}

	SPDK_PTL_DEBUG("CP server: BOOTING control plane server...");
	if (pthread_create(&ptl_control_plane_server.cp_server_cnxt, NULL,
			   rdma_run_ptl_cp_server, cm_id)) {
		perror("Reason of failure of booting control plane server:");
		SPDK_PTL_FATAL("Failed to boot control plane server");
	}
	while (ptl_control_plane_server.status != PTL_CP_SERVER_RUNNING);


	SPDK_PTL_DEBUG("[%s] CP server: BOOTED control plane server",
		       ptl_control_plane_server.role);
exit:
	PTL_CP_SERVER_UNLOCK(&ptl_control_plane_server.init_lock);
}

struct rdma_event_channel *
rdma_create_event_channel(void)
{
	SPDK_PTL_DEBUG("RDMACM: Intercepted rdma_create_event_channel()");
	struct rdma_cm_ptl_event_channel *ptl_channel = calloc(1UL,
		sizeof(*ptl_channel));
	if (!ptl_channel) {
		SPDK_PTL_DEBUG("RDMACM: Allocation of memory failed");
		return NULL;
	}
	ptl_channel->magic_number = RDMA_CM_PTL_EVENT_CHANNEL_MAGIC_NUMBER;
	RDMA_CM_LOCK_INIT(&ptl_channel->events_deque_lock);
	ptl_channel->events_deque = deque_create(NULL);

	struct rdma_event_channel *channel = &ptl_channel->fake_channel;

	channel->fd = eventfd(0, EFD_NONBLOCK);
	if (channel->fd < 0) {
		free(ptl_channel);
		SPDK_PTL_DEBUG("RDMACM: eventfd failed. Reason follows:");
		perror("Reason:");
		return NULL;
	}
	return channel;
}

struct ibv_context **rdma_get_devices(int *num_devices)
{
	struct ibv_context **devices;
	struct ptl_context *cnxt;

	SPDK_PTL_DEBUG("CAUTION: Intercepted rdma_get_devices");
	cnxt = ptl_cnxt_get();

	devices = calloc(1UL, 2 * sizeof(struct ibv_context *));
	if (NULL == devices) {
		SPDK_PTL_FATAL("RDMACM: Failed to allocate memory for device list");
	}

	devices[0] = ptl_cnxt_get_ibv_context(cnxt);
	devices[0]->async_fd = eventfd(0, EFD_NONBLOCK);
	if (devices[0]->async_fd < 0) {
		SPDK_PTL_FATAL("RDMACM: Failed to create async fd");
	}
	SPDK_PTL_DEBUG("RDMACM: Created this async_fd thing");
	devices[0]->device = calloc(1UL, sizeof(struct ibv_device));
	if (NULL == devices[0]->device) {
		SPDK_PTL_FATAL("RDMACM: No memory");
	}
	// Set device name and other attributes
	strcpy(devices[0]->device->name, "portals_device");
	strcpy(devices[0]->device->dev_name, "bxi0");
	strcpy(devices[0]->device->dev_path, "/dev/portals0");
	if (num_devices) {
		*num_devices = 1;
	}
	devices[1] = NULL;
	SPDK_PTL_DEBUG("RDMACM: Initialization DONE with portals Initialization, encapsulated portals_context inside ibv_context");
	return devices;
}

void rdma_free_devices(struct ibv_context **list)
{
	SPDK_PTL_DEBUG("CAUTION: Trapped rdma_free_devices ignore XXX TODO XXX");
	return;
	struct ibv_context **devices = list;

	if (close(devices[0]->async_fd)) {
		SPDK_PTL_FATAL("failed to closed async_fd");
	}

	free(devices[0]->device);
	free(devices);
}

/**
 * ********************************************************************
 * <Subset> of libverbs that Nida implements so nvmf target can operate
 * ********************************************************************
 **/
int ibv_query_device(struct ibv_context *context,
		     struct ibv_device_attr *device_attr)
{

	// Zero out the structure first
	memset(device_attr, 0, sizeof(struct ibv_device_attr));
	strcpy(device_attr->fw_ver, "2.42.5000");
	device_attr->node_guid = 0x0002c90300fed670;
	device_attr->sys_image_guid = 0x0002c90300fed673;
	device_attr->max_mr_size =  18446744073709551615UL;
	device_attr->page_size_cap = 0xfffffe00;
	device_attr->vendor_id = 0x000002c9;
	device_attr->vendor_part_id = 0x00001003;
	device_attr->hw_ver = 0x00000001;
	device_attr->max_qp = 131000;
	device_attr->max_qp_wr = 16351;
	device_attr->device_cap_flags = 0x05361c76;
	device_attr->max_sge = 32;
	device_attr->max_sge_rd = 30;
	device_attr->max_cq = 65408;
	device_attr->max_cqe = 4194303;
	device_attr->max_mr = 524032;
	device_attr->max_pd = 32764;
	device_attr->max_qp_rd_atom = 16;
	device_attr->max_ee_rd_atom = 0;
	device_attr->max_res_rd_atom = 2096000;
	device_attr->max_qp_init_rd_atom = 128;
	device_attr->max_ee_init_rd_atom = 0;
	device_attr->atomic_cap = 1; // Assuming 1 corresponds to the enum value
	device_attr->max_ee = 0;
	device_attr->max_rdd = 0;
	device_attr->max_mw = 0;
	device_attr->max_raw_ipv6_qp = 0;
	device_attr->max_raw_ethy_qp = 0;
	device_attr->max_mcast_grp = 8192;
	device_attr->max_mcast_qp_attach = 248;
	device_attr->max_total_mcast_qp_attach = 2031616;
	device_attr->max_ah = 2147483647;

	device_attr->max_srq = 256;            // Max Shared Receive Queues
	device_attr->max_srq_wr = 4096;        // Max SRQ work requests
	device_attr->max_srq_sge = 32;         // Max SGE for SRQ

	// device_attr->vendor_id = 0x02c9;       //Fake Mellanox id
	// device_attr->vendor_part_id = 0x1017;  // Fake Mellanox part id
	// device_attr->max_qp = 256;             // Number of QPs you'll support
	// device_attr->max_cq = 256;             // Number of CQs
	// device_attr->max_mr = 256;             // Number of Memory Regions
	// device_attr->max_pd = 256;             // Number of Protection Domains
	// device_attr->max_qp_wr = 4096;         // Max Work Requests per QP
	// device_attr->max_cqe = 4096;           // Max CQ entries
	// device_attr->max_mr_size = UINT64_MAX; // Max size of Memory Region
	// device_attr->max_sge = 32;             // Max Scatter/Gather Elements
	// device_attr->max_sge_rd = 32;          // Max SGE for RDMA read
	// device_attr->max_qp_rd_atom = 0;      // Max outstanding RDMA reads
	// device_attr->max_qp_init_rd_atom = 16; // Initial RDMA read resources
	// device_attr->max_srq = 256;            // Max Shared Receive Queues
	// device_attr->max_srq_wr = 4096;        // Max SRQ work requests
	// device_attr->max_srq_sge = 32;         // Max SGE for SRQ

	// // Set capabilities flags
	// device_attr->device_cap_flags =
	// 	IBV_DEVICE_RESIZE_MAX_WR |  // Support QP/CQ resize
	// 	IBV_DEVICE_BAD_PKEY_CNTR |  // Support bad pkey counter
	// 	IBV_DEVICE_BAD_QKEY_CNTR |  // Support bad qkey counter
	// 	IBV_DEVICE_RAW_MULTI |      // Support raw packet QP
	// 	IBV_DEVICE_AUTO_PATH_MIG |  // Support auto path migration
	// 	IBV_DEVICE_CHANGE_PHY_PORT; // Support changing physical port
	SPDK_PTL_DEBUG("IBVPTL: Trapped ibv_query_device *FILLED* it with reasonable values...DONE");
	return 0;
}

struct ibv_pd *ibv_alloc_pd(struct ibv_context *context)
{
	struct ptl_pd *ptl_pd;
	struct ptl_context *ptl_context = ptl_cnxt_get_from_ibcnxt(context);
	SPDK_PTL_DEBUG("IBVPTL: OK trapped ibv_alloc_pd allocating ptl_pd");
	if (ptl_context->ptl_pd) {
		SPDK_PTL_DEBUG("PTL_PD Already set, go on");
		return ptl_pd_get_ibv_pd(ptl_context->ptl_pd);
	}
	ptl_pd = ptl_pd_create(ptl_context);
	ptl_context->ptl_pd = ptl_pd;
	return ptl_pd_get_ibv_pd(ptl_pd);
}

struct ibv_context *ibv_open_device(struct ibv_device *device)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}

struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,
			     void *cq_context, struct ibv_comp_channel *channel,
			     int comp_vector)
{

	SPDK_PTL_DEBUG("IBVPTL: Ok trapped ibv_create_cq time to create the event queue in portals");
	struct ptl_cq *ptl_cq = ptl_cq_create(cq_context);
	SPDK_PTL_DEBUG("PtlCQ: Ok set up event queue for PORTALS :-) CQ id = %d", ptl_cq->cq_id);
	return ptl_cq_get_ibv_cq(ptl_cq);
}

// Caution! Due to inlining of ibv_poll_cq SPDK_PTL overrides it also in
// ptl_context
// int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc) {
//     SPDK_PTL_DEBUG("Intercepted ibv_poll_cq");
//     SPDK_PTL_FATAL("UNIMPLEMENTED");
//     return -1;
// }

struct ibv_srq *ibv_create_srq(struct ibv_pd *pd,
			       struct ibv_srq_init_attr *srq_init_attr)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return NULL;
}

int ibv_modify_srq(struct ibv_srq *srq,
		   struct ibv_srq_attr *srq_attr,
		   int srq_attr_mask)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return 0;
}


int ibv_destroy_srq(struct ibv_srq *srq)
{
	SPDK_PTL_FATAL("UNIMPLEMENTED");
	return 0;
}
/**
 * ********************************************************************
 * </Subset> of libverbs that Nida implements so nvmf target can operate
 * ********************************************************************
 **/

int rdma_create_id(struct rdma_event_channel *channel, struct rdma_cm_id **id,
		   void *context, enum rdma_port_space ps)
{
	struct ptl_cm_id *ptl_id;
	struct rdma_cm_ptl_event_channel *ptl_channel;
	ptl_channel = rdma_cm_ptl_event_channel_get(channel);
	ptl_id = ptl_cm_id_create(ptl_channel, context);
	rdma_ptl_conn_map_add(ptl_id);
	/*Caution wiring need it, it is accessed later*/
	// ptl_id->fake_cm_id.qp = &ptl_id->fake_qp;
	/*
	 * set PD, CQ, and QP to NULL. These fields are updated through rdma_create_qp!
	 * Don't worry already done in the constructor of the ptl_cm_id object
	 * */

	*id = &ptl_id->fake_cm_id;
	SPDK_PTL_DEBUG("Trapped create cm id FAKED it, waking up possible guys for the event");
	return 0;
}

int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr)
{
	SPDK_PTL_DEBUG("Trapped rdma_bind_addr setting src addresss");
	struct ptl_cm_id * ptl_id = ptl_cm_id_get(id);
	if (ptl_id->object_type != PTL_CM_ID) {
		SPDK_PTL_FATAL("Corrupted ptl_cm_id");
	}
	memcpy(&id->route.addr.src_addr, addr, sizeof(*addr));

	return 0;
}

int rdma_listen(struct rdma_cm_id *id, int backlog)
{
	struct ptl_cm_id * ptl_id = ptl_cm_id_get(id);
	ptl_id->is_listen_id = true;
	/**
	* Issue: The upper layer of the NVMe-oF target creates a single ibv_cq, but the
	* listen_id doesn't contain the necessary information to determine to which ptl_cq
	* each generated queue pair should report to.
	*
	* Current workaround: Search the ptl context for the reference to cq_id 0,
	* which should have its in-use flag already set to true.
	*/

	if (ptl_id->cq == NULL) {
		ptl_id->cq = ptl_cq_get(PTL_UUID_TARGET_COMPLETION_QUEUE_ID);
	}

	// struct rdma_cm_event *fake_event;
	rdma_ptl_boot_cp_server(ptl_id, "TARGET");

	// SPDK_PTL_DEBUG("RDMA_LISTEN(): Create a fake connection event to establish queue pair no 1");
	// fake_event = ptl_cm_id_create_event(ptl_id, id, RDMA_CM_EVENT_CONNECT_REQUEST, &private_data,
	// 				    sizeof(private_data));

	// /*Fill up fake data about the origin of the guy that wants a new connection*/
	// fake_event->listen_id = id;

	// ptl_cm_id_add_event(ptl_id, fake_event);
	// SPDK_PTL_DEBUG("RDMA_LISTEN(): Ok created the fake RDMA_CM_EVENT_CONNECT_REQUEST triggering it through channel's async fd");
	// uint64_t value = 1;
	// if (write(ptl_id->ptl_channel->fake_channel.fd, &value, sizeof(value)) != sizeof(value)) {
	// 	perror("write to eventfd, reason:");
	// 	SPDK_PTL_FATAL("Failed to write eventfd");
	// }
	return 0;
}

int rdma_get_cm_event(struct rdma_event_channel *channel,
		      struct rdma_cm_event **event)
{

	struct rdma_cm_ptl_event_channel *ptl_channel;
	struct rdma_cm_event *fake_event;
	struct ptl_cm_id *ptl_id;
	ptl_channel = rdma_cm_ptl_event_channel_get(channel);
	fake_event = NULL;
	RDMA_CM_LOCK(&ptl_channel->events_deque_lock);
	fake_event = deque_pop_front(ptl_channel->events_deque);
	RDMA_CM_UNLOCK(&ptl_channel->events_deque_lock);

	// if(fake_event)
	//   break;
	// SPDK_PTL_DEBUG("No event going to sleep...");
	// sem_wait(&ptl_channel->sem);
	// SPDK_PTL_DEBUG("Got something woke up");
	*event = fake_event;

	if (fake_event) {
		ptl_id = ptl_cm_id_get(fake_event->id);
		SPDK_PTL_DEBUG("(nikos) OK got event of type: %d for qp num: %d from channel: %p",
			       fake_event->event, ptl_id->ptl_qp_num, ptl_channel);
		/* Clean the event*/
		// uint64_t result;
		// if (read(channel->fd, &result, sizeof(result)) != sizeof(result)) {
		// 	perror("read");
		// 	SPDK_PTL_FATAL("Failed to clean the event, go on");
		// }
		return 0;
	}
	// SPDK_PTL_DEBUG("Got nothing shit EAGAIN!");
	errno = EAGAIN;
	return EAGAIN;
}

int rdma_ack_cm_event(struct rdma_cm_event *event)
{
	SPDK_PTL_DEBUG("ACK (Destroy basically) my fake CM event");
	if (event->param.conn.private_data) {
		free((void *)event->param.conn.private_data);
	}
	free(event);
	return 0;
}

static void rdma_cm_print_addr_info(const char *prefix, struct sockaddr *addr)
{
	char ip_str[INET6_ADDRSTRLEN] = {0};
	uint16_t port = 0;

	if (!addr) {
		SPDK_PTL_INFO("%s: NULL address", prefix);
		return;
	}

	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *)addr;
		inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET6_ADDRSTRLEN);
		port = ntohs(ipv4->sin_port);
		SPDK_PTL_DEBUG("%s: IPv4 %s:%u", prefix, ip_str, port);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)addr;
		inet_ntop(AF_INET6, &(ipv6->sin6_addr), ip_str, INET6_ADDRSTRLEN);
		port = ntohs(ipv6->sin6_port);
		SPDK_PTL_DEBUG("%s: IPv6 %s:%u", prefix, ip_str, port);
		break;
	}
	default:
		SPDK_PTL_DEBUG("%s: Unknown address family %d", prefix, addr->sa_family);
	}
}


int rdma_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
		      struct sockaddr *dst_addr, int timeout_ms)
{
	struct sockaddr resolve_src_addr;
	struct sockaddr *resolve_src_addr_p;
	struct rdma_cm_event *fake_event;
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(id);

	SPDK_PTL_DEBUG("Keep accounting of the src and dst addr in rdma_cm_id structure");
	if (src_addr) {
		memcpy(&id->route.addr.src_addr, src_addr,
		       (src_addr->sa_family == AF_INET6) ?
		       sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in));
	}

	if (!dst_addr) {
		SPDK_PTL_FATAL("No dst addr");
	}
	memcpy(&id->route.addr.dst_addr, dst_addr,
	       (dst_addr->sa_family == AF_INET6) ? sizeof(struct sockaddr_in6)
	       : sizeof(struct sockaddr_in));

	// Set the address family in the route
	id->route.addr.src_addr.sa_family = src_addr ?
					    src_addr->sa_family : dst_addr->sa_family;
	id->route.addr.dst_addr.sa_family = dst_addr->sa_family;

	resolve_src_addr_p = src_addr;
	if (NULL == src_addr) {
		rdma_cm_find_matching_local_ip(dst_addr, &resolve_src_addr);
		resolve_src_addr_p = &resolve_src_addr;
	}

	if (NULL == resolve_src_addr_p) {
		rdma_cm_print_addr_info("DESTINATION IS:", dst_addr);
		SPDK_PTL_FATAL("Could not find a local ip to match destination address");
	}

	SPDK_PTL_DEBUG("----------->   Resolved src addr   <--------------");
	rdma_cm_print_addr_info("SOURCE_ADDR = ", &id->route.addr.src_addr);
	fake_event = ptl_cm_id_create_event(ptl_id, NULL, RDMA_CM_EVENT_ADDR_RESOLVED);
	ptl_cm_id_add_event(ptl_id, fake_event);
	rdma_ptl_write_event_to_fd(ptl_id->ptl_channel->fake_channel.fd);
	SPDK_PTL_DEBUG("Ok stored dst addr and generated fake "
		       "RDMA_CM_EVENT_ADDR_RESOLVED event");
	return 0;
}


int rdma_resolve_route(struct rdma_cm_id *id, int timeout_ms)
{
	struct rdma_cm_event *fake_event;
	struct ifaddrs *ifaddr, *ifa;
	struct sockaddr *dst_addr = &id->route.addr.dst_addr;
	struct sockaddr *src_addr = &id->route.addr.src_addr;
	int family = dst_addr->sa_family;
	int found = 0;
	/*First given the dest address fill the src address*/


	if (getifaddrs(&ifaddr) == -1) {
		perror("getifaddrs failed");
		SPDK_PTL_FATAL("Failed to get IP addresses");
	}

	SPDK_PTL_DEBUG("Looking for interfaces with family %d (AF_INET=%d, AF_INET6=%d)",
		       family, AF_INET, AF_INET6);

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) {
			continue;
		}

		// Skip if not the address family we're looking for
		if (ifa->ifa_addr->sa_family != family) {
			continue;
		}

		SPDK_PTL_DEBUG("Checking interface: %s", ifa->ifa_name);
		SPDK_PTL_DEBUG("  Flags: 0x%x ", ifa->ifa_flags);


		// Skip loopback
		if (ifa->ifa_flags & IFF_LOOPBACK) {
			printf("  Skipping loopback interface\n");
			continue;
		}

		// Skip if interface is not up and running
		if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING)) {
			SPDK_PTL_DEBUG("  Skipping interface - not up/running");
			continue;
		}

		// Copy the address
		memcpy(src_addr, ifa->ifa_addr,
		       (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
		found = 1;
		break;
	}

	freeifaddrs(ifaddr);
	if (false == found) {
		SPDK_PTL_FATAL("Could not find a corresponding SRC IP address");
	}
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(id);
	fake_event = ptl_cm_id_create_event(ptl_id, NULL, RDMA_CM_EVENT_ROUTE_RESOLVED);
	ptl_cm_id_add_event(ptl_id, fake_event);
	rdma_ptl_write_event_to_fd(ptl_id->ptl_channel->fake_channel.fd);
	return 0;
}


/**
 * @brief Extracts the target process ID from the last digit of an RDMA connection's IP address
 *
 * This function examines the destination IP address of an RDMA connection and returns
 * its last digit, which is used as the target process ID. The function supports both
 * IPv4 and IPv6 addresses.
 *
 * @param id Pointer to an RDMA connection identifier structure (rdma_cm_id).
 *           Must not be NULL and must have a valid destination address set.
 *
 * @return The last digit found in the IP address, to be used as the target process ID
 *
 * @throws SPDK_PTL_FATAL in the following cases:
 *         - If the input rdma_cm_id pointer is NULL
 *         - If IPv4/IPv6 address conversion fails
 *         - If the address family is unsupported (neither IPv4 nor IPv6)
 *         - If no digits are found in the IP address
 *
 * @note The function assumes that the last digit of the IP address corresponds
 *       to a valid process ID in the target system.
 *
 * Example usage:
 * @code
 *     struct rdma_cm_id *id = get_grdma_connection();
 *     int target_pid = rdmdstfind_target_pid(id);
 *     // target_pid now contains the last digit of the IP address
 * @endcode
 */
static int rdma_ptl_find_nid(struct sockaddr *addr)
{
	struct sockaddr_in *sin = NULL;
	struct sockaddr_in6 *sin6 = NULL;
	uint8_t last_byte;

	if (NULL == addr) {
		SPDK_PTL_FATAL("Invalid sockaddr");
	}

	switch (addr->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)addr;
		// For IPv4, get the last byte directly from the address
		last_byte = ((uint8_t *)&sin->sin_addr.s_addr)[3];
		SPDK_PTL_DEBUG("IPv4 last byte: %d", last_byte);
		return last_byte;

	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)addr;
		// For IPv6, get the last byte from the 16-byte address
		last_byte = sin6->sin6_addr.s6_addr[15];
		SPDK_PTL_DEBUG("IPv6 last byte: %d", last_byte);
		return last_byte;

	default:
		SPDK_PTL_FATAL("Unsupported address family: %d", addr->sa_family);
	}
	return -1;
}

static int rdma_ptl_find_dst_nid(struct rdma_cm_id *id)
{
	return rdma_ptl_find_nid(&id->route.addr.dst_addr);
}

static int rdma_ptl_find_self_nid(struct rdma_cm_id *id)
{
	return rdma_ptl_find_nid(&id->route.addr.src_addr);
}

int rdma_create_qp(struct rdma_cm_id *id, struct ibv_pd *pd,
		   struct ibv_qp_init_attr *qp_init_attr)
{
	/**
	 * All the money here. Now everyone (ptl_qp, ptl_cm_id, ptl_pd) should know each other
	 * */
	struct ptl_context * ptl_cnxt = ptl_cnxt_get();
	struct ptl_pd *ptl_pd = ptl_pd_get_from_ibv_pd(pd);
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(id);
	struct ptl_cq *send_queue = ptl_cq_get_from_ibv_cq(qp_init_attr->send_cq);
	struct ptl_cq *recv_queue = ptl_cq_get_from_ibv_cq(qp_init_attr->recv_cq);

	struct ptl_qp *ptl_qp = ptl_id->ptl_qp;
	struct ptl_conn_comm_pair_info comm_pair_info;
	if (ptl_qp) {
		SPDK_PTL_DEBUG("Queue pair already there nothing to do, connected to remote nid: %d pid: %d portals index: %d",
			       ptl_qp->remote_nid, ptl_qp->remote_pid, ptl_qp->remote_pt_index);
		return 0;
	}
	comm_pair_info.dst_nid = rdma_ptl_find_dst_nid(id);
	comm_pair_info.dst_pid = PTL_TARGET_PID;
	comm_pair_info.dst_pte = PTL_PT_INDEX;
	comm_pair_info.src_nid = ptl_cnxt_get_nid(ptl_cnxt);
	comm_pair_info.src_pid = ptl_cnxt_get_pid(ptl_cnxt);
	comm_pair_info.src_pte = PTL_PT_INDEX;
	SPDK_PTL_DEBUG("Creating queue pair... connected to remote nid: %d pid: %d portals index: %d",
		       comm_pair_info.dst_nid, comm_pair_info.dst_pid, comm_pair_info.dst_pte);
	ptl_qp = ptl_qp_create(ptl_pd, send_queue, recv_queue, &comm_pair_info);
	ptl_qp->fake_qp.qp_num = ptl_id->ptl_qp_num;
	/*Update cm_id*/
	ptl_cm_id_set_ptl_qp(ptl_id, ptl_qp);
	ptl_cm_id_set_ptl_pd(ptl_id, ptl_pd);
	ptl_qp->ptl_cm_id = ptl_id;
	SPDK_PTL_DEBUG("Successfully created Portals Queue Pair Object and updated Portal CM ID and Queue Pair pointers");
	return 0;
}


/**
 * @brief Establish RDMA connection using Portals PUT operation
 *
 * This function implements the client-side connection establishment by:
 * - Preparing connection information (PID, NID, etc.)
 * - Sending this information to the server using PtlPut
 * - Creating a fake RDMA connection event for compatibility
 *
 * @param id RDMA communication identifier
 * @param conn_param Connection parameters including private data
 * @return 0 on success, negative value on failure
 */
int rdma_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct rdma_ptl_send_buffer *request_buf;
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(id);
	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	conn_param->initiator_depth = 16;
	/* Boot Control plane server if not booted*/
	rdma_ptl_boot_cp_server(ptl_id, "INITIATOR");

	if (sizeof(*request_buf) + conn_param->private_data_len > RDMA_PTL_MSG_BUFFER_SIZE) {
		SPDK_PTL_FATAL("connection parameter private_data_len too large!");
	}

	/*Allocate the buffer for the open connection request*/
	if (posix_memalign((void **)&request_buf, 4096, RDMA_PTL_MSG_BUFFER_SIZE)) {
		SPDK_PTL_FATAL("Failed to allocate ptl_conn_request");
	}
	request_buf->conn_msg.msg_header.version = PTL_SPDK_PROTOCOL_VERSION;
	request_buf->conn_msg.msg_header.msg_type = PTL_OPEN_CONNECTION;
	request_buf->conn_msg.msg_header.total_msg_size = sizeof(request_buf->conn_msg) +
		conn_param->private_data_len;


	request_buf->conn_msg.msg_header.peer_info.src_nid = ptl_cnxt_get_nid(ptl_cnxt);
	request_buf->conn_msg.msg_header.peer_info.src_pid = ptl_cnxt_get_pid(ptl_cnxt);
	request_buf->conn_msg.msg_header.peer_info.src_pte = PTL_CP_SERVER_PTE;
	request_buf->conn_msg.conn_open.initiator_qp_num = ptl_id->ptl_qp_num;
	/*Inform the target about the match bits I (the initiator) use for my recv operations*/
	request_buf->conn_msg.conn_open.recv_match_bits = ptl_id->my_match_bits;
	request_buf->conn_msg.conn_open.cq_id = ptl_id->ptl_qp->recv_cq->cq_id;
	request_buf->conn_msg.conn_open.rma_match_bits = PTL_UUID_RMA_MASK;

	memcpy(&request_buf->conn_msg.conn_open.src_addr, &id->route.addr.src_addr,
	       sizeof(request_buf->conn_msg.conn_open.src_addr));
	// SPDK_PTL_DEBUG("Source address is");
	// rdma_ptl_print_sockaddr(&id->route.addr.src_addr);
	// SPDK_PTL_DEBUG("Destination address is");
	// rdma_ptl_print_sockaddr(&id->route.addr.dst_addr);

	/* Setup target process identifier */
	request_buf->conn_msg.msg_header.peer_info.dst_nid = rdma_ptl_find_dst_nid(id);
	request_buf->conn_msg.msg_header.peer_info.dst_pid = PTL_TARGET_PID;
	request_buf->conn_msg.msg_header.peer_info.dst_pte = PTL_CP_SERVER_PTE;
	/*Now serialize the conn param staff*/
	request_buf->conn_msg.conn_open.conn_param = *conn_param;
	/*Intentionally, let the receiver fix this*/
	request_buf->conn_msg.conn_open.conn_param.private_data = NULL;
	/*Serialize the staff*/
	char *private_data_buf = (char*)request_buf + sizeof(*request_buf);
	if (conn_param->private_data) {
		memcpy(private_data_buf, conn_param->private_data, conn_param->private_data_len);
		SPDK_PTL_DEBUG("CONN_PARAM: Serialized connection params of size: %u "
			       "in OPEN_CONNECTION_REQUEST conn_msg size is: %lu "
			       "total message size: %lu",
			       conn_param->private_data_len,
			       sizeof(request_buf->conn_msg),
			       request_buf->conn_msg.msg_header.total_msg_size);
	}


	SPDK_PTL_DEBUG("[%s] CP server: Sending OPEN_CONNECTION_REQUEST to [nid: %d pid: %d pte: %d]",
		       ptl_control_plane_server.role, request_buf->conn_msg.msg_header.peer_info.dst_nid,
		       request_buf->conn_msg.msg_header.peer_info.dst_pid,
		       request_buf->conn_msg.msg_header.peer_info.dst_pte);

	// SPDK_PTL_DEBUG("[%s] CP server: Sending OPEN_CONNECTION_REQUEST from [nid: %d pid: %d pte: %d]",
	// 	       ptl_control_plane_server.role, request_buf->conn_msg.msg_header.peer_info.src_nid,
	// 	       request_buf->conn_msg.msg_header.peer_info.src_pid,
	// 	       request_buf->conn_msg.msg_header.peer_info.src_pte);

	ptl_id->conn_param.private_data = conn_param;/*TODO XXX is this ok?*/
	if (conn_param->private_data) {
		ptl_id->conn_param.private_data = calloc(1UL, conn_param->private_data_len);
		memcpy((void *)ptl_id->conn_param.private_data, conn_param->private_data,
		       conn_param->private_data_len);
	}
	ptl_id->cm_id_state = PTL_CM_CONNECTING;
	rdma_cm_ptl_send_request(request_buf, &request_buf->conn_msg.msg_header.peer_info);

	return 0;
}


int rdma_set_option(struct rdma_cm_id *id, int level, int optname, void *optval,
		    size_t optlen)
{
	SPDK_PTL_INFO("Ignoring (for) now options XXX TODO XXX");
	return 0;
}

int rdma_accept(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct rdma_cm_event *fake_event;
	struct ptl_context * ptl_cnxt = ptl_cnxt_get();
	struct rdma_ptl_send_buffer *conn_reply_buf;
	struct ptl_cm_id * ptl_id = ptl_cm_id_get(id);
	char *conn_param_private_data;
	struct ptl_conn_comm_pair_info comm_pair_info = {
		.dst_nid = ptl_id->ptl_qp->remote_nid,
		.dst_pid = ptl_id->ptl_qp->remote_pid,
		.dst_pte = PTL_CP_SERVER_PTE,
		.src_nid = ptl_cnxt_get_nid(ptl_cnxt),
		.src_pid = ptl_cnxt_get_pid(ptl_cnxt),
		.src_pte = PTL_CP_SERVER_PTE
	};

	SPDK_PTL_DEBUG("[%s] CP server: At accept sending uuid of the connection: %lu",
		       ptl_control_plane_server.role, ptl_id->uuid);


	if (sizeof(*conn_reply_buf) + conn_param->private_data_len > RDMA_PTL_MSG_BUFFER_SIZE) {
		SPDK_PTL_FATAL("connection parameter private_data_len too large!");
	}

	if (posix_memalign((void **)&conn_reply_buf, 4096, RDMA_PTL_MSG_BUFFER_SIZE)) {
		SPDK_PTL_FATAL("Failed to allocate memory");
	}

	/*first fill the reply parts*/
	conn_reply_buf->conn_msg.msg_header.version = PTL_SPDK_PROTOCOL_VERSION;
	conn_reply_buf->conn_msg.msg_header.msg_type = PTL_OPEN_CONNECTION_REPLY;
	conn_reply_buf->conn_msg.msg_header.total_msg_size = sizeof(conn_reply_buf->conn_msg) +
		conn_param->private_data_len;
	conn_reply_buf->conn_msg.conn_open_reply.status = PTL_OK;
	conn_reply_buf->conn_msg.conn_open_reply.uuid = ptl_id->uuid;

	/*Tell the initiator (you are the target) what are the match bits of my srq*/
	conn_reply_buf->conn_msg.conn_open_reply.srq_match_bits = PTL_UUID_TARGET_SRQ_MATCH_BITS;
	/*Tell the initiator (you are the target) in which cq_id you expect notifications*/
	conn_reply_buf->conn_msg.conn_open_reply.cq_id = ptl_id->ptl_qp->recv_cq->cq_id;

	conn_reply_buf->conn_msg.conn_open_reply.conn_param = *conn_param;
	conn_reply_buf->conn_msg.conn_open_reply.conn_param.private_data = NULL;/*Intentionally*/
	conn_param_private_data = (char*)conn_reply_buf + sizeof(*conn_reply_buf);

	if (conn_param->private_data) {
		SPDK_PTL_DEBUG("CONN_PARAM: Serializing conn param staff to send to the initiator");
		memcpy(conn_param_private_data, conn_param->private_data, conn_param->private_data_len);
	}

	rdma_cm_ptl_send_request(conn_reply_buf, &comm_pair_info);
	SPDK_PTL_DEBUG("[%s] DONE: SENT the accept message to initiator nid: %d pid: %d pt_index: %d",
		       ptl_control_plane_server.role,
		       comm_pair_info.dst_nid, comm_pair_info.dst_pid, comm_pair_info.dst_pte);
	/* At the target (rdma_accept) all good for the connection*/
	ptl_id->cm_id_state = PTL_CM_CONNECTED;
	fake_event = ptl_cm_id_create_event(ptl_id, NULL, RDMA_CM_EVENT_ESTABLISHED);
	ptl_cm_id_add_event(ptl_id, fake_event);
	rdma_ptl_write_event_to_fd(ptl_id->ptl_channel->fake_channel.fd);
	return 0;
}

int rdma_disconnect(struct rdma_cm_id *id)
{
	return 0;
	struct rdma_ptl_send_buffer *conn_close_request;
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(id);
	struct ptl_context *ptl_cnxt = ptl_cnxt_get();
	int initiator_qp_num = ptl_uuid_get_initiator_qp_num(ptl_id->uuid);
	int target_qp_num = ptl_uuid_get_target_qp_num(ptl_id->uuid);


	if (initiator_qp_num == 0) {
		SPDK_PTL_FATAL("Nida does not assign 0 as an initiator qp num!");
	}

	if (target_qp_num == 0) {
		SPDK_PTL_FATAL("Nida does not assign 0 as a target qp num!");
	}

	if (ptl_id->cm_id_state < 0 || ptl_id->cm_id_state > PTL_CM_GUARD) {
		SPDK_PTL_FATAL("Corrupted state: %d", ptl_id->cm_id_state);

	}

	if (ptl_id->cm_id_state == PTL_CM_UNCONNECTED) {
		SPDK_PTL_FATAL("End up in a wrong state PTL_CM_UNCONNECTED");
	}

	if (ptl_id->cm_id_state == PTL_CM_DISCONNECTED) {
		SPDK_PTL_FATAL("End up in a wrong state PTL_CM_DISCONNECTED");
	}

	if (ptl_id->cm_id_state == PTL_CM_CONNECTING) {
		SPDK_PTL_FATAL("End up in a wrong state PTL_CM_CONNECTING");
	}

	if (ptl_id->cm_id_state == PTL_CM_DISCONNECTING) {
		SPDK_PTL_DEBUG("[%s] CP server: No-op ptl_id already disconnecting/disconnected for queue pair initiator: %d target: %d",
			       ptl_control_plane_server.role, ptl_uuid_get_initiator_qp_num(ptl_id->uuid),
			       ptl_uuid_get_target_qp_num(ptl_id->uuid));
		ptl_id->cm_id_state = PTL_CM_DISCONNECTED;
		return 0;
	}

	SPDK_PTL_DEBUG("[%s] CP server: Calling disconnect for queue pairs initiator: %d target: %d",
		       ptl_control_plane_server.role, ptl_uuid_get_initiator_qp_num(ptl_id->uuid),
		       ptl_uuid_get_target_qp_num(ptl_id->uuid));
	if (is_target) {
		SPDK_PTL_DEBUG("CAUTION What? Initiating disconnect operation from the target.");
	}


	/*Allocate the buffer for the open connection request*/
	if (posix_memalign((void **)&conn_close_request, 4096, sizeof(*conn_close_request))) {
		SPDK_PTL_FATAL("Failed to allocate ptl_conn_request");
	}
	conn_close_request->conn_msg.msg_header.version = PTL_SPDK_PROTOCOL_VERSION;
	conn_close_request->conn_msg.msg_header.msg_type = PTL_CLOSE_CONNECTION;
	conn_close_request->conn_msg.msg_header.total_msg_size = sizeof(conn_close_request->conn_msg);
	conn_close_request->conn_msg.conn_close.uuid = ptl_id->uuid;
	conn_close_request->conn_msg.msg_header.peer_info.dst_nid = ptl_id->ptl_qp->remote_nid;
	conn_close_request->conn_msg.msg_header.peer_info.dst_pid = ptl_id->ptl_qp->remote_pid;
	conn_close_request->conn_msg.msg_header.peer_info.dst_pte = PTL_CP_SERVER_PTE;
	conn_close_request->conn_msg.msg_header.peer_info.src_nid = ptl_cnxt_get_nid(ptl_cnxt);
	conn_close_request->conn_msg.msg_header.peer_info.src_pid = ptl_cnxt_get_pid(ptl_cnxt);
	conn_close_request->conn_msg.msg_header.peer_info.src_pte = PTL_CP_SERVER_PTE;
	conn_close_request->conn_msg.conn_close.uuid = ptl_id->uuid;


	rdma_cm_ptl_send_request(conn_close_request, &conn_close_request->conn_msg.msg_header.peer_info);
	ptl_id->cm_id_state = PTL_CM_DISCONNECTING;

	// SPDK_PTL_DEBUG("[%s] CP server creating a *FAKE* disconnected event",
	// 	       ptl_control_plane_server.role);
	// fake_event = ptl_cm_id_create_event(ptl_id, NULL, RDMA_CM_EVENT_DISCONNECTED);
	// ptl_cm_id_add_event(ptl_id, fake_event);
	// rdma_ptl_write_event_to_fd(ptl_id->ptl_channel->fake_channel.fd);
	return 0;
}


int ibv_destroy_cq(struct ibv_cq *cq)
{
	struct ptl_cq *ptl_cq = ptl_cq_get_from_ibv_cq(cq);
	SPDK_PTL_DEBUG("PtlCQ: destroy CAUTION, ignore this XXX TODO XXX");
	ptl_cq->is_in_use = false;
	return 0;
}



int rdma_destroy_id(struct rdma_cm_id *id)
{
	struct ptl_cm_id *ptl_id = ptl_cm_id_get(id);
	SPDK_PTL_DEBUG("QP NUM = %d ptl_id = %p CAUTION, is this ok? XXX TODO XXX", ptl_id->ptl_qp_num,
		       ptl_id);
	rdma_ptl_conn_map_remove(ptl_id);
	memset(ptl_id, 0x00, sizeof(*ptl_id));
	free(ptl_id);
	return 0;
}

void rdma_destroy_event_channel(struct rdma_event_channel *channel)
{
	SPDK_PTL_DEBUG("CAUTION: Destroying ptl_channel Ignore XXX TODO XXX");
	struct rdma_cm_ptl_event_channel *ptl_channel = SPDK_CONTAINEROF(channel,
		struct rdma_cm_ptl_event_channel, fake_channel);
	if (ptl_channel->magic_number != RDMA_CM_PTL_EVENT_CHANNEL_MAGIC_NUMBER) {
		SPDK_PTL_FATAL("Corrupted object");
	}

	if (close(channel->fd) != 0) {
		SPDK_PTL_FATAL("Error closing eventfd: %s", strerror(errno));
	}
	memset(ptl_channel, 0x00, sizeof(*ptl_channel));
	free(ptl_channel);
}

int ibv_dealloc_pd(struct ibv_pd *pd)
{
	SPDK_PTL_DEBUG("CAUTION Do nothing pd is singleton staff in Portals only for compatibility XXX TODO XXX");
	return 0;
}
