#ifndef PTL_QP_H
#define PTL_QP_H
#include "ptl_object_types.h"
#include "rdma/ib_verbs.h"
struct ptl_cm_id;
struct ptl_qp {
  ptl_obj_type_e object_type;
  struct ptl_cm_id *ptl_id;
  struct ptl_pd *ptl_pd;
  struct ptl_cq *send_cq;
  struct ptl_cq *recv_cq;
  int qpn;
};

struct ptl_qp *ptl_qp_create(struct ptl_cm_id *ptl_id, struct ptl_pd *ptl_pd,
                             struct ib_qp_init_attr *attr);
#endif
