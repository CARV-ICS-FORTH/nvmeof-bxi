#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
#  All rights reserved.
#

SPDK_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

SO_VER := 2
SO_MINOR := 0

SPDK_MAP_FILE = $(abspath $(CURDIR)/spdk_rdma_utils.map)

LIBNAME = rdma_utils


# gesalous
ifeq ($(CONFIG_RDMA_PROV),portals)
C_SRCS = rdma_utils_portals.c
CFLAGS += -I$(CONFIG_DPDK_INC_DIR)
CFLAGS += -I$(PORTALS_INCLUDE_PREFIX)
#gesalous, don't need it everything done in mk/spdk.common.mk
# LDFLAGS += -L$(PORTALS_LIB_PREFIX)
# LOCAL_SYS_LIBS += -lportals
# LDFLAGS += -L$(RDMA_CM_PORTALS_PREFIX)
# LOCAL_SYS_LIBS += -lrdmacmportals
else
C_SRCS = rdma_utils.c
endif

#original
#C_SRCS = rdma_utils.c

ifeq ($(CONFIG_RDMA_PROV),portals)
	LOCAL_SYS_LIBS += -L$(RDMA_CM_PORTALS_PREFIX) -lrdmacm_portals -libverbs_portals -lspdk_ptl_core
else
	LOCAL_SYS_LIBS += -libverbs -lrdmacm
endif


#Attach only if FreeBSD and RDMA is specified with configure
ifeq ($(OS),FreeBSD)
# Mellanox - MLX4 HBA Userspace Library
ifneq ("$(wildcard /usr/lib/libmlx4.*)","")
LOCAL_SYS_LIBS += -lmlx4
endif
# Mellanox - MLX5 HBA Userspace Library
ifneq ("$(wildcard /usr/lib/libmlx5.*)","")
LOCAL_SYS_LIBS += -lmlx5
endif
# Chelsio HBA Userspace Library
ifneq ("$(wildcard /usr/lib/libcxgb4.*)","")
LOCAL_SYS_LIBS += -lcxgb4
endif
endif

include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk
