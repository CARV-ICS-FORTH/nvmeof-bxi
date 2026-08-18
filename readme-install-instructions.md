# SPDK + DPDK (Portals4/BXI) Docker-First Build and Run Tutorial

This workflow builds SPDK **inside Docker** (faster and cleaner than building directly in the VM), then runs target + initiator commands for Portals4.

## Environment model

- Host VM runs Docker.
- Source/data live on an NFS share (use your own path).
- Container performs SPDK build steps.

## Variables used in this guide

- `<NFS_HOST_PATH>`: host-side NFS mount (example: `/path/to/nfs/share`)
- `<NFS_CONTAINER_PATH>`: container mount point (example: `/path/to/nfs/share`)
- `<SPDK_DIR>`: SPDK source dir inside container (example: `/path/to/nfs/share/spdk`)

## Warning

- Be sure to always mount the NFS share at the same path inside the container/VM (create it in case it doesn't exist). Otherwise, the build may fail due to mismatched file paths.


## 1. Install required software

On Rocky/RHEL-like systems:

```bash
sudo dnf install -y CUnit
```

If Meson later reports missing `elftools`:

```bash
pip3 install --user pyelftools
```

## 2. Build and start the Docker build environment

Build image:

```bash
docker build -t spdk-build .
```

Run container with NFS mounted and host UID:GID mapping:

```bash
docker run -it -u "$(id -u):$(id -g)" -v <NFS_HOST_PATH>:<NFS_CONTAINER_PATH> my-app-name /bin/bash
```

## 3. Build SPDK inside Docker

Enter source tree:

```bash
cd <SPDK_DIR>
```

Initialize DPDK submodule:

```bash
git -c safe.directory="*" submodule update --init
```

Pre-build DPDK (required before SPDK `make`):

```bash
cd <SPDK_DIR>/dpdk
meson setup build -Denable_libs=hash,eal,kvargs,log,ring,mempool,mbuf -Denable_drivers="" -Ddisable_drivers=net/gve
ninja -C build
cd <SPDK_DIR>
```

Configure SPDK for Portals4 and point it to the built DPDK:

```bash
./configure --with-rdma=portals --with-dpdk=<SPDK_DIR>/dpdk/build
```

Build:

```bash
make
```

## 4. Memory settings on the target

Both settings below are runtime state and **reset on every reboot**.

Set up huge pages:

```bash
sh -c 'echo 512 > /proc/sys/vm/nr_hugepages'
```
Transparent huge pages — must be never:

**IMPORTANT FOR CONSISTENT RESULTS** — not needed to build or connect, but without it 30-60 % of
runs stall and die on a keep-alive timeout. The cause is a defect in qemu-bxi3-kvm, not in SPDK
or the initiator. Check issues for details.

``` bash
echo never > /sys/kernel/mm/transparent_hugepage/enabled
cat  /sys/kernel/mm/transparent_hugepage/enabled    # expect: always madvise [never]
```
## 5. Connect to the VM on shuttle6/7

Use this flow to discover and access the VM IP from inside the containerized environment.

On `shuttle6` or `shuttle7`:

```bash
sudo docker exec -it rocky9-libvirt /bin/bash
cd /opt/qemu-bxi3-image/
./show_ips.sh
```

Then connect from the host shell:

```bash
ssh carv@<VM_IP>
```

Login password:

```text
carvcarv
```

After login, verify the expected tools/devices:

```bash
bxinic
nvidia-smi
```

Both commands should work.

## 6. Target workflow

Start target:

```bash
ROLE=target PORTALS_PID=11 ./build/bin/nvmf_tgt -m 0x1 2>&1 | tee target.log
```
Find **BXINIC.TARGET.IP**:

To find the IP of the NIC you want to pin the target to, simply run: 
```bash
ip a | grep -i bxi0
```
and grab the first 3 out of 4 digits of `inet` field. For example, from `192.168.123.40` you need
`192.168.123`


Find **BXINIC-NID**:

```bash
sudo dmesg | grep -iE "bxi3|nid"
```                                      
And look for entries of this type: `bxi3 bxi0: Manual NID asked 0:1/0`, of which `1` is the NID of bxi0 NIC.

Create target ramdisk:

```bash
./gesalous_create_target_ramdisk.sh BXINIC.TARGET.IP.BXINIC-NID 1
```

Stop target:

```bash
pkill -9 -f nvmf_tgt; disown -a
```

## 7. Initiator workflow (Linux kernel side)

Build Linux initiator module (kernel sources needed, refer to `Troubleshooting` if missing):

First, navigate inside the kernel initiator directory: 
```bash
  cd spdk/kernel_initiator
```
and then: 
```bash   
  make NVME_HOST_DIR=/path/to/sources/BUILD/linux-5.14.0-570.39.1.el9_6/drivers/nvme/host/ \
  KBUILD_EXTRA_SYMBOLS=/usr/src/bxi3-portals/Module.symvers \
  EXTRA_CFLAGS="-DPTL_RELEASE"
```

`Note`: Removing the `EXTRA_FLAGS` option enables debugging symbols.

Load modules:

```bash
sudo modprobe nvme_core; sudo modprobe nvme; sudo modprobe nvme_fabrics; sudo modprobe nvme_rdma; sudo insmod bxiv3_initiator.ko
```

Connect to SPDK target:

```bash
sudo nvme connect -t portals4 -a BXINIC.TARGET.IP.<BXINIC-NID> -s 11 -n nqn.2016-06.io.spdk:cnode1
```
`Note:` -s argument = `PORTALS_PID` of `nvmf_tgt` application.

## Troubleshooting

### `mk/config.mk not found`

DPDK submodule did not initialize. Run:

```bash
git -c safe.directory="*" submodule update --init
```

### `Missing python module: elftools`

Run:

```bash
pip3 install --user pyelftools
```

### `Missing kernel sources`

To validate/align NVMe host interfaces (`nvme.h`, `fabrics.h`, `rdma.c`), unpack matching kernel sources.

Download exact Rocky source RPM:

```bash 
wget https://download.rockylinux.org/vault/rocky/9.6/devel/source/tree/Packages/k/kernel-5.14.0-570.39.1.el9_6.src.rpm
```

Install source RPM (example local path):

```bash
rpm -ivh /path/to/kernel_src/kernel-5.14.0-570.39.1.el9_6.src.rpm
```

Unpack kernel source:

```bash
cd ~/rpmbuild/BUILD
tar xf ~/rpmbuild/SOURCES/linux-5.14.0-570.39.1.el9_6.tar.xz
