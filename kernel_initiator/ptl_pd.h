#ifndef PTL_PD_H
#define PTL_PD_H
#include "ptl_object_types.h"
#include <rdma/ib_verbs.h>

struct ptl_pd {
	ptl_obj_type_e object_type;
	struct ib_pd fake_pd;
};

struct ptl_pd *ptl_pd_alloc(struct ib_device *dev, unsigned int flags);
#endif
