#include "ptl_srq.h"
#include "ptl_context.h"
#include "ptl_log.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"

static int ptl_post_srq_recv(struct ibv_srq *srq, struct ibv_recv_wr *recv_wr,
			     struct ibv_recv_wr **bad_recv_wr)
{
	SPDK_PTL_FATAL("Trapped it this post_srq_recv but it is not implemented");
	return 0;
}

struct ptl_srq *ptl_create_srq(struct ptl_pd *ptl_pd,
			       struct ibv_srq_init_attr *srq_init_attr)
{
	struct ptl_srq * ptl_srq;
	ptl_srq = calloc(1UL, sizeof(*ptl_srq));
	ptl_srq->obj_type = PTL_SRQ;
	struct ptl_context * ptl_cnxt = ptl_pd_get_cnxt(ptl_pd);
	ptl_srq->fake_srq.context = ptl_cnxt_get_ibv_context(ptl_cnxt);
	ptl_srq->fake_srq.context->ops.post_srq_recv = ptl_post_srq_recv;
	SPDK_PTL_DEBUG("Created a PTL_SRQ...");
	return ptl_srq;
}
