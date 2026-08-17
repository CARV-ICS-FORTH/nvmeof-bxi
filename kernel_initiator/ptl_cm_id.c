#include "ptl_cm_id.h"
#include <linux/slab.h>
#include <linux/err.h>
#include <net/net_namespace.h>
#include <linux/printk.h>

/* State machine */
int ptl_cm_set_state(struct ptl_cm_id *id, enum ptl_cm_state new_state)
{
	unsigned long flags;

	spin_lock_irqsave(&id->lock, flags);

	if (id->state == new_state)
		goto out;

	switch (id->state) {

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

	pr_debug("ptl_cm: state %d -> %d\n", id->state, new_state);
	id->state = new_state;

out:
	spin_unlock_irqrestore(&id->lock, flags);
	return 0;

invalid:
	pr_err("ptl_cm: invalid state transition %d -> %d\n",
	       id->state, new_state);
	spin_unlock_irqrestore(&id->lock, flags);
	return -EINVAL;
}

/* Lifecycle */


struct ptl_cm_id *ptl_create_id(struct net *net, ptl_cm_handler handler,
				void *context)
{
	struct ptl_cm_id *id;

	if (!handler)
		return ERR_PTR(-EINVAL);

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return ERR_PTR(-ENOMEM);

	id->net           = get_net(net);
	id->event_handler = handler;
	id->context       = context;

	/* device is bound later in ptl_resolve_addr(); local identity */
	id->bxiv3_dev = NULL;
	id->nid       = -1;
	id->pid       = -1;
	id->qp        = NULL;

	id->state = PTL_CM_IDLE;
	spin_lock_init(&id->lock);

	pr_debug("ptl_cm: created id %p\n", id);
	return id;
}

void ptl_destroy_id(struct ptl_cm_id *id)
{
	unsigned long flags;

	if (!id)
		return;

	/* stop further operations; every state may transition to ERROR */
	ptl_cm_set_state(id, PTL_CM_ERROR);

	spin_lock_irqsave(&id->lock, flags);
	id->event_handler = NULL;
	spin_unlock_irqrestore(&id->lock, flags);

	kfree((void *)id->param.private_data);
	id->param.private_data = NULL;

	/*
	 * TODO: create-path takes kref_get(&bxiv3_dev->count) at bind
	 * time; the matching kref_put belongs here. The old code never
	 * put the ref (device leak). Identify the device release
	 * function and add:
	 *     kref_put(&id->bxiv3_dev->count, <release_fn>);
	 */

	put_net(id->net);

	pr_debug("ptl_cm: destroyed id %p\n", id);
	kfree(id);
}
