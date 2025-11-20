#ifndef PTL_CM_ID_H
#define PTL_CM_ID_H
// #include "ptl_connection.h"
// #include "ptl_context.h"
// #include "ptl_cq.h"
// #include "ptl_log.h"
// #include "ptl_pd.h"
// #include "ptl_qp.h"
#include "ptl_object_types.h"
#include <linux/types.h>
#include <rdma/rdma_cm.h>
typedef enum {
  PTL_CM_DISCONNECTING = 0,
  PTL_CM_DISCONNECTED,
  PTL_CM_CONNECTING,
  PTL_CM_CONNECTED,
  PTL_CM_UNCONNECTED,
  PTL_CM_GUARD
} ptl_cm_id_e;

struct ptl_cm_id {
  ptl_obj_type_e object_type;
  struct rdma_cm_id fake_cm_id;
  struct net *net;
  rdma_cm_event_handler event_handler;
  void *event_handler_context;
  int nid;
  int pid;
  struct ptl_bxiv3_device *bxiv3_dev;
  struct ptl_qp *ptl_qp;
  //  struct ptl_conn_msg conn_msg;
  // struct rdma_cm_ptl_event_channel *ptl_channel;

  /*As in verbs, each ptl_cm_id associates with a single ptl_pd */
  // struct ptl_pd *ptl_pd;
  // struct ptl_qp *ptl_qp;
  // struct ptl_context *ptl_context;
  // u64 uuid;
  // /*Where the remote peer has MEs for recv*/
  // u64 recv_match_bits;
  // /*Where the remote peer has MEs for RMA operations*/
  // u64 rma_match_bits;
  // /**
  // * CQ id of the remote peer where it has subscribed for events.
  // * Its purpose is to encoded in the match bits in the PtlPut operations
  // */
  // int remote_cq_id;
  // /*Which are MY match bits for recv operations*/
  // u64 my_match_bits;
  ptl_cm_id_e cm_id_state;
  // int ptl_qp_num;
  // struct ptl_cq *cq;
  // struct rdma_conn_param conn_param;
  // //needed for connection setup
  // const void *fake_data;
};

struct ptl_cm_id *ptl_cm_id_create(struct net *net,
                                   rdma_cm_event_handler event_handler,
                                   void *context, enum rdma_ucm_port_space ps,
                                   enum ib_qp_type qp_type, const char *caller);

int ptl_cm_id_resolve_addr(struct ptl_cm_id *ptl_cm_id,
                           struct sockaddr *src_addr,
                           const struct sockaddr *dst_addr,
                           unsigned long timeout_ms);

// void ptl_cm_id_set_recv_cq(struct ptl_cm_id *ptl_id, struct ptl_cq *recv_cq);

// void ptl_cm_id_set_send_cq(struct ptl_cm_id *ptl_id, struct ptl_cq *send_cq);

// struct ptl_cm_id *ptl_cm_id_create(struct rdma_cm_ptl_event_channel *
// event_channel, void *context);

// static inline struct ptl_cm_id *ptl_cm_id_get(struct rdma_cm_id *id)
// {
//      struct ptl_cm_id *ptl_id =
//              container_of(id, struct ptl_cm_id, fake_cm_id);
//      if (PTL_CM_ID != ptl_id->object_type) {
//              SPDK_PTL_FATAL("Corrupted PTL ID");
//      }
//      return ptl_id;
// }
// struct rdma_cm_event *ptl_cm_id_create_event(struct ptl_cm_id *ptl_id, struct
// ptl_cm_id *listen_id,
//              enum rdma_cm_event_type event_type);

// void ptl_cm_id_add_event(struct ptl_cm_id *ptl_id,
//                       struct rdma_cm_event *event);

// // static inline struct sockaddr *
// // rdma_cm_ptl_id_get_src_addr(struct ptl_cm_id *ptl_id)
// // {
// //   return &ptl_id->src_addr;
// // }

// void ptl_cm_id_set_fake_data(struct ptl_cm_id *ptl_id, const void
// *fake_data);

// static inline void ptl_cm_id_set_ptl_qp(struct ptl_cm_id *ptl_id, struct
// ptl_qp *ptl_qp)
// {
//      if (ptl_id->ptl_qp) {
//              PTL_FATAL("PTL QP already set");
//     return;
//      }
//      ptl_id->ptl_qp = ptl_qp;
//      ptl_id->fake_cm_id.qp = ptl_qp_get_ibv_qp(ptl_qp);
// }

// static inline void ptl_cm_id_set_ptl_pd(struct ptl_cm_id *ptl_id, struct
// ptl_pd *ptl_pd)
// {
//      if (ptl_id->ptl_pd) {
//              PTL_FATAL("PTL PD already set");
//      }
//      ptl_id->ptl_pd = ptl_pd;
//      ptl_id->fake_cm_id.pd = ptl_pd_get_ibv_pd(ptl_pd);
//      /*set also context as in the verbs case*/
//      ptl_id->ptl_context = ptl_pd_get_cnxt(ptl_pd);
//      ptl_id->fake_cm_id.context =
//      ptl_cnxt_get_ibv_context(ptl_pd_get_cnxt(ptl_pd));
// }
#endif
