# Build bxiv3_initiator.ko from rdma.c and portals shims (ib_portals.c, rdma_cm_portals.c)
obj-m += bxiv3_initiator.o
bxiv3_initiator-y := rdma.o ib_portals.o rdma_cm_portals.o mr_portals_pool.o ptl_cm_id.o ptl_pd.o ptl_bxiv3_device.o ptl_cq_pool.o ptl_cq.o ptl_qp.o ptl_uuid.o

# Kernel build tree for standard includes
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# Optional: path to private NVMe host headers (drivers/nvme/host/nvme.h)
# Override with:
#   make NVME_HOST_DIR=/path/to/linux-source/drivers/nvme/host
#original NVME_HOST_DIR ?= /root/rpmbuild/BUILD/kernel-5.14.0-570.55.1.el9_6/linux-5.14.0-570.55.1.el9.x86_64/drivers/nvme/host
NVME_HOST_DIR ?= /root/rpmbuild/BUILD/kernel-5.14.0-611.36.1.el9_7/linux-5.14.0-611.36.1.el9.x86_64/drivers/nvme/host
# Path to BXI Portals4 kernel headers (from kmod-bxi3-portals-devel)
# Override with:
#   make BXI_PORTALS4_DIR=/usr/src/bxi3-portals/include
BXI_PORTALS4_DIR ?= /usr/src/bxi3-portals/include

# Add include path for linux/bxi3 headers (ptl.h)
BXI_LINUX_DIR ?= /usr/src/bxi3-module/include
ifneq ("$(wildcard $(BXI_LINUX_DIR)/linux/bxi3/ptl.h)","")
  ccflags-y += -I$(BXI_LINUX_DIR)
else
  $(warning BXI_LINUX_DIR '$(BXI_LINUX_DIR)' does not contain linux/bxi3/ptl.h; \
           set BXI_LINUX_DIR=/usr/src/bxi3-module/include)
endif

# -------------------------
# Include paths and warnings
# -------------------------

# Add include path to private NVMe host headers if present
ifneq ("$(wildcard $(NVME_HOST_DIR)/nvme.h)","")
  ccflags-y += -I$(NVME_HOST_DIR)
else
  $(warning NVME_HOST_DIR '$(NVME_HOST_DIR)' does not contain nvme.h; \
           set NVME_HOST_DIR=/path/to/linux-source/drivers/nvme/host)
endif

# Add include path for BXI Portals4 kernel headers if present
ifneq ("$(wildcard $(BXI_PORTALS4_DIR)/portals4.h)","")
  ccflags-y += -I$(BXI_PORTALS4_DIR)
else
  $(warning BXI_PORTALS4_DIR '$(BXI_PORTALS4_DIR)' does not contain portals4.h; \
           set BXI_PORTALS4_DIR=/usr/src/bxi3-portals/include)
endif

# Add include paths for RDMA (in-tree kernel RDMA headers)
ccflags-y += -I$(KDIR)/drivers/infiniband/core -I$(KDIR)/include/rdma

# Warnings
ccflags-y += -Wall -Wextra -Wno-unused-parameter -Wno-type-limits -Wno-sign-compare
# Enable debug symbols for crash line information
ccflags-y += -g
# Optional: build with DEBUG to enable pr_debug sites, etc.
# ccflags-y += -DDEBUG

# -------------------------
# Build rules
# -------------------------

.PHONY: all clean preclean postclean

all:
	$(MAKE) -C $(KDIR) M=$(PWD) V=1 modules

# Helper: keep/restore compile_commands.json across clean
preclean:
	@if [ -f compile_commands.json ]; then \
	  cp -f compile_commands.json .compile_commands.json.bak; \
	fi

postclean:
	@if [ -f .compile_commands.json.bak ]; then \
	  mv -f .compile_commands.json.bak compile_commands.json; \
	fi

clean: preclean
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) postclean
