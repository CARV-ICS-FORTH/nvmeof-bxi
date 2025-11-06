// SPDX-License-Identifier: GPL-2.0
/*
 * Fake RDMA-CM "portals" shim — no-op / fail-fast mock for NVMe-RDMA experimentation
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/ratelimit.h>
#include <linux/printk.h>
#include <linux/err.h>
#include <rdma/rdma_cm.h>
#include <rdma/rdma_user_cm.h>


#define rdma_cm_portals_create_id(net, event_handler, context, ps, qp_type)               \
	__rdma_cm_portals_create_kernel_id(net, event_handler, context, ps, qp_type,      \
				KBUILD_MODNAME)

struct rdma_cm_id *
__rdma_cm_portals_create_kernel_id(struct net *net, rdma_cm_event_handler event_handler,
			void *context, enum rdma_ucm_port_space ps,
			enum ib_qp_type qp_type, const char *caller);


int rdma_cm_portals_destroy_id(struct rdma_cm_id *id);

int rdma_cm_portals_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr, const struct sockaddr *dst_addr, unsigned long timeout_ms);

int rdma_cm_portals_resolve_route(struct rdma_cm_id *id, unsigned long timeout_ms);

int rdma_cm_portals_connect_locked(struct rdma_cm_id *id, struct rdma_conn_param *param);

int rdma_cm_portals_disconnect(struct rdma_cm_id *id);

int rdma_cm_portals_create_qp(struct rdma_cm_id *id, struct ib_pd *pd, struct ib_qp_init_attr *attr);

int rdma_cm_portals_destroy_qp(struct rdma_cm_id *id);

/* --- Helpers that return strings/data ---- */

const char *rdma_cm_portals_event_msg(int ev);

const void *rdma_cm_portals_reject_msg(struct rdma_cm_id *id, int status);

const void *rdma_cm_portals_consumer_reject_data(struct rdma_cm_id *id, struct rdma_cm_event *ev, u8 *data_len);

int rdma_cm_portals_set_service_type(struct rdma_cm_id *id, u8 tos);


int rdma_cm_portals_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param);


