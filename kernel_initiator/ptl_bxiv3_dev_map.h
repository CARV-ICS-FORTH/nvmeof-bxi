
#ifndef PTL_BXIV3_DEV_MAP_H
#define PTL_BXIV3_DEV_MAP_H
#include <linux/types.h>
#define PTL_MAX_NICIA_NUM 2
struct ptl_bxiv3_dev_map {
	struct ptl_bxiv3_device *bxiv3_dev[PTL_MAX_NICIA_NUM];
	u32 num_nicia;
};
#endif
