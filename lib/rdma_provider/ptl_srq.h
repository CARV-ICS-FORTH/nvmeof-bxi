#ifndef PTL_SRQ_H
#define PTL_SRQ_H
#include "ptl_log.h"
#include "ptl_object_types.h"
#include "ptl_pd.h"
#include <infiniband/verbs.h>
struct ptl_srq {
	enum ptl_obj_type obj_type;
	struct ibv_srq fake_srq;
	int pte_number;
};


struct ptl_srq *ptl_srq_create(struct ptl_pd *pd,
			       struct ibv_srq_init_attr *srq_init_attr);
struct ptl_srq *ptl_srq_get_from_ibv_srq(struct ibv_srq *ibv_srq);

#endif
