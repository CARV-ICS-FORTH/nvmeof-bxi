#ifndef PTL_RECV_OP_H
#define PTL_RECV_OP_H
#include "portals4.h"
#include "ptl_object_types.h"
struct ptl_recv_op {
	ptl_obj_type_e object_type;
	ptl_le_t le;
	ptl_handle_le_t leh;
	struct ib_cqe *wr_cqe;
	struct ptl_qp *ptl_qp;
	u64 wr_id;
};

struct ptl_send_op {
	ptl_obj_type_e object_type;
	struct ib_cqe *wr_cqe;
	struct ptl_qp *ptl_qp;
	u64 wr_id;
};
#endif
