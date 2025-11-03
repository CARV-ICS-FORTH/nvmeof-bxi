# Build bxiv3_initiator.ko from rdma.c and portals shims (ib_portals.c, rdma_cm_portals.c)
obj-m += bxiv3_initiator.o
bxiv3_initiator-y := rdma.o ib_portals.o rdma_cm_portals.o

# Kernel build tree for standard includes
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# Optional: NVME_HOST_DIR points to the directory containing the private header drivers/nvme/host/nvme.h
# Override with:
#   make NVME_HOST_DIR=/path/to/linux-source/drivers/nvme/host
NVME_HOST_DIR ?= /root/rpmbuild/BUILD/kernel-5.14.0-570.55.1.el9_6/linux-5.14.0-570.55.1.el9.x86_64/drivers/nvme/host

# Add include path to private NVMe host headers if the directory exists
ifneq ("$(wildcard $(NVME_HOST_DIR)/nvme.h)","")
ccflags-y += -I$(NVME_HOST_DIR)
else
$(warning NVME_HOST_DIR '$(NVME_HOST_DIR)' does not contain nvme.h; set NVME_HOST_DIR=/path/to/linux-source/drivers/nvme/host)
endif

# Add include path for RDMA headers (if building against in-tree kernel RDMA)
ccflags-y += -I$(KDIR)/drivers/infiniband/core -I$(KDIR)/include/rdma

# Optional warnings
ccflags-y += -Wall -Wextra -Wno-unused-parameter -Wno-type-limits -Wno-sign-compare

# Default build targets
all:
	$(MAKE) -C $(KDIR) M=$(PWD) V=1 modules

# Helper: keep/restore compile_commands.json across clean
.PHONY: preclean postclean
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
	@# The kernel clean may remove everything; restore our compile_commands.json if we had it
	$(MAKE) postclean
