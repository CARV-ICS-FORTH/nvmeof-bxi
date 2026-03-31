#include "ptl_uuid.h"
#include "ptl_connection.h"
#include "ptl_object_types.h"
/* User-space equivalent of unlikely and PTL_FATAL */
#define unlikely(x) __builtin_expect(!!(x), 0)


u16 ptl_uuid_get_op_type(u64 *uuid)
{

	ptl_uuid_t *ptl_uuid = (ptl_uuid_t *)uuid;
	return le16_to_cpu(ptl_uuid->msg_type);
}

void ptl_uuid_set_op_type(u64 *uuid, u16 msg_type)
{

	ptl_uuid_t *ptl_uuid = (ptl_uuid_t *)uuid;
	ptl_uuid->msg_type = cpu_to_le16(msg_type);
}

u16 ptl_uuid_get_cq_id(u64 *uuid)
{
	if (WARN_ON_ONCE(!uuid)) {
		return 0;
	}

	return le16_to_cpu(ptl_uuid_as_const_uuid(uuid)->cq_id);
}

void ptl_uuid_set_cq_id(u64 *uuid, u16 cq_id)
{
	ptl_uuid_t *ptl_uuid = (ptl_uuid_t *)uuid;
	ptl_uuid->cq_id = cpu_to_le16(cq_id);
}

u16 ptl_uuid_get_total_parts(u64 *uuid)
{
	const ptl_uuid_t *ptl_uuid = ptl_uuid_as_const_uuid(uuid);
	if (ptl_uuid->msg_type != NVMeOF_cpl) {
		PTL_FATAL("Request for total parts for non NVMeOF_cpl message is not allowed!");
	}
	return le16_to_cpu(ptl_uuid->uuid_nvmeof_cpl.total_parts);
}

void ptl_uuid_set_total_parts(u64 *uuid, u16 total_parts)
{
	ptl_uuid_t *ptl_uuid = (ptl_uuid_t *)uuid;
	if (ptl_uuid->msg_type != NVMeOF_cpl) {
		PTL_FATAL("Request for total parts for non NVMeOF_cpl message is not allowed!");
	}
	ptl_uuid->uuid_nvmeof_cpl.total_parts = cpu_to_le16(total_parts);
}


u16 ptl_uuid_get_nvme_cid(u64 *uuid)
{
	const ptl_uuid_t *ptl_uuid = ptl_uuid_as_const_uuid(uuid);

	if (NVMeOF_cpl == ptl_uuid->msg_type)
		return le16_to_cpu(ptl_uuid->uuid_nvmeof_cpl.cid);

	if (NVMeOF_rma == ptl_uuid->msg_type)
		return le16_to_cpu(ptl_uuid->uuid_nvmeof_rma.cid);

	PTL_FATAL("get_cid is not allowed for message type: %d", ptl_uuid->msg_type);
}

void ptl_uuid_set_nvme_cid(u64 *uuid, u16 cid)
{
	ptl_uuid_t *ptl_uuid = (ptl_uuid_t *)uuid;

	if (NVMeOF_cpl == ptl_uuid->msg_type) {
		ptl_uuid->uuid_nvmeof_cpl.cid = cpu_to_le16(cid);
		return;
	}

	if (NVMeOF_rma == ptl_uuid->msg_type) {
		ptl_uuid->uuid_nvmeof_rma.cid = cpu_to_le16(cid);
		return;
	}

	PTL_FATAL("get_cid is not allowed for message type: %d", ptl_uuid->msg_type);
}

u16 ptl_uuid_get_target_qp_num(u64* uuid)
{
	const ptl_uuid_t *ptl_uuid = ptl_uuid_as_const_uuid(uuid);
	if (NVMeOF_cmd != ptl_uuid->msg_type) {
		PTL_FATAL("get_target_qp_num is not allowed for message type: %d", ptl_uuid->msg_type);
	}
	return ptl_uuid->uuid_nvmeof_cmd.target_qp_num;
}

void ptl_uuid_set_target_qp_num(u64* uuid, u16 target_qp_num)
{
	ptl_uuid_t *ptl_uuid = (ptl_uuid_t *)uuid;
	if (NVMeOF_cmd != ptl_uuid->msg_type) {
		PTL_FATAL("get_target_qp_num is not allowed for message type: %d", ptl_uuid->msg_type);
	}
	ptl_uuid->uuid_nvmeof_cmd.target_qp_num = cpu_to_le16(target_qp_num);
}

u16 ptl_uuid_get_initiator_qp_num(u64 *uuid)
{
	const ptl_uuid_t *ptl_uuid = ptl_uuid_as_const_uuid(uuid);
	if (NVMeOF_cmd != ptl_uuid->msg_type) {
		PTL_FATAL("get_target_qp_num is not allowed for message type: %d", ptl_uuid->msg_type);
	}
	return ptl_uuid->uuid_nvmeof_cmd.initiator_qp_num;
}

void ptl_uuid_set_initiator_qp_num(u64 *uuid, u16 initiator_qp_num)
{
	ptl_uuid_t *ptl_uuid = (ptl_uuid_t *)uuid;
	if (NVMeOF_cmd != ptl_uuid->msg_type) {
		PTL_FATAL("get_target_qp_num is not allowed for message type: %d", ptl_uuid->msg_type);
	}
	ptl_uuid->uuid_nvmeof_cmd.initiator_qp_num = initiator_qp_num;
}

