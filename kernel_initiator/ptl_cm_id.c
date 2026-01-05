#include "ptl_cm_id.h"
#include "linux/cpumask.h"
#include "linux/gfp_types.h"
#include "linux/kref.h"
#include "linux/slab.h"
#include "ptl_bxiv3_dev_map.h"
#include "ptl_bxiv3_device.h"
#include "ptl_object_types.h"
#include <linux/string.h>

extern struct ptl_bxiv3_dev_map bxiv3_dev_map;
static unsigned long next_nicia_num = 0;
struct ptl_cm_id *ptl_cm_id_create(struct net *net,
                                   rdma_cm_event_handler event_handler,
                                   void *context, enum rdma_ucm_port_space ps,
                                   enum ib_qp_type qp_type,
                                   const char *caller)
{
	struct ptl_cm_id *ptl_cm_id;
	(void)qp_type;
	if (RDMA_PS_TCP != ps) {
		PTL_FATAL("Sorry only RDMA_PS_TCP supported");
		return ERR_PTR(-EOPNOTSUPP);
	}
	ptl_cm_id = kzalloc(sizeof(*ptl_cm_id), GFP_KERNEL);
	if (!ptl_cm_id)
		return ERR_PTR(-ENOMEM);
	ptl_cm_id->object_type = PTL_CM_ID;
	ptl_cm_id->net = get_net(net);
	ptl_cm_id->event_handler = event_handler;
	ptl_cm_id->event_handler_context = context;

	/*Wiring staff of the og rdma_cm_id for the bottom layer of the driver to work */
	kref_get(&bxiv3_dev_map.bxiv3_dev[next_nicia_num]->count);
	ptl_cm_id->fake_cm_id.device =
	        &bxiv3_dev_map.bxiv3_dev[next_nicia_num]->fake_ib_dev;

	ptl_cm_id->fake_cm_id.context = context;
	ptl_cm_id->fake_cm_id.event_handler = event_handler;/*Just in case*/


	//  ptl_cm_id->fake_cm_id.device =
	//     kzalloc(sizeof(*ptl_cm_id->fake_cm_id.device), GFP_KERNEL);
	// strlcpy(ptl_cm_id->fake_cm_id.device->name, "BXIv3",
	//    IB_DEVICE_NAME_MAX);
	// ptl_cm_id->fake_cm_id.device->attrs.device_cap_flags =
	//     IB_DEVICE_MEM_MGT_EXTENSIONS;
	// ptl_cm_id->fake_cm_id.device->attrs.max_send_sge =
	//     PTL_RDMA_MAX_INLINE_SEGMENTS;
	// ptl_cm_id->fake_cm_id.device->num_comp_vectors = num_online_cpus();
	ptl_cm_id->nid = -1;
	ptl_cm_id->pid = -1;
	PTL_DEBUG("Created a new ptl cm id from called: %s", caller);
	return ptl_cm_id;
}

int ptl_cm_id_resolve_addr(struct ptl_cm_id *ptl_cm_id,
                           struct sockaddr *src_addr,
                           const struct sockaddr *dst_addr,
                           unsigned long timeout_ms)
{

	struct sockaddr_in *addr_in;
	struct sockaddr_in6 *addr_in6;
	if (!ptl_cm_id || !dst_addr)
		return -EINVAL;

	switch (dst_addr->sa_family) {
	case AF_INET: {
		addr_in = (struct sockaddr_in *)dst_addr;

		/* Last byte of IPv4 address */
		ptl_cm_id->target_nid = ((unsigned char *)&addr_in->sin_addr.s_addr)[3];

		/* Port number (convert from network to host byte order) */
		ptl_cm_id->target_pid = ntohs(addr_in->sin_port);
		break;
	}
	case AF_INET6: {
		addr_in6 = (struct sockaddr_in6 *)dst_addr;

		/* Last byte of IPv6 address */
		ptl_cm_id->target_nid = addr_in6->sin6_addr.s6_addr[15];

		/* Port number (convert from network to host byte order) */
		ptl_cm_id->target_pid = ntohs(addr_in6->sin6_port);
		break;
	}
	default:
		return -EAFNOSUPPORT;
	}
	ptl_cm_id->target_nid = ptl_cm_id->target_nid << 7;

	ptl_cm_id->bxiv3_dev = bxiv3_dev_map.bxiv3_dev[0];
	ptl_cm_id->nid = ptl_cm_id->bxiv3_dev->proc_id.phys.nid;
	ptl_cm_id->pid = ptl_cm_id->bxiv3_dev->proc_id.phys.pid;
	PTL_DEBUG("Resolved Target's nid: %d pid: %d initiator is {nid:%d, pid:%d}", ptl_cm_id->target_nid, ptl_cm_id->target_pid, ptl_cm_id->nid, ptl_cm_id->pid);
	PTL_DEBUG("Statically assign ptl_cm_id to BXIv3-1 XXX TODO XXX: spread it dynamically Ok: %s", "yes");
	return 0;
}

// struct rdma_cm_event *ptl_cm_id_create_event(struct ptl_cm_id *ptl_id, struct
// ptl_cm_id *listen_id,
//              enum rdma_cm_event_type event_type)
// {

//      struct rdma_cm_event *fake_event;
//      /*Create a fake event*/
//      fake_event = calloc(1UL, sizeof(struct rdma_cm_event));
//      if (!fake_event) {
//              SPDK_PTL_FATAL("No memory!");
//      }
//      fake_event->id = &ptl_id->fake_cm_id;
//      SPDK_PTL_DEBUG("CP server: creating event %d for qp num: %d",
//      event_type,
//                     fake_event->id->qp ? fake_event->id->qp->qp_num : -128);
//      fake_event->listen_id = (void*)0xFFFFFFFFFFFFFFFF;
//      if (listen_id) {
//              fake_event->listen_id = &listen_id->fake_cm_id;
//      }
//      fake_event->status = 0;
//      fake_event->event = event_type;
//      //original
//      // fake_event->param.conn.private_data = ptl_id->fake_data;
//      /*rdma_cm library uses the private_data field to negotiate a new
//      connection*/ fake_event->param.conn = ptl_id->conn_param; if
//      (ptl_id->conn_param.private_data) {
//              SPDK_PTL_DEBUG("CONN_PARAM: setting connection params for this
//              event"); fake_event->param.conn.private_data = calloc(1UL,
//              fake_event->param.conn.private_data_len); memcpy((void
//              *)fake_event->param.conn.private_data,
//              ptl_id->conn_param.private_data,
//                     fake_event->param.conn.private_data_len);
//      }
//      // fake_event->param.conn.private_data = private_data;
//      // fake_event->param.conn.private_data_len = private_data_len;
//      // fake_event->param.conn.initiator_depth = 32;
//      // fake_event->param.conn.responder_resources = 0;
//      // fake_event->param.conn.retry_count = 7;
//      // fake_event->param.conn.rnr_retry_count = 7;
//      return fake_event;
// }

// void ptl_cm_id_add_event(struct ptl_cm_id *ptl_id,
//                       struct rdma_cm_event *event)
// {
//      if (NULL == ptl_id->ptl_channel) {
//              SPDK_PTL_FATAL("NULL channel in ptl_cm_id? why?");
//      }
//      rdma_cm_ptl_event_channel_lock_event_deque(ptl_id->ptl_channel);
//      if (false ==
//          deque_push_front(ptl_id->ptl_channel->events_deque, event)) {
//              SPDK_PTL_FATAL("Failed to queue fake event");
//      }
//      //gesalous XXX TODO XXX we do not need this for now.
//      // uint64_t result = 0;
//      // if (write(ptl_id->ptl_channel->fake_channel.fd, &result,
//      sizeof(result)) != sizeof(result)) {
//      //      perror("write failed reason:");
//      //      SPDK_PTL_WARN("Failed to write event");
//      // }
//      rdma_cm_ptl_event_channel_unlock_event_deque(ptl_id->ptl_channel);
// }

// void ptl_cm_id_set_fake_data(struct ptl_cm_id *ptl_id,
//                           const void *fake_data)
// {
//      ptl_id->fake_data = fake_data;
// }
