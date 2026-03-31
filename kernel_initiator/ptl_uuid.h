#ifndef PTL_UUID_H
#define PTL_UUID_H

#include <linux/types.h>


struct ptl_uuid_nvmeof_cmd {
	u16 target_qp_num;
	u16 initiator_qp_num;
} __attribute((packed));

struct ptl_uuid_nvmeof_cpl {
	u16 cid;
	u16 total_parts;
} __attribute((packed));

struct ptl_uuid_nvmeof_rma {
	u16 cid;
	u16 future_extension_1;
} __attribute((packed));

struct ptl_uuid_open_conn {
	u16 future_extension_1;
	u16 future_extension_2;
} __attribute((packed));

struct ptl_uuid_open_conn_rep {
	u16 future_extension_1;
	u16 future_extension_2;
} __attribute((packed));

struct ptl_uuid_close_conn {
	u16 future_extension_1;
	u16 future_extension_2;
} __attribute((packed));


struct ptl_uuid_close_conn_rep {
	u16 future_extension_1;
	u16 future_extension_2;
} __attribute((packed));
/**
 * @brief Portals4 UUID Layout (8 Bytes / Little-Endian)
 */
typedef struct {
	u16 msg_type;
	u16 cq_id;
	union {
		struct ptl_uuid_nvmeof_cmd uuid_nvmeof_cmd;
		struct ptl_uuid_nvmeof_cpl uuid_nvmeof_cpl;
		struct ptl_uuid_nvmeof_rma uuid_nvmeof_rma;
		struct ptl_uuid_open_conn uuid_open_conn;
		struct ptl_uuid_open_conn_rep uuid_open_conn_rep;
		struct ptl_uuid_close_conn uuid_close_conn;
		struct ptl_uuid_close_conn_rep uuid_close_conn_rep;
	};
} __attribute__((packed)) ptl_uuid_t;



/* --- Zero-Copy Casting Helpers --- */

static inline const ptl_uuid_t *ptl_uuid_as_const_uuid(const u64* raw)
{
	return (const ptl_uuid_t *)raw;
}

static inline ptl_uuid_t *ptl_uuid_as_uuid(u64 *raw)
{
	return (ptl_uuid_t *)raw;
}



u16 ptl_uuid_get_op_type(u64 *uuid);
void ptl_uuid_set_op_type(u64 *uuid, u16 op_type);

u16 ptl_uuid_get_nvme_cid(u64 *uuid);
void ptl_uuid_set_nvme_cid(u64 *uuid, u16 nvme_cid);

u16 ptl_uuid_get_total_parts(u64 *uuid);
void ptl_uuid_set_total_parts(u64 *uuid, u16 total_parts);

u16 ptl_uuid_get_cq_id(u64 *uuid);
void ptl_uuid_set_cq_id(u64 *uuid, u16 cq_id);

u16 ptl_uuid_get_target_qp_num(u64* uuid);
void ptl_uuid_set_target_qp_num(u64* uuid, u16 target_qp_num);

u16 ptl_uuid_get_initiator_qp_num(u64 *uuid);
void ptl_uuid_set_initiator_qp_num(u64 *uuid, u16 initiator_qp_num);



#endif /* PTL_UUID_H */

