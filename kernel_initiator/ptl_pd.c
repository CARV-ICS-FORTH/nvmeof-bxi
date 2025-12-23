#include "ptl_pd.h"
#include "ptl_object_types.h"

struct ptl_pd *ptl_pd_alloc(struct ib_device *dev, unsigned int flags)
{

	if (flags) {
		PTL_FATAL("Sorry only flags = 0 supported");
		return ERR_PTR(-EOPNOTSUPP);
	}
	struct ptl_pd *ptl_pd = kzalloc(sizeof(*ptl_pd), GFP_KERNEL);
	if (!ptl_pd)
		return ERR_PTR(-ENOMEM);
	ptl_pd->object_type = PTL_PD;
	ptl_pd->fake_pd.device = dev;
	PTL_DEBUG("Successfully created PTL_PD");
	return ptl_pd;
}
