// SPDX-License-Identifier: GPL-2.0
/*
 * Fake RDMA-CM "portals" shim — no-op / fail-fast mock for NVMe-RDMA
 * experimentation
 */
#include "rdma_cm_portals.h"
#include "ib_portals.h"
#include "linux/dma-direction.h"
#include "portals4_bxiext.h"
#include "ptl_bxiv3_dev_map.h"
#include "ptl_bxiv3_device.h"
#include "ptl_cm_id.h"
#include "ptl_connection.h"
#include "ptl_cq.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include "ptl_qp.h"
#include "ptl_uuid.h"
#include "rdma/ib_verbs.h"
#include <asm-generic/errno-base.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/inet.h> //for printing sockaddr
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/socket.h> //for printing sockaddr
#include <net/sock.h>     //for printing sockaddr
#include <portals4.h>
#include <rdma/rdma_cm.h>
#include <rdma/rdma_user_cm.h>


#define RDMA_PTL_MSG_BUFFER_SIZE 256UL
#define PTL_INITIATOR_DEPTH 32
/* --- Rate-limited "unimplemented" logging ---- */
#define GES_UNIMPL_RATELIMIT_PERIOD HZ
#define GES_UNIMPL_RATELIMIT_BURST 10
static DEFINE_RATELIMIT_STATE(ges_unimpl_rs, GES_UNIMPL_RATELIMIT_PERIOD,
			      GES_UNIMPL_RATELIMIT_BURST);

#define RDMACM_IB_UNIMPL(fmt, ...)                                             \
  do {                                                                         \
    if (__ratelimit(&ges_unimpl_rs))                                           \
      pr_warn("[%s:%s:%d]UNIMPLEMENTED " fmt "\n", __FILE__, __func__,         \
              __LINE__, ##__VA_ARGS__);                                        \
  } while (0)

/* --- Module‑param toggle for success/failure ---- */
static bool ges_fail_fast = true;
module_param_named(fail_fast, ges_fail_fast, bool, 0644);
MODULE_PARM_DESC(
	fail_fast,
	"If true, return -EOPNOTSUPP for all ops; if false, always succeed.");

/* --- Helpers ---- */
static inline int ges_ret(int fail_code)
{
	(void)fail_code;
	return ges_fail_fast ? fail_code : 0;
}




const char *ptl_msg_types[PTL_NUM_MSGS] = {"NVMeOF_cmd",
					   "NVMeOF_cpl",
					   "NVMeOF_rma",
					   "PTL_OPEN_CONNECTION",
					   "PTL_OPEN_CONNECTION_REPLY",
					   "PTL_OPEN_CONNECTION",
					   "PTL_OPEN_CONNECTION_REPLY"
					  };

static int rdma_cm_ptl_send_request(struct ptl_conn_send_buffer *send_buffer,
				    struct ptl_cm_id *ptl_id)
{
	extern struct ptl_bxiv3_dev_map bxiv3_dev_map;

	ptl_process_t target;
	ptl_hdr_data_t message_type = send_buffer->conn_msg.msg_header.msg_type;
	int rc;

	struct ptl_conn_comm_pair_info *peer_info = &send_buffer->conn_msg.msg_header.peer_info;
	target.phys.nid = peer_info->dest.nid;
	target.phys.pid = peer_info->dest.pid;

	/* Create memory descriptor for the connection info */
	memset(&send_buffer->md, 0, sizeof(send_buffer->md));
	send_buffer->md.length = send_buffer->conn_msg.msg_header.total_msg_size;
	send_buffer->md.cpu_start = &send_buffer->conn_msg;
	send_buffer->md.start = ib_portals_dma_map_single(&ptl_id->bxiv3_dev->fake_ib_dev,
				send_buffer->md.cpu_start, send_buffer->md.length, DMA_FROM_DEVICE);
	if (ib_portals_dma_mapping_error(&ptl_id->bxiv3_dev->fake_ib_dev, send_buffer->md.start)) {
		PTL_FATAL("DMA mapping failed for length %llu on PtlMDBInd", send_buffer->md.length);
		return -EIO;
	}
	send_buffer->md.options = 0;
	send_buffer->md.eq_handle = ptl_id->bxiv3_dev->conn_mgmt_eq->eq;
	// send_buffer->md.eq_handle = bxiv3_dev_map.bxiv3_dev[0]->conn_mgmt_eq->eq;
	PTL_DEBUG("Associated the send buffer with device id: %d", ptl_id->bxiv3_dev->iface_id);
	send_buffer->md.ct_handle = PTL_CT_NONE;
	PTL_CHECK(ptl_id->bxiv3_dev, PTL_BXIV3_DEVICE);

	rc = PtlMDBind(ptl_id->bxiv3_dev->nicia_handle, &send_buffer->md, &send_buffer->md_handle);
	if (rc != PTL_OK) {
		PTL_FATAL("PtlMDBind failed with reason: %s", PtlToStr(rc, PTL_STR_ERROR));
	}

	// ptl_hdr_data_t hdr = PTL_NI_ARG_INVALID;
	PTL_DEBUG("Sending message: %s and total "
		  "size in B: %llu to {nid:%d,pid:%d,pte:%d}",
		  ptl_msg_types[send_buffer->conn_msg.msg_header.msg_type],
		  send_buffer->conn_msg.msg_header.total_msg_size,
		  target.phys.nid, target.phys.pid, peer_info->dest.pte);

	rc = PtlPut(send_buffer->md_handle,/* MD handle */
		    0,/* local offset */
		    send_buffer->conn_msg.msg_header.total_msg_size,/* length */
		    PTL_ACK_REQ,/* acknowledgment requested */
		    target, /* target process */
		    peer_info->dest.pte,  /* portal table index */
		    0,
		    0, /* remote offset */
		    send_buffer,
		    message_type);

	if (rc != PTL_OK) {
		PTL_FATAL("PtlPut failed with code: %d", rc);
		return rc;
	}
	return 0;
}


/* --- API stubs ---- */

struct rdma_cm_id *__rdma_cm_portals_create_kernel_id(
	struct net *net, rdma_cm_event_handler event_handler, void *context,
	enum rdma_ucm_port_space ps, enum ib_qp_type qp_type, const char *caller)
{
	struct ptl_cm_id *ptl_cm_id =
		ptl_cm_id_create(net, event_handler, context, ps, qp_type, caller);
	return &ptl_cm_id->fake_cm_id;
}

EXPORT_SYMBOL_GPL(__rdma_cm_portals_create_kernel_id);

int rdma_cm_portals_destroy_id(struct rdma_cm_id *id)
{
	(void)id;
	RDMACM_IB_UNIMPL("Sorry");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_destroy_id);

#include <linux/inet.h>
#include <linux/socket.h>
#include <net/sock.h>

/**
 * print_sockaddr - Print details of a struct sockaddr
 * @addr: Pointer to struct sockaddr to print
 * @addrlen: Length of the address structure
 */
// static void rdma_cm_portals_print_sockaddr(const struct sockaddr *addr, int
// addrlen)
// {
//     if (!addr) {
//         pr_info("sockaddr: NULL\n");
//         return;
//     }

//     pr_info("sockaddr family: %d, length: %d\n", addr->sa_family, addrlen);

//     switch (addr->sa_family) {
//     case AF_INET: {
//         struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
//         pr_info("  IPv4 Address: %pI4:%d\n",
//                 &addr_in->sin_addr.s_addr,
//                 ntohs(addr_in->sin_port));
//         break;
//     }
//     case AF_INET6: {
//         struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
//         pr_info("  IPv6 Address: %pI6c:%d\n",
//                 &addr_in6->sin6_addr,
//                 ntohs(addr_in6->sin6_port));
//         pr_info("  Flow info: 0x%08x, Scope ID: %d\n",
//                 addr_in6->sin6_flowinfo,
//                 addr_in6->sin6_scope_id);
//         break;
//     }
//     case AF_UNSPEC:
//         pr_info("  Unspecified address family\n");
//         break;
//     default:
//         pr_info("  Unknown/unsupported address family\n");
//         break;
//     }
// }

int rdma_cm_portals_resolve_addr(struct rdma_cm_id *id,
				 struct sockaddr *src_addr,
				 const struct sockaddr *dst_addr,
				 unsigned long timeout_ms)
{
	struct rdma_cm_event event = {0};
	struct ptl_cm_id *ptl_cm_id = container_of(id, struct ptl_cm_id, fake_cm_id);
	int ret;
	if (!id) {
		return -EINVAL;
	}

	if (!dst_addr) {
		return -EINVAL;
	}

	if (ptl_cm_id->object_type != PTL_CM_ID) {
		PTL_FATAL("Corrupted ptl_cm_id");
		return -EINVAL;
	}

	ret = ptl_cm_id_resolve_addr(ptl_cm_id, src_addr, dst_addr, timeout_ms);
	if (ret) {
		event.event = RDMA_CM_EVENT_ADDR_ERROR;
		event.status = -ENETUNREACH;
	} else {
		event.event = RDMA_CM_EVENT_ADDR_RESOLVED;
	}
	return ptl_cm_id->event_handler(&ptl_cm_id->fake_cm_id, &event);
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_resolve_addr);

int rdma_cm_portals_resolve_route(struct rdma_cm_id *id,
				  unsigned long timeout_ms)
{
	/**
	 * XXX TODO XXX Think if we need here to do something like ping the
	 * targer or something similar
	 */
	struct rdma_cm_event event = {0};
	struct ptl_cm_id *ptl_id = container_of(id, struct ptl_cm_id, fake_cm_id);
	if (ptl_id->object_type != PTL_CM_ID) {
		PTL_FATAL("Corrupted PTL_CM_ID object!");
		return -EINVAL;
	}
	event.event = RDMA_CM_EVENT_ROUTE_RESOLVED;
	return ptl_id->event_handler(&ptl_id->fake_cm_id, &event);
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_resolve_route);



int rdma_cm_portals_connect_locked_with_ptl_params(
	struct rdma_cm_id *id, struct rdma_conn_param *param,
	u64 nvme_cpl_start_dma_addr, size_t queue_size)
{
	struct ptl_cm_id *ptl_id = container_of(id, struct ptl_cm_id, fake_cm_id);
	PTL_CHECK(ptl_id, PTL_CM_ID);
	ptl_id->nvme_cpl_start =  nvme_cpl_start_dma_addr;
	ptl_id->nvme_completion_queue_size = queue_size;
	return rdma_cm_portals_connect_locked(id, param);
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_connect_locked_with_ptl_params);

int rdma_cm_portals_connect_locked(struct rdma_cm_id *id,
				   struct rdma_conn_param *param)
{
	struct ptl_conn_send_buffer *send_buffer;
	char *private_data_buf;
	struct ptl_cm_id *ptl_id = container_of(id, struct ptl_cm_id, fake_cm_id);
	PTL_CHECK(ptl_id, PTL_CM_ID);

	if (0 == param->initiator_depth) {
		PTL_DEBUG("Caution initiator_depth set to 0. Setting it to %d", PTL_INITIATOR_DEPTH);
		param->initiator_depth = PTL_INITIATOR_DEPTH;
	}

	send_buffer = kzalloc(RDMA_PTL_MSG_BUFFER_SIZE, GFP_KERNEL);
	if (!send_buffer) {
		PTL_FATAL("Out of memory");
		return -ENOMEM;
	}
	send_buffer->object_type = PTL_CONN_SEND_BUFFER;
	send_buffer->conn_msg.msg_header.version = PTL_SPDK_PROTOCOL_VERSION;
	send_buffer->conn_msg.msg_header.msg_type = PTL_OPEN_CONNECTION;
	send_buffer->conn_msg.msg_header.total_msg_size = sizeof(send_buffer->conn_msg) +
		param->private_data_len;
	if (send_buffer->conn_msg.msg_header.total_msg_size > RDMA_PTL_MSG_BUFFER_SIZE) {
		PTL_FATAL("Buffer too small");
	}
	/*Setup self*/
	send_buffer->conn_msg.msg_header.peer_info.src.nid = ptl_id->nid;
	send_buffer->conn_msg.msg_header.peer_info.src.pid = ptl_id->pid;
	send_buffer->conn_msg.msg_header.peer_info.src.pte = PTL_CP_SERVER_PTE;
	/*Destination*/
	send_buffer->conn_msg.msg_header.peer_info.dest.nid = ptl_id->remote_nid;
	send_buffer->conn_msg.msg_header.peer_info.dest.pid = ptl_id->remote_pid;
	send_buffer->conn_msg.msg_header.peer_info.dest.pte = PTL_CP_SERVER_PTE;


	PTL_DEBUG("Initator QP NUM: %d", send_buffer->conn_msg.conn_open.initiator_qp_num);
	/*Inform the target about the match bits I (the initiator) use for my recv operations*/
	send_buffer->conn_msg.conn_open.msg_pte = ptl_id->ptl_qp->recv_cq->pte;
	send_buffer->conn_msg.conn_open.rma_pte = PTL_RMA_PTE;
	send_buffer->conn_msg.conn_open.cq_id = ptl_id->ptl_qp->recv_cq->ptl_cq_id;
	send_buffer->conn_msg.conn_open.initiator_qp_num = ptl_id->ptl_qp->qpn;
	/*Extensions*/
	send_buffer->conn_msg.conn_open.is_kernel_initiator = 1;
	send_buffer->conn_msg.conn_open.nvme_cpl_start_addr = ptl_id->nvme_cpl_start;
	send_buffer->conn_msg.conn_open.nvme_cpl_queue_size = ptl_id->nvme_completion_queue_size;

	memcpy(&send_buffer->conn_msg.conn_open.src_addr, &id->route.addr.src_addr,
	       sizeof(send_buffer->conn_msg.conn_open.src_addr));

	/*Now serialize the conn param staff*/
	send_buffer->conn_msg.conn_open.conn_param = *param;
	/*Intentionally, let the receiver fix this*/
	send_buffer->conn_msg.conn_open.conn_param.private_data = NULL;
	PTL_DEBUG("Initiator depth: %u", param->initiator_depth);

	if (param->private_data) {
		private_data_buf = (char*)send_buffer + sizeof(*send_buffer);
		memcpy(private_data_buf, param->private_data, param->private_data_len);
		send_buffer->conn_msg.conn_open.conn_param.private_data_len = param->private_data_len;
		PTL_DEBUG("CONN_PARAM: Serialized connection params of size: %u "
			  "in OPEN_CONNECTION_REQUEST conn_msg size is: %lu "
			  "total message size: %llu param private data len = %u",
			  param->private_data_len,
			  sizeof(send_buffer->conn_msg),
			  send_buffer->conn_msg.msg_header.total_msg_size, param->private_data_len);
	}

	/*Keep a copy also of conn param in ptl_cm_id, Why? XXX TODO XXX*/
	ptl_id->param = *param;
	if (param->private_data) {
		ptl_id->param.private_data = kzalloc(param->private_data_len, GFP_KERNEL);
		if (ptl_id->param.private_data == NULL) {
			/*XXX TODO XXX rollback*/
		}
		memcpy((void *)ptl_id->param.private_data, param->private_data,
		       param->private_data_len);
	}
	ptl_id->cm_id_state = PTL_CM_CONNECTING;
	rdma_cm_ptl_send_request(send_buffer, ptl_id);
	return 0;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_connect_locked);

int rdma_cm_portals_disconnect(struct rdma_cm_id *id)
{
	(void)id;
	PTL_FATAL("Unimplemented Sorry");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_disconnect);

int rdma_cm_portals_create_qp(struct rdma_cm_id *id, struct ib_pd *pd,
			      struct ib_qp_init_attr *attr)
{
	struct ptl_cm_id *ptl_id;
	struct ptl_pd *ptl_pd;
	ptl_id = container_of(id, struct ptl_cm_id, fake_cm_id);
	if (PTL_CM_ID != ptl_id->object_type) {
		PTL_FATAL("Corrupted ptl_cm_id!");
		return -EINVAL;
	}

	ptl_pd = container_of(pd, struct ptl_pd, fake_pd);
	if (PTL_PD != ptl_pd->object_type) {
		PTL_FATAL("Corrupted ptl_pd!");
		return -EINVAL;
	}
	ptl_id->ptl_qp = ptl_qp_create(ptl_id, ptl_pd, attr);

	/*appropriate wiring needed*/
	ptl_id->fake_cm_id.qp = &ptl_id->ptl_qp->fake_qp;

	PTL_DEBUG("Created QP successfully");
	return IS_ERR(ptl_id->ptl_qp) ? -EINVAL : 0;
}
EXPORT_SYMBOL_GPL(rdma_cm_portals_create_qp);

int rdma_cm_portals_destroy_qp(struct rdma_cm_id *id)
{
	(void)id;
	RDMACM_IB_UNIMPL("Sorry");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_destroy_qp);

/* --- Helpers that return strings/data ---- */

const char *rdma_cm_portals_event_msg(int ev)
{
	(void)ev;
	RDMACM_IB_UNIMPL("Sorry");
	return "RDMACM-IB-unimplemented";
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_event_msg);

const void *rdma_cm_portals_reject_msg(struct rdma_cm_id *id, int status)
{
	(void)id;
	(void)status;
	RDMACM_IB_UNIMPL("Sorry");
	return ERR_PTR(-EOPNOTSUPP);
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_reject_msg);

const void *rdma_cm_portals_consumer_reject_data(struct rdma_cm_id *id,
		struct rdma_cm_event *ev,
		u8 *data_len)
{
	(void)id;
	(void)ev;
	(void)data_len;
	RDMACM_IB_UNIMPL("Sorry");
	return ERR_PTR(-EOPNOTSUPP);
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_consumer_reject_data);

int rdma_cm_portals_set_service_type(struct rdma_cm_id *id, u8 tos)
{
	(void)id;
	(void)tos;
	RDMACM_IB_UNIMPL("Sorry");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_set_service_type);

int rdma_cm_portals_connect(struct rdma_cm_id *id,
			    struct rdma_conn_param *conn_param)
{
	(void)id;
	(void)conn_param;
	RDMACM_IB_UNIMPL("Sorry");
	return -EOPNOTSUPP;
}

EXPORT_SYMBOL_GPL(rdma_cm_portals_connect);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(
	"RDMACM implementation over Portals4 shim (no-op / fail-fast)");
MODULE_AUTHOR("Giorgis Saloustris (Chatzis) gesalous@ics.forth.gr");
