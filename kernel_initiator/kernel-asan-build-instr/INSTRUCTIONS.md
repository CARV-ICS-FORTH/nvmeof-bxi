# KASAN Kernel Build & Deploy Guide

# Phase 1: Environment Setup & Prerequisites

## 1. Prepare the Unified Workspace

Decide on your base NFS path (e.g., `/home1/public/ballis`). Create the unified directory structure for your kernel sources and installation artifacts:

```bash
mkdir -p <NFS_SHARE_PATH>/kernel-kasan/sources
mkdir -p <NFS_SHARE_PATH>/kernel-kasan/install
```

## 2. Gather Host Credentials & VM Config

On your host machine, find your user IDs:

```bash
id -u <username>
id -g <username>
```

On the target VM, copy your current kernel config to the new sources directory:

```bash
cp /boot/config-$(uname -r) <NFS_SHARE_PATH>/kernel-kasan/sources/vm-5.14.0-570.39.1.el9_6.x86_64.config
```

## 3. Acquire the Kernel Source

Check if the source RPM is in your sources directory. If it is missing, download it directly from the Rocky Linux vault:

```bash
cd <NFS_SHARE_PATH>/kernel-kasan/sources/
wget https://download.rockylinux.org/vault/rocky/9.6/devel/source/tree/Packages/k/kernel-5.14.0-570.39.1.el9_6.src.rpm
```

# Phase 2: Source Extraction & Configuration (Docker)

## Note: Go to the end of this file to see the usage of the automated scripts.

## 4. Launch the Build Container

Start the Docker container, mounting your NFS share.

```bash
docker run -it --rm -v <NFS_SHARE_PATH>:<NFS_SHARE_PATH> kernel_build:latest /bin/bash
```

## 5. Map Your User Inside Docker

Once inside the container, recreate your user so files written to the NFS share have the correct permissions (replace IDs with the output from Step 2).

```bash
useradd -u <user_id> -g <user_group_id> -m <username>
```

## 6. Unpack the Kernel Source

Inside the container (as root), navigate to the sources directory and extract it:

```bash
cd <NFS_SHARE_PATH>/kernel-kasan/sources/
rpm -ivh kernel-5.14.0-570.39.1.el9_6.src.rpm

mkdir -p /work/kernel
cd ~/rpmbuild/SOURCES/
tar -xJf linux-*.tar.xz -C /work/kernel/
cd /work/kernel/linux-5.14.0-570.39.1.el9_6/
```

## 7. Apply Config and Inject KASAN Flags

Copy the VM config into the source tree:

```bash
cp <NFS_SHARE_PATH>/kernel-kasan/sources/vm-5.14.0-570.39.1.el9_6.x86_64.config .config
```

Run the config script to enable KASAN, disable signatures, and strip module versioning:

```bash
scripts/config \
  --enable CONFIG_KASAN \
  --enable CONFIG_KASAN_GENERIC \
  --enable CONFIG_KASAN_INLINE \
  --enable CONFIG_KASAN_VMALLOC \
  --enable CONFIG_DEBUG_KERNEL \
  --enable CONFIG_SLUB_DEBUG \
  --enable CONFIG_DMA_API_DEBUG \
  --enable CONFIG_FRAME_POINTER \
  --enable CONFIG_STACKTRACE \
  --enable CONFIG_DEBUG_INFO \
  --enable CONFIG_PANIC_ON_OOPS \
  --disable CONFIG_MODULE_SIG \
  --disable CONFIG_MODULE_SIG_FORCE \
  --disable CONFIG_MODULE_SIG_ALL \
  --disable CONFIG_KFENCE \
  --disable CONFIG_MODVERSIONS \
  --set-str CONFIG_SYSTEM_TRUSTED_KEYS "" \
  --set-str CONFIG_SYSTEM_REVOCATION_KEYS "" \
  --set-str CONFIG_MODULE_SIG_KEY ""

make olddefconfig
```

## 8. Tag the Custom Release

Pin the base extra-version and append your `.kasan` tag:

```bash
sed -i 's/^EXTRAVERSION =.*/EXTRAVERSION = -570.39.1.el9_6/' Makefile
scripts/config --set-str CONFIG_LOCALVERSION ".x86_64.kasan"

# Verify it returns: 5.14.0-570.39.1.el9_6.x86_64.kasan
make kernelrelease LOCALVERSION=".x86_64.kasan"
```

# Phase 3: Build & Export (Inside Container)

## 9. Compile the Kernel

```bash
make -j$(nproc) LOCALVERSION=".x86_64.kasan"
```

Note: `make -j` automatically builds both the kernel image and the modules. No separate `make modules` command is needed.

## 10. Export to NFS as Your User

Do not use `make install` here, as the container lacks a bootloader and will fail. Instead, export the modules and manually copy the core binaries to your NFS installation directory using your mapped user account:

```bash
# Install modules to the staging directory on NFS
su <username> -c "make INSTALL_MOD_PATH=<NFS_SHARE_PATH>/kernel-kasan/install modules_install CONFIG_MODULE_SIG=n CONFIG_MODULE_SIG_ALL=n"

# Copy the kernel image and System.map
su <username> -c "cp arch/x86/boot/bzImage <NFS_SHARE_PATH>/kernel-kasan/install/vmlinuz-5.14.0-570.39.1.el9_6.x86_64.kasan"
su <username> -c "cp System.map <NFS_SHARE_PATH>/kernel-kasan/install/System.map-5.14.0-570.39.1.el9_6.x86_64.kasan"
```

You can now safely exit the Docker container.

# Phase 4: Target VM Installation

## 11. Install the Modules and Clean Symlinks

Log into your target VM and copy the modules into the system path. Delete the broken symlinks (they pointed to the dead Docker container path).

```bash
cp -r <NFS_SHARE_PATH>/kernel-kasan/install/lib/modules/5.14.0-570.39.1.el9_6.x86_64.kasan /lib/modules/
rm -f /lib/modules/5.14.0-570.39.1.el9_6.x86_64.kasan/build
rm -f /lib/modules/5.14.0-570.39.1.el9_6.x86_64.kasan/source
```

## 12. Install the Kernel Image & Initramfs

```bash
cp <NFS_SHARE_PATH>/kernel-kasan/install/vmlinuz-5.14.0-570.39.1.el9_6.x86_64.kasan /boot/
dracut /boot/initramfs-5.14.0-570.39.1.el9_6.x86_64.kasan.img 5.14.0-570.39.1.el9_6.x86_64.kasan
```

## 13. Configure GRUB via BLS

Safely duplicate the current boot entry without modifying the default fallback:

```bash
ls -l /boot/loader/entries/
cp /boot/loader/entries/YOUR_MACHINE_ID-5.14.0-570.39.1.el9_6.x86_64.conf /boot/loader/entries/custom-kasan.conf
vi /boot/loader/entries/custom-kasan.conf
```

Change only the title, version, linux, and initrd lines to include the `.kasan` suffix. Leave the options line untouched.

## 14. Verify GRUB

```bash
grubby --info=ALL
```

# Phase 5: The Proprietary Module Bypass

## 15. Force-Port the bxi3 Module

Because you disabled `CONFIG_MODVERSIONS` in Step 7, the new kernel will not reject this binary module during load based on version magic. Copy it from the old kernel's weak-updates folder into your new KASAN module tree:

```bash
mkdir -p /lib/modules/5.14.0-570.39.1.el9_6.x86_64.kasan/weak-updates/drivers/char/

cp -r /lib/modules/5.14.0-570.39.1.el9_6.x86_64/weak-updates/drivers/char/bxi3 \
      /lib/modules/5.14.0-570.39.1.el9_6.x86_64.kasan/weak-updates/drivers/char/

chmod -R 755 /lib/modules/5.14.0-570.39.1.el9_6.x86_64.kasan/weak-updates
```

## 16. Update Module Dependencies and Reboot

Update the system's module map so it recognizes the newly copied `bxi3` driver, then reboot to test.

```bash
depmod -a 5.14.0-570.39.1.el9_6.x86_64.kasan
reboot
```

Select the KASAN kernel manually from the GRUB menu during startup.

If it does reject it, use `modprobe -f` to force-load the module.

# Phase 6: Automated Workflow (The Scripts)

If you prefer to automate Phases 2 through 5, use these three scripts.

## How to use them:

 - Complete Phase 1 (Prerequisites) manually so the source and config are in `<NFS_SHARE_PATH>/kernel-kasan/sources/`.

 - Make sure all the scripts are in your NFS share and make them executable (`chmod +x`).

 - Run Script 1 on the Host VM. It will automatically spin up Docker, pass your credentials, and execute Script 2 inside the container to build the kernel.

 - Run Script 3 as root on your Target VM to install the generated kernel and set up GRUB.


#Phase 7: Fix environment to enable building of the NVMe-oF module

-In the container compress kernel in /work/kernel to move it in NFS
`tar -czf /tmp/linux-5.14.0-570.39.1.el9_6.tar.gz linux-5.14.0-570.39.1.el9_6`

-Move it in the NFS share
`su <username> -c "cp /tmp/linux-5.14.0-570.39.1.el9_6.tar.gz <NFS_PREFIX>/kernel-kasan/"`

-Back to the VM. Uncompress it in /usr/src
`tar -xzf <NFS_PREFIX>/kernel-kasan/linux-5.14.0-570.39.1.el9_6.tar.gz`

-Set environment variable
`KVER="5.14.0-570.39.1.el9_6.x86_64.kasan`

-Delete /lib/modules/$KVER which contains the broken symlink
`rm -f /lib/modules/$KVER/build`

-Create the new healthy symlink
`ln -sf /usr/src/linux-5.14.0-570.39.1.el9_6 /lib/modules/$KVER/build`
`cd /usr/src/linux-5.14.0-570.39.1.el9_6`

-Prepare the kernel to be able to build modules
`make modules_prepare`

-Then, go into the NVMe-oF module code
`cd <NFS_PATH_TO_SPDK>/kernel_initiator`

-Execute
`make -C /lib/modules/$KVER/build M=$PWD modules
KBUILD_EXTRA_SYMBOLS=/usr/src/bxi3-portals/Module.symvers     
NVME_HOST_DIR=/usr/src/linux-5.14.0-570.39.1.el9_6/drivers/nvme/host/ EXTRA_CFLAGS="-DPTL_RELEASE"`



















