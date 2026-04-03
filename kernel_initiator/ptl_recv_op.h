#ifndef PTL_RECV_OP_H
#define PTL_RECV_OP_H
#include "portals4.h"
#include "ptl_object_types.h"
#include <ib_verbs.h>
struct ptl_recv_op {
	ptl_obj_type_e object_type;
	ptl_le_t le;
	ptl_handle_le_t leh;
	struct ib_cqe *wr_cqe;
	struct ptl_qp *ptl_qp;
	u64 wr_id;
	struct ib_wc late_wc;
	/**
	 * Number of parts that must have been received for this nvme_cpl to be
	 * complete. We introduce this mechanism due to the absence of FIFO
	 * delivery (compared to IB/verbs) in Portasl4
	 **/
	u16 parts_num_received;
	/**
	 * Total parts that the nvme_cpl consists of. Initially, when we do not know
	 * we set it to 0.
	 */
	u16 total_parts;
	bool is_set;
	bool late_wc_valid;
};


struct ptl_send_op {
	ptl_obj_type_e object_type;
	struct ib_cqe *wr_cqe;
	struct ptl_qp *ptl_qp;
	u64 wr_id;
};
#endif
