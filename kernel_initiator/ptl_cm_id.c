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
	if (!ptl_cm_id) {
		return ERR_PTR(-ENOMEM);
	}
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
	if (!ptl_cm_id || !dst_addr) {
		return -EINVAL;
	}

	switch (dst_addr->sa_family) {
	case AF_INET: {
		addr_in = (struct sockaddr_in *)dst_addr;

		/* Last byte of IPv4 address */
		ptl_cm_id->remote_nid = ((unsigned char *)&addr_in->sin_addr.s_addr)[3];

		/* Port number (convert from network to host byte order) */
		ptl_cm_id->remote_pid = ntohs(addr_in->sin_port);
		break;
	}
	case AF_INET6: {
		addr_in6 = (struct sockaddr_in6 *)dst_addr;

		/* Last byte of IPv6 address */
		ptl_cm_id->remote_nid = addr_in6->sin6_addr.s6_addr[15];

		/* Port number (convert from network to host byte order) */
		ptl_cm_id->remote_pid = ntohs(addr_in6->sin6_port);
		break;
	}
	default:
		return -EAFNOSUPPORT;
	}
	ptl_cm_id->remote_nid = ptl_cm_id->remote_nid << 7;

	ptl_cm_id->bxiv3_dev = bxiv3_dev_map.bxiv3_dev[0];
	ptl_cm_id->nid = ptl_cm_id->bxiv3_dev->proc_id.phys.nid;
	ptl_cm_id->pid = ptl_cm_id->bxiv3_dev->proc_id.phys.pid;
	PTL_DEBUG("Resolved Target address: {nid:%d,pid:%d} initiator is "
		  "{nid:%d, pid:%d}",
		  ptl_cm_id->remote_nid, ptl_cm_id->remote_pid, ptl_cm_id->nid,
		  ptl_cm_id->pid);
	PTL_DEBUG("Statically assign ptl_cm_id to BXIv3-1 XXX TODO XXX: spread it dynamically Ok: %s",
		  "yes");
	return 0;
}

