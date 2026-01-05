#  SPDX-License-Identifier: BSD-3-Clause
#  Copyright (C) 2020 Intel Corporation. All rights reserved.
#  Copyright (c) Mellanox Technologies LTD. All rights reserved.
#  Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#

SPDK_ROOT_DIR := $(abspath $(CURDIR)/../..)
include $(SPDK_ROOT_DIR)/mk/spdk.common.mk

SO_VER := 8
SO_MINOR := 0

SPDK_MAP_FILE = $(abspath $(CURDIR)/spdk_rdma_provider.map)
include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk
LIBNAME = rdma_provider

# Portals core library settings (shared by both providers)
CORE_LIBNAME = spdk_ptl_core
CORE_SRCS = ptl_cm_id.c
CORE_SRCS += ptl_context.c
CORE_SRCS += dlist.c
CORE_SRCS += deque.c
CORE_SRCS += ptl_cq.c
CORE_SRCS += ptl_pd.c
CORE_SRCS += ptl_qp.c
CORE_SRCS += ptl_srq.c
CORE_SRCS += ptl_uuid.c
CORE_SRCS += ptl_mem_desc.c
CORE_SRCS += ptl_print_nvme_commands.c
CORE_SO = lib$(CORE_LIBNAME).so

# librdmacm library settings
RDMACM_LIBNAME = rdmacm_portals
RDMACM_SRCS = rdma_cm_portals.c
RDMACM_SO = lib$(RDMACM_LIBNAME).so

# libibverbs library settings
VERBS_LIBNAME = ibverbs_portals
VERBS_SRCS = verbs_mockup.c
VERBS_SO = lib$(VERBS_LIBNAME).so


# Add configurable Portals prefix
PORTALS_LIB_PREFIX ?= /usr/local
PORTALS_INCLUDE_PREFIX ?= /usr/local/include

ifeq ($(CONFIG_RDMA_PROV),verbs)
C_SRCS = common.c
C_SRCS += rdma_provider_verbs.c
else ifeq ($(CONFIG_RDMA_PROV),mlx5_dv)
C_SRCS = common.c
C_SRCS += rdma_provider_mlx5_dv.c
LOCAL_SYS_LIBS += -lmlx5
else ifeq ($(CONFIG_RDMA_PROV),portals)
C_SRCS = rdma_provider_portals.c
C_SRCS += ptl_rte_hash_map.c
EXTRA_TARGETS = $(CORE_SO) $(VERBS_SO) $(RDMACM_SO)
# gesalous, not needed everything set in mk/spdk.common.mk
LOCAL_SYS_LIBS = -L$(PORTALS_LIB_PREFIX) -lportals
LOCAL_SYS_LIBS = -L$(RDMA_CM_PORTALS_PREFIX) -lspdk_ptl_core -libverbs_portals -lrdmacm_portals -lspdk_ptl_core
# gesalous, not needed everything set in mk/spdk.common.mk
# LDFLAGS += -Wl,-rpath,$(PORTALS_LIB_PREFIX)
# LDFLAGS += -Wl,-rpath,-L$(RDMA_CM_PORTALS_PREFIX) 
CFLAGS += -I$(CONFIG_DPDK_INC_DIR)
CFLAGS += -I$(PORTALS_INCLUDE_PREFIX)

else
$(error Wrong RDMA provider specified: $(CONFIG_RDMA_PROV))
endif

ifneq ($(CONFIG_RDMA_PROV),portals)
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

# Build portals core shared library (must be built first)
$(CORE_SO): $(CORE_SRCS)
	$(CC) -shared -o $@ $^ -fPIC $(CFLAGS) \
		-I$(PORTALS_INCLUDE_PREFIX) \
		-L$(PORTALS_LIB_PREFIX) -lportals \
		-Wl,-rpath,$(PORTALS_LIB_PREFIX)

$(VERBS_SO): $(VERBS_SRCS) $(CORE_SO)
	$(CC) -shared -o $@ $^ -fPIC $(CFLAGS) \
	    -L. -lspdk_ptl_core -Wl,-rpath,'$$ORIGIN'

$(RDMACM_SO): $(RDMACM_SRCS) $(CORE_SO)
	$(CC) -shared -o $@ $^ -fPIC $(CFLAGS) \
	    -L. -lspdk_ptl_core -ldl -Wl,-rpath,'$$ORIGIN'


.PHONY: core_clean
core_clean:
	$(RM) $(CORE_SO)

.PHONY: verbs_clean
verbs_clean:
	$(RM) $(VERBS_SO)

.PHONY: rdmacm_clean
rdmacm_clean:
	$(RM) $(RDMACM_SO)


clean: core_clean verbs_clean rdmacm_clean

# Add verbs, rdmacm impl over portals to all targets if we're using portals
ifeq ($(CONFIG_RDMA_PROV),portals)
all: $(EXTRA_TARGETS)
endif

.PHONY: rdmacm
rdmacm: $(RDMACM_SO)

include $(SPDK_ROOT_DIR)/mk/spdk.lib.mk
