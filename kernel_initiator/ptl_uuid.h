/**
 * @file ptl_uuid.h
 * @brief Portals UUID management and field extraction/insertion utilities - Header
 *
 * This header provides function declarations for packing and unpacking various
 * fields into a 64-bit UUID.
 *
 * Layout (little-endian bit numbering):
 *   Bits 0-15   (Bytes 0-1):   Initiator Queue Pair Number (16-bit)
 *   Bits 16-31  (Bytes 2-3):   Target Queue Pair Number (16-bit)
 *   Bits 32-47  (Bytes 4-5):   Completion Queue ID (16-bit)
 *   Bits 48-63  (Bytes 6-7):   Operation Type (16-bit)
 *
 * All fields are 16-bit unsigned values (0x0000 - 0xFFFF).
 */

#ifndef PTL_UUID_H
#define PTL_UUID_H

#include <linux/types.h>

/**
 * @brief Set the operation type field in the UUID
 * @param uuid The UUID to modify
 * @param op_type The operation type value (0-0xFFFF)
 * @return Modified UUID with operation type set
 */
uint64_t ptl_uuid_set_op_type(uint64_t uuid, int op_type);

/**
 * @brief Get the operation type field from the UUID
 * @param uuid The UUID to extract from
 * @return The operation type value
 */
int ptl_uuid_get_op_type(uint64_t uuid);

/**
 * @brief Set the target queue pair number in the UUID
 * @param uuid The UUID to modify
 * @param qp_num The target QP number (0-0xFFFF)
 * @return Modified UUID with target QP number set
 */
uint64_t ptl_uuid_set_target_qp_num(uint64_t uuid, int qp_num);

/**
 * @brief Get the target queue pair number from the UUID
 * @param uuid The UUID to extract from
 * @return The target QP number
 */
int ptl_uuid_get_target_qp_num(uint64_t uuid);

/**
 * @brief Set the initiator queue pair number in the UUID
 * @param uuid The UUID to modify
 * @param qp_num The initiator QP number (0-0xFFFF)
 * @return Modified UUID with initiator QP number set
 */
uint64_t ptl_uuid_set_initiator_qp_num(uint64_t uuid, int qp_num);

/**
 * @brief Get the initiator queue pair number from the UUID
 * @param uuid The UUID to extract from
 * @return The initiator QP number
 */
int ptl_uuid_get_initiator_qp_num(uint64_t uuid);

/**
 * @brief Get the completion queue number from the UUID
 * @param uuid The UUID to extract from
 * @return The CQ number
 */
int ptl_uuid_get_cq_num(uint64_t uuid);

/**
 * @brief Set the completion queue number in the UUID
 * @param uuid The UUID to modify
 * @param cq_num The CQ number (0-0xFFFF)
 * @return Modified UUID with CQ number set
 */
uint64_t ptl_uuid_set_cq_num(uint64_t uuid, int cq_num);

#endif /* PTL_UUID_H */
