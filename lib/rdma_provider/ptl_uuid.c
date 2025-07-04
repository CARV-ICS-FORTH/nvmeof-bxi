#include "ptl_uuid.h"
#include "ptl_log.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>

/*We keep target's qp num in the last two least significant (0,1) bytes of uuid*/
#define PTL_UUID_TARGET_QP_NUM_MASK    0xFFFFFFFFFFFF0000UL
/*We keep initiator's qp num in bytes 2,3*/
#define PTL_UUID_INITIATOR_QP_NUM_MASK 0xFFFFFFFF0000FFFFUL
/*We encode the completion queue id in bytes 4,5*/
#define PTL_UUID_CQ_MASK                    0xFFFF0000FFFFFFFFUL
#define PTL_UUID_INITIATOR_MAX_QP_NUM  (1<<15)
/* We use the 2 most significant bytes for match bits */
#define PTL_UUID_IGNORE_MASK          0x0000FFFFFFFFFFFFUL

uint64_t ptl_uuid_set_match_list(uint64_t uuid, uint64_t match_list)
{
	match_list &= ~PTL_UUID_IGNORE_MASK;
	uuid &= PTL_UUID_IGNORE_MASK;
	uuid |= match_list;
	// SPDK_PTL_DEBUG("MATCH_BITS in hex: 0x%" PRIx64, uuid);
	return uuid;
}

// uint64_t ptl_uuid_set_op_type(uint64_t uuid, ptl_uuid_op_type_e op)
// {
// 	if (op == PTL_SEND_RECV) {
// 		return (uuid & PTL_UUID_IGNORE_MASK) | PTL_UUID_SEND_RECV_MASK;
// 	}

// 	if (op == PTL_RMA) {
// 		return (uuid & PTL_UUID_IGNORE_MASK) | PTL_UUID_RMA_MASK;
// 	}

// 	SPDK_PTL_FATAL("Unknown type of operation");
// 	return 0;
// }

uint64_t ptl_uuid_set_target_qp_num(uint64_t uuid, int qp_num)
{
	if (qp_num < 0 || qp_num > PTL_UUID_INITIATOR_MAX_QP_NUM) {
		SPDK_PTL_FATAL("qp number too large");
	}
	uint64_t qp = qp_num;
	// SPDK_PTL_DEBUG("CP server: Setting target's qp num to %d", qp_num);
	/*clear first*/
	uuid &= PTL_UUID_TARGET_QP_NUM_MASK;
	uuid |= qp;
	return uuid;
}

int ptl_uuid_get_target_qp_num(uint64_t uuid)
{
	return (int32_t)(uuid & ~PTL_UUID_TARGET_QP_NUM_MASK);
	// SPDK_PTL_DEBUG("CP server: Getting target's qp num: %d", qp_num);
}

uint64_t ptl_uuid_set_initiator_qp_num(uint64_t uuid, int qp_num)
{

	SPDK_PTL_DEBUG("UUID was %lu and qp num: %d", uuid, qp_num);
	if (qp_num < 0 || qp_num > PTL_UUID_INITIATOR_MAX_QP_NUM) {
		SPDK_PTL_FATAL("qp number too large");
	}

	uint64_t qp = qp_num;
	uuid &= PTL_UUID_INITIATOR_QP_NUM_MASK;
	uuid |= (qp << 16UL);
	SPDK_PTL_DEBUG("UUID now is %lu", uuid);
	return uuid;
}

int ptl_uuid_get_initiator_qp_num(uint64_t uuid)
{
	// int qp_num = (uuid >> 32) & 0x00FFFFFF;
	int qp_num = (uuid & ~PTL_UUID_INITIATOR_QP_NUM_MASK) >> 16UL;
	SPDK_PTL_DEBUG("CP server: Getting initiator's qp num uuid = %lu qp num = %d", uuid, qp_num);
	return qp_num;
}

int ptl_uuid_get_cq_num(uint64_t uuid)
{
	return (int32_t)((uuid & ~PTL_UUID_CQ_MASK) >> 32);
}

uint64_t ptl_uuid_set_cq_num(uint64_t uuid, int cq_num)
{
	if (cq_num < 0 || cq_num > PTL_UUID_INITIATOR_MAX_QP_NUM) {
		SPDK_PTL_FATAL("cq number too large");
	}
	uuid = uuid & PTL_UUID_CQ_MASK;
	uint64_t cq = cq_num;
	uuid |= (cq << 32);
	return uuid;
}

uint64_t ptl_uuid_get_next_match_bit(void)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	static uint64_t global_match_bits = PTL_UUID_RMA_MASK;
	pthread_mutex_lock(&lock);
	uint64_t my_match_bits = (global_match_bits >> 48) + 1;
	my_match_bits <<= 48;
	global_match_bits = my_match_bits;
	SPDK_PTL_DEBUG("MATCH_BITS: Just gave match bit: %lu", my_match_bits);
	pthread_mutex_unlock(&lock);
	return my_match_bits;
}


