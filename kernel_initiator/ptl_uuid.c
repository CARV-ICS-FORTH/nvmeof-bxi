/**
 * @file ptl_uuid.c
 * @brief Portals UUID management and field extraction/insertion utilities
 *
 * This module provides functions to pack and unpack various fields into a 64-bit UUID.
 * The UUID is structured as follows:
 *
 * Layout (little-endian bit numbering):
 *   Bits 0-15   (Bytes 0-1):   Initiator Queue Pair Number (16-bit)
 *   Bits 16-31  (Bytes 2-3):   Target Queue Pair Number (16-bit)
 *   Bits 32-47  (Bytes 4-5):   Completion Queue ID (16-bit)
 *   Bits 48-63  (Bytes 6-7):   Operation Type (16-bit)
 *
 * All fields are 16-bit unsigned values (0x0000 - 0xFFFF).
 */

#include "ptl_uuid.h"
#include "ptl_object_types.h"

/* Kernel space includes */
#include <linux/types.h>
#include <linux/spinlock.h>

/**
 * Bit masks for clearing specific field ranges in the UUID.
 * Each mask has 0s in the bits to be cleared and 1s elsewhere.
 */
#define PTL_UUID_INITIATOR_QP_NUM_MASK 0xFFFFFFFFFFFF0000ULL  /**< Clear bits 0-15 */
#define PTL_UUID_TARGET_QP_NUM_MASK    0xFFFFFFFF0000FFFFULL  /**< Clear bits 16-31 */
#define PTL_UUID_CQ_MASK               0xFFFF0000FFFFFFFFULL  /**< Clear bits 32-47 */
#define PTL_UUID_OP_TYPE_MASK          0x0000FFFFFFFFFFFFULL  /**< Clear bits 48-63 */
#define PTL_UUID_MAX_QP_NUM            (1<<16)                /**< Maximum 16-bit value */


u64 ptl_uuid_set_op_type(u64 uuid, int op_type)
{
	if (op_type < 0 || op_type > 0xFFFF) {
		PTL_FATAL("op_type too large");
	}
	u64 op = op_type;
	uuid &= PTL_UUID_OP_TYPE_MASK;  /* Clear bits 48-63 */
	uuid |= (op << 48);              /* Set bits 48-63 */
	return uuid;
}


int ptl_uuid_get_op_type(u64 uuid)
{
	return (int)((uuid >> 48) & 0xFFFF);  /* Extract bits 48-63 */
}


u64 ptl_uuid_set_target_qp_num(u64 uuid, int qp_num)
{
	if (qp_num < 0 || qp_num > 0xFFFF) {
		PTL_FATAL("qp number too large");
	}
	u64 qp = qp_num;
	uuid &= PTL_UUID_TARGET_QP_NUM_MASK;  /* Clear bits 16-31 */
	uuid |= (qp << 16);                    /* Set bits 16-31 */
	return uuid;
}


int ptl_uuid_get_target_qp_num(u64 uuid)
{
	return (int)((uuid >> 16) & 0xFFFF);  /* Extract bits 16-31 */
}


u64 ptl_uuid_set_initiator_qp_num(u64 uuid, int qp_num)
{
	PTL_DEBUG("UUID was %llu and qp num: %d", uuid, qp_num);
	if (qp_num < 0 || qp_num > 0xFFFF) {
		PTL_FATAL("qp number too large");
	}

	u64 qp = qp_num;
	uuid &= PTL_UUID_INITIATOR_QP_NUM_MASK;  /* Clear bits 0-15 */
	uuid |= qp;                    /* Set bits 0-15 */
	PTL_DEBUG("UUID now is %llu", uuid);
	return uuid;
}


int ptl_uuid_get_initiator_qp_num(u64 uuid)
{
	int qp_num = (int)(uuid & 0xFFFF);  /* Extract bits 0-15 */
	return qp_num;
}


int ptl_uuid_get_cq_num(u64 uuid)
{
	return (int)((uuid >> 32) & 0xFFFF);  /* Extract bits 32-47 */
}


u64 ptl_uuid_set_cq_num(u64 uuid, int cq_num)
{
	if (cq_num < 0 || cq_num > 0xFFFF) {
		PTL_FATAL("cq number too large");
	}
	uuid = uuid & PTL_UUID_CQ_MASK;  /* Clear bits 32-47 */
	u64 cq = cq_num;
	uuid |= (cq << 32);               /* Set bits 32-47 */
	return uuid;
}

