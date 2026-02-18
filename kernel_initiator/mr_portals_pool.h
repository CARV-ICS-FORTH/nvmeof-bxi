#include "ptl_bxiv3_device.h"
#include "ptl_object_types.h"
#include "rdma/ib_verbs.h"
#ifndef MR_PORTALS_POOL_H
#define MR_PORTALS_POOL_H 1
#include <rdma/mr_pool.h>
struct ptl_mr {
	ptl_obj_type_e object_type;
	struct ptl_bxiv3_device *bxi3_device;
	struct ib_mr fake_mr;
	struct list_head mr_entry;
	u32 max_num_sg;
	u32 max_num_meta_sg;
	enum ib_mr_type type;
};

struct ib_mr *ib_portals_mr_pool_get(struct ib_qp *qp, struct list_head *list);
void ib_portals_mr_pool_put(struct ib_qp *qp, struct list_head *list,
                            struct ib_mr *mr);

int ib_portals_mr_pool_init(struct ib_qp *qp, struct list_head *list, int nr,
                            enum ib_mr_type type, u32 max_num_sg,
                            u32 max_num_meta_sg);
void ib_portals_mr_pool_destroy(struct ib_qp *qp, struct list_head *list);

struct ptl_mr *ptl_mr_create(u32 max_num_sg, u32 max_num_meta_sg,
                             enum ib_mr_type type,
                             struct ptl_bxiv3_device *bxi3_device);
void ptl_mr_pool_destroy(struct ptl_mr *ptl_mr);
#endif
