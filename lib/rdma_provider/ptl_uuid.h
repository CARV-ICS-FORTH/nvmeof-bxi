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


/**
 * ptl_uuid_set_op_type - Set the operation type field in a UUID
 * @uuid: The UUID to modify
 * @op_type: The operation type value (0x0000 - 0xFFFF)
 *
 * Sets the operation type field (bits 48-63) in the UUID.
 * The operation type identifies the type of operation being performed.
 *
 * Return: The modified UUID with the operation type field set
 */
uint64_t ptl_uuid_set_op_type(uint64_t uuid, int op_type);


/**
 * ptl_uuid_get_op_type - Extract the operation type field from a UUID
 * @uuid: The UUID to extract from
 *
 * Extracts the operation type field (bits 48-63) from the UUID.
 *
 * Return: The operation type value (0x0000 - 0xFFFF)
 */
int ptl_uuid_get_op_type(uint64_t uuid);


/**
 * ptl_uuid_get_next_match_bit - Generate the next unique match bits value
 *
 * Generates and returns the next unique match bits value in a thread-safe manner.
 * Match bits are used to identify and match operations in the Portals protocol.
 * This function maintains a global counter that is incremented on each call.
 *
 * The match bits are stored in bits 48-63 of the UUID, allowing for up to
 * 65536 unique match bit values before wrapping around.
 *
 * Thread-safe: Uses a mutex to protect the global match bits counter.
 *
 * Return: The next unique match bits value (bits 48-63 set, lower bits clear)
 */
uint64_t ptl_uuid_get_next_match_bit(void);


/**
 * ptl_uuid_set_target_qp_num - Set the target queue pair number in a UUID
 * @uuid: The UUID to modify
 * @qp_num: The target queue pair number (0x0000 - 0xFFFF)
 *
 * Sets the target queue pair number field (bits 16-31) in the UUID.
 * The target QP number identifies the queue pair on the target side.
 *
 * Return: The modified UUID with the target QP number field set
 */
uint64_t ptl_uuid_set_target_qp_num(uint64_t uuid, int qp_num);

/**
 * ptl_uuid_get_target_qp_num - Extract the target queue pair number from a UUID
 * @uuid: The UUID to extract from
 *
 * Extracts the target queue pair number field (bits 16-31) from the UUID.
 *
 * Return: The target queue pair number (0x0000 - 0xFFFF)
 */
int ptl_uuid_get_target_qp_num(uint64_t uuid);

/**
 * ptl_uuid_set_initiator_qp_num - Set the initiator queue pair number in a UUID
 * @uuid: The UUID to modify
 * @qp_num: The initiator queue pair number (0x0000 - 0xFFFF)
 *
 * Sets the initiator queue pair number field (bits 0-15) in the UUID.
 * The initiator QP number identifies the queue pair on the initiator side.
 *
 * Return: The modified UUID with the initiator QP number field set
 */
uint64_t ptl_uuid_set_initiator_qp_num(uint64_t uuid, int qp_num);

/**
 * ptl_uuid_get_initiator_qp_num - Extract the initiator queue pair number from a UUID
 * @uuid: The UUID to extract from
 *
 * Extracts the initiator queue pair number field (bits 0-15) from the UUID.
 *
 * Return: The initiator queue pair number (0x0000 - 0xFFFF)
 */
int ptl_uuid_get_initiator_qp_num(uint64_t uuid);

/**
 * ptl_uuid_get_cq_num - Extract the completion queue ID from a UUID
 * @uuid: The UUID to extract from
 *
 * Extracts the completion queue ID field (bits 32-47) from the UUID.
 *
 * Return: The completion queue ID (0x0000 - 0xFFFF)
 */
int ptl_uuid_get_cq_num(uint64_t uuid);

/**
 * ptl_uuid_set_cq_num - Set the completion queue ID in a UUID
 * @uuid: The UUID to modify
 * @cq_num: The completion queue ID (0x0000 - 0xFFFF)
 *
 * Sets the completion queue ID field (bits 32-47) in the UUID.
 * The CQ ID identifies the completion queue associated with this operation.
 *
 * Return: The modified UUID with the completion queue ID field set
 */
uint64_t ptl_uuid_set_cq_num(uint64_t uuid, int cq_num);
#endif

