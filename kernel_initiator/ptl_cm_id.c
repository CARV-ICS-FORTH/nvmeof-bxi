#include "ptl_cm_id.h"
#include <linux/slab.h>
#include <linux/err.h>
#include <net/net_namespace.h>
#include <linux/printk.h>
#include <linux/string.h>
#include "ptl_object_types.h"
#include <linux/in.h>            
#include <linux/in6.h>           
#include <linux/kref.h>         
#include <linux/spinlock.h>      
#include "ptl_bxiv3_dev_map.h"   
#include "ptl_bxiv3_device.h"    
/* the global device map, defined in the device layer */
extern struct ptl_bxiv3_dev_map bxiv3_dev_map;   
static unsigned long next_nicia_num = 0;
/* State machine */
int ptl_cm_id_set_state(struct ptl_cm_id *id, ptl_cm_id_e new_state)   
{
    unsigned long flags;

    spin_lock_irqsave(&id->state_lock, flags);

    if (id->cm_id_state == new_state)
        goto out;

    switch (id->cm_id_state) {

    case PTL_CM_IDLE:
        if (new_state == PTL_CM_ADDR_RESOLVING ||
            new_state == PTL_CM_CONNECTING ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_ADDR_RESOLVING:
        if (new_state == PTL_CM_ADDR_RESOLVED ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_ADDR_RESOLVED:
        if (new_state == PTL_CM_ROUTE_RESOLVING ||
            new_state == PTL_CM_CONNECTING ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_ROUTE_RESOLVING:
        if (new_state == PTL_CM_ROUTE_RESOLVED ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_ROUTE_RESOLVED:
        if (new_state == PTL_CM_CONNECTING ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_CONNECTING:
        if (new_state == PTL_CM_ESTABLISHED ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_ESTABLISHED:
        if (new_state == PTL_CM_DISCONNECTING ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_DISCONNECTING:
        if (new_state == PTL_CM_DISCONNECTED ||
            new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_DISCONNECTED:
        if (new_state == PTL_CM_ERROR)
            break;
        goto invalid;

    case PTL_CM_ERROR:
        if (new_state == PTL_CM_IDLE)
            break;
        goto invalid;

    default:
        goto invalid;
    }

    PTL_DEBUG("ptl_cm_id: state %d -> %d", id->cm_id_state, new_state);   
    id->cm_id_state = new_state;

out:
    spin_unlock_irqrestore(&id->state_lock, flags);
    return 0;

invalid:
    PTL_WARN("ptl_cm_id: invalid state transition %d -> %d",              
           id->cm_id_state, new_state);
    spin_unlock_irqrestore(&id->state_lock, flags);
    return -EINVAL;
}

/* Lifecycle */


struct ptl_cm_id *ptl_cm_id_create(struct net *net, ptl_cm_handler handler,
                void *context)
{
    struct ptl_cm_id *id;

    if (!handler)
        return ERR_PTR(-EINVAL);

    id = kzalloc(sizeof(*id), GFP_KERNEL);
    if (!id)
        return ERR_PTR(-ENOMEM);

    id->object_type = PTL_CM_ID;

    id->net           = get_net(net);
    id->event_handler = handler;
    id->event_handler_context       = context;

    /*Wiring staff of the og rdma_cm_id for the bottom layer of the driver to work */
	kref_get(&bxiv3_dev_map.bxiv3_dev[next_nicia_num]->count);


    /* device is bound later in ptl_cm_id_resolve_addr(); local identity */
    id->bxiv3_dev = NULL;
    /* self_peer/remote_peer stay zeroed by kzalloc; both are filled in
    * ptl_cm_resolve_addr() before any send path reads them */
    id->ptl_qp        = NULL;

    id->cm_id_state = PTL_CM_IDLE;
    spin_lock_init(&id->state_lock);

    PTL_DEBUG("Created ptl_cm_id: %p", id);
    return id;
}

void ptl_cm_id_destroy(struct ptl_cm_id *id)
{
    unsigned long flags;

    if (!id)
        return;

    /* stop further operations; every state may transition to ERROR */
    ptl_cm_id_set_state(id, PTL_CM_ERROR);   //renamed: ptl_cm_set_state -> ptl_cm_id_set_state

    spin_lock_irqsave(&id->state_lock, flags);
    id->event_handler = NULL;
    spin_unlock_irqrestore(&id->state_lock, flags);

    kfree((void *)id->param.private_data);
    id->param.private_data = NULL;

    /* TODO: the create path takes kref_get(&bxiv3_dev->count) at bind time and
     * the matching kref_put belongs here, once a device release fn exists. */

    put_net(id->net);

    PTL_DEBUG("ptl_cm_id: destroyed id %p", id);   

    id->object_type = 0;

    kfree(id);
}

/* Address resolution — also binds the device */
int ptl_cm_id_resolve_addr(struct ptl_cm_id *id,
		     const struct sockaddr *src_addr,
		     const struct sockaddr *dst_addr,
		     unsigned long timeout_ms)
{
	const struct sockaddr_in *sin;
	const struct sockaddr_in6 *sin6;
	unsigned long flags;
	int rc;

	if (!id || !dst_addr)
		return -EINVAL;

	rc = ptl_cm_id_set_state(id, PTL_CM_ADDR_RESOLVING);
	if (rc)
		return rc;

	spin_lock_irqsave(&id->state_lock, flags);

	switch (dst_addr->sa_family) {

	case AF_INET:
		sin = (const struct sockaddr_in *)dst_addr;
		/* last byte of the IPv4 address; << 7 applied below.
		 * TODO: replace with the real BXI node discovery. */
		id->remote_peer.phys.nid = ((const u8 *)&sin->sin_addr.s_addr)[3];
        id->remote_peer.phys.pid = ntohs(sin->sin_port);
		break;

	case AF_INET6:
		sin6 = (const struct sockaddr_in6 *)dst_addr;
		id->remote_peer.phys.nid = sin6->sin6_addr.s6_addr[15];
        id->remote_peer.phys.pid = ntohs(sin6->sin6_port);
		break;

	default:
		spin_unlock_irqrestore(&id->state_lock, flags);
		ptl_cm_id_set_state(id, PTL_CM_ERROR);
		return -EAFNOSUPPORT;
	}

	 id->remote_peer.phys.nid = id->remote_peer.phys.nid << 7;

	/* keep the source address — connect serializes it into
	 *conn_open.src_addr for the target */
	memset(&id->src_addr, 0, sizeof(id->src_addr));
	if (src_addr)
		memcpy(&id->src_addr, src_addr,
		       min_t(size_t, sizeof(id->src_addr),
			     sizeof(struct sockaddr_storage)));

	/* bind the device — statically dev 0,
	 *("XXX TODO XXX: spread it dynamically") */
	if (!id->bxiv3_dev) {
		if (!bxiv3_dev_map.bxiv3_dev[0]) {
			spin_unlock_irqrestore(&id->state_lock, flags);
			ptl_cm_id_set_state(id, PTL_CM_ERROR);
			return -ENODEV;
		}
		id->bxiv3_dev = bxiv3_dev_map.bxiv3_dev[0];
		kref_get(&id->bxiv3_dev->count);
		 id->self_peer = id->bxiv3_dev->proc_id;
	}

	spin_unlock_irqrestore(&id->state_lock, flags);

	(void)timeout_ms;

	rc = ptl_cm_id_set_state(id, PTL_CM_ADDR_RESOLVED);
	if (rc)
		return rc;

	 PTL_DEBUG("Resolved target {nid:%u,pid:%u}, initiator {nid:%u,pid:%u}",
      id->remote_peer.phys.nid, id->remote_peer.phys.pid,
      id->self_peer.phys.nid, id->self_peer.phys.pid);


	if (id->event_handler) {
		struct ptl_cm_event ev = {
			.event  = PTL_CM_EVENT_ADDR_RESOLVED,
			.status = 0,
		};
		return id->event_handler(id, &ev);
	}

	return 0;
}
