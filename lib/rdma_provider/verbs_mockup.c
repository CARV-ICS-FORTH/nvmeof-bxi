#include "ptl_context.h"
#include "ptl_cq.h"
#include "ptl_log.h"
#include "ptl_pd.h"
#include <infiniband/verbs.h>

#define DEVICE_NAME "bxi"


#define ibv_reg_mr_iova2(pd, addr, length, iova, access) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_reg_mr_iova2"); (struct ibv_mr *)NULL; })

// // Now mock the inline wrapper
// #define __ibv_reg_mr(pd, addr, length, access, is_access_const) \
//     ({ SPDK_PTL_FATAL("Sorry unimplemented: __ibv_reg_mr"); (struct ibv_mr *)NULL; })

// #undef ibv_reg_mr_iova
// #define ibv_reg_mr_iova(pd, addr, length, iova, access) \
//     ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_reg_mr_iova"); (struct ibv_mr *)NULL; })

// #undef ibv_reg_mr_iova2
// #define ibv_reg_mr_iova2(pd, addr, length, iova, access) \
//     ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_reg_mr_iova2"); (struct ibv_mr *)NULL; })

#undef ibv_query_port
#undef ___ibv_query_port

// Mock the base function that takes _compat_ibv_port_attr
#define ibv_query_port(context, port_num, port_attr) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_query_port"); -1; })

// Mock the inline wrapper that takes ibv_port_attr
#define ___ibv_query_port(context, port_num, port_attr) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ___ibv_query_port"); -1; })


#undef ibv_post_srq_recv
#define ibv_post_srq_recv(srq, recv_wr, bad_recv_wr) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_post_srq_recv"); -1; })

#undef ibv_post_send
#define ibv_post_send(qp, wr, bad_wr) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_post_send"); -1; })

#undef ibv_post_recv
#define ibv_post_recv(qp, wr, bad_wr) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_post_recv"); -1; })

#undef ibv_alloc_mw
#define ibv_alloc_mw(pd, type) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_alloc_mw"); (struct ibv_mw *)NULL; })

#undef ibv_dealloc_mw
#define ibv_dealloc_mw(mw) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_dealloc_mw"); -1; })

#undef ibv_bind_mw
#define ibv_bind_mw(qp, mw, mw_bind) \
    ({ SPDK_PTL_FATAL("Sorry unimplemented: ibv_bind_mw"); -1; })

/* Device and context functions */
struct ibv_device **ibv_get_device_list(int *num_devices)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

void ibv_free_device_list(struct ibv_device **list)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}

const char *ibv_get_device_name(struct ibv_device *device)
{
	SPDK_PTL_DEBUG("FAKE IT");
	return DEVICE_NAME;
}

int ibv_get_device_index(struct ibv_device *device)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

__be64 ibv_get_device_guid(struct ibv_device *device)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return 0;
}

struct ibv_context *ibv_open_device(struct ibv_device *device)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

int ibv_close_device(struct ibv_context *context)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

struct ibv_context *ibv_import_device(int cmd_fd)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

/* Protection domain functions */
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context)
{
	struct ptl_pd *ptl_pd;
	struct ptl_context *ptl_context = ptl_cnxt_get_from_ibcnxt(context);
	SPDK_PTL_DEBUG("IBVPTL: OK trapped ibv_alloc_pd allocating ptl_pd");
	if (ptl_context->ptl_pd) {
		SPDK_PTL_DEBUG("PTL_PD Already set, go on");
		return ptl_pd_get_ibv_pd(ptl_context->ptl_pd);
	}
	ptl_pd = ptl_pd_create(ptl_context, NULL);
	ptl_context->ptl_pd = ptl_pd;
	return ptl_pd_get_ibv_pd(ptl_pd);
}


int ibv_dealloc_pd(struct ibv_pd *pd)
{
	SPDK_PTL_DEBUG("CAUTION Do nothing pd is singleton staff in Portals only for compatibility XXX TODO XXX");
	return 0;
}

struct ibv_pd *ibv_import_pd(struct ibv_context *context, uint32_t pd_handle)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

void ibv_unimport_pd(struct ibv_pd *pd)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}


struct ibv_mr *ibv_reg_dmabuf_mr(struct ibv_pd *pd, uint64_t offset, size_t length, uint64_t iova,
				 int fd, int access)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

int ibv_rereg_mr(struct ibv_mr *mr, int flags, struct ibv_pd *pd, void *addr, size_t length,
		 int access)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_dereg_mr(struct ibv_mr *mr)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

struct ibv_mr *ibv_import_mr(struct ibv_pd *pd, uint32_t mr_handle)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

void ibv_unimport_mr(struct ibv_mr *mr)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}

/* Device memory functions */
struct ibv_dm *ibv_import_dm(struct ibv_context *context, uint32_t dm_handle)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

void ibv_unimport_dm(struct ibv_dm *dm)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}

/* Query functions */
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


int ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int _ibv_query_gid_ex(struct ibv_context *context, uint32_t port_num, uint32_t gid_index,
		      struct ibv_gid_entry *entry, uint32_t flags, size_t entry_size)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

ssize_t _ibv_query_gid_table(struct ibv_context *context, struct ibv_gid_entry *entries,
			     size_t max_entries, uint32_t flags, size_t entry_size)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_query_pkey(struct ibv_context *context, uint8_t port_num, int index, __be16 *pkey)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_get_pkey_index(struct ibv_context *context, uint8_t port_num, __be16 pkey)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

/* Async event functions */
int ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

void ibv_ack_async_event(struct ibv_async_event *event)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}

/* Completion channel functions */
struct ibv_comp_channel *ibv_create_comp_channel(struct ibv_context *context)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

int ibv_destroy_comp_channel(struct ibv_comp_channel *channel)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

/* Completion queue functions */
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,
			     void *cq_context, struct ibv_comp_channel *channel,
			     int comp_vector)
{

	SPDK_PTL_DEBUG("IBVPTL: Ok trapped ibv_create_cq time to create the event queue in portals");
	struct ptl_cq *ptl_cq = ptl_cq_create(cq_context);
	SPDK_PTL_DEBUG("PtlCQ: Ok set up event queue for PORTALS :-) CQ id = %d", ptl_cq->cq_id);
	return ptl_cq_get_ibv_cq(ptl_cq);
}


int ibv_resize_cq(struct ibv_cq *cq, int cqe)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_destroy_cq(struct ibv_cq *cq)
{
	struct ptl_cq *ptl_cq = ptl_cq_get_from_ibv_cq(cq);
	SPDK_PTL_DEBUG("PtlCQ: destroy CAUTION, ignore this XXX TODO XXX");
	ptl_cq->is_in_use = false;
	return 0;
}



int ibv_get_cq_event(struct ibv_comp_channel *channel, struct ibv_cq **cq, void **cq_context)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

void ibv_ack_cq_events(struct ibv_cq *cq, unsigned int nevents)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}

/* Shared receive queue functions */
struct ibv_srq *ibv_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *srq_init_attr)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

int ibv_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr, int srq_attr_mask)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_destroy_srq(struct ibv_srq *srq)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}


/* Queue pair functions */
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask,
		 struct ibv_qp_init_attr *init_attr)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_destroy_qp(struct ibv_qp *qp)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}



struct ibv_qp_ex *ibv_qp_to_qp_ex(struct ibv_qp *qp)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

/* Address handle functions */
struct ibv_ah *ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

int ibv_destroy_ah(struct ibv_ah *ah)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

/* Multicast functions */
int ibv_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

/* Rate conversion functions */
int ibv_rate_to_mult(enum ibv_rate rate)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

enum ibv_rate mult_to_ibv_rate(int mult)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return IBV_RATE_MAX;
}

int ibv_rate_to_mbps(enum ibv_rate rate)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

enum ibv_rate mbps_to_ibv_rate(int mbps)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return IBV_RATE_MAX;
}

/* Status string functions */
const char *ibv_wc_status_str(enum ibv_wc_status status)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

const char *ibv_wr_opcode_str(enum ibv_wr_opcode opcode)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return NULL;
}

/* Fork support functions */
int ibv_fork_init(void)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}


/* Extended functions that might be called through function pointers */
int ibv_poll_cq_v1(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

int ibv_req_notify_cq_v1(struct ibv_cq *cq, int solicited_only)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
	return -1;
}

/* Additional functions that might be needed */
void ibv_static_providers(void *unused, ...)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}

const char *ibv_event_type_str(enum ibv_event_type event_type)
{
	SPDK_PTL_FATAL("Sorry unimplemented");
}

#undef ibv_reg_mr
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
			  int access)
{

	SPDK_PTL_FATAL("Sorry unimplemented");
}

#undef ibv_reg_mr_iova2
struct ibv_mr *ibv_reg_mr_iova2(struct ibv_pd *pd, void *addr, size_t length,
				uint64_t iova, unsigned int access)
{
	SPDK_PTL_FATAL("Sorry unimplemented");

}
