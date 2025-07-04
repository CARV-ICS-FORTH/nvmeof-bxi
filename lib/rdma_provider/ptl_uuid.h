#ifndef PTL_UUID_H
#define PTL_UUID_H
#include "ptl_log.h"
#include <stdint.h>

/* In a nutshell 1 is reserved for target srq and 2 for RMA operations to the initiator*/
#define PTL_UUID_TARGET_SRQ_MATCH_BITS 0x0001000000000000UL
#define PTL_UUID_RMA_MASK              0x0002000000000000UL
#define PTL_UUID_TARGET_COMPLETION_QUEUE_ID  0

#define PTL_UUID_SEND_RECV_MASK 0x4000000000000000UL

/* We use the 2 most significant bytes for match bits */
#define PTL_UUID_IGNORE_MASK          0x0000FFFFFFFFFFFFUL
// typedef enum {
// 	PTL_SEND_RECV = 19,
// 	PTL_RMA
// } ptl_uuid_op_type_e;


uint64_t ptl_uuid_get_next_match_bit(void);

uint64_t ptl_uuid_set_match_list(uint64_t uuid, uint64_t match_list);

// uint64_t ptl_uuid_set_op_type(uint64_t uuid, ptl_uuid_op_type_e op);

uint64_t ptl_uuid_set_target_qp_num(uint64_t uuid, int qp_num);

int ptl_uuid_get_target_qp_num(uint64_t uuid);

uint64_t ptl_uuid_set_initiator_qp_num(uint64_t uuid, int qp_num);

int ptl_uuid_get_initiator_qp_num(uint64_t uuid);

int ptl_uuid_get_cq_num(uint64_t uuid);

uint64_t ptl_uuid_set_cq_num(uint64_t uuid, int cq_num);
#endif

