#!/bin/bash
set -e

# --- Environment Validation ---
# We validate WORKSPACE here because it is injected dynamically by 01_host_launcher.sh
if [ -z "${BUILD_USER}" ] || [ -z "${BUILD_UID}" ] || [ -z "${BUILD_GID}" ] || [ -z "${WORKSPACE}" ]; then
    echo "[ERROR] Missing environment variables."
    echo "This script is designed to be executed automatically by 01_host_launcher.sh."
    echo "Do not run it manually."
    exit 1
fi

# --- Configuration ---
SRC_DIR="${WORKSPACE}/sources"
INSTALL_DIR="${WORKSPACE}/install"

KERNEL_RPM="kernel-5.14.0-570.39.1.el9_6.src.rpm"
BASE_VER="5.14.0-570.39.1.el9_6"
KVER="${BASE_VER}.x86_64.kasan"

echo "[*] Setting up user inside container..."
groupadd -g ${BUILD_GID} ${BUILD_USER} || true
useradd -u ${BUILD_UID} -g ${BUILD_GID} -m ${BUILD_USER} || true

echo "[*] Extracting Kernel Sources..."
cd ${SRC_DIR}

# Fallback: Download the RPM if it doesn't exist
if [ ! -f "${KERNEL_RPM}" ]; then
    echo "[*] RPM not found. Downloading from Rocky Linux Vault..."
    su ${BUILD_USER} -c "cd ${SRC_DIR} && wget https://download.rockylinux.org/vault/rocky/9.6/devel/source/tree/Packages/k/${KERNEL_RPM}"
fi

rpm -ivh ${KERNEL_RPM}

mkdir -p /work/kernel
cd /root/rpmbuild/SOURCES/
tar -xJf linux-*.tar.xz -C /work/kernel/
cd /work/kernel/linux-${BASE_VER}/

echo "[*] Configuring Kernel..."
cp ${SRC_DIR}/vm-${BASE_VER}.x86_64.config .config

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

# Pin base version and append .kasan
sed -i "s/^EXTRAVERSION =.*/EXTRAVERSION = -${BASE_VER#*-}/" Makefile
scripts/config --set-str CONFIG_LOCALVERSION ".x86_64.kasan"

echo "[*] Building Kernel (this will take a while)..."
make -j$(nproc)

echo "[*] Exporting artifacts to NFS as ${BUILD_USER}..."
su ${BUILD_USER} -c "mkdir -p ${INSTALL_DIR}"

# Add explicit cd commands to ensure the sub-shell is in the right directory
su ${BUILD_USER} -c "cd /work/kernel/linux-${BASE_VER} && make INSTALL_MOD_PATH=${INSTALL_DIR} modules_install CONFIG_MODULE_SIG=n CONFIG_MODULE_SIG_ALL=n"
su ${BUILD_USER} -c "cd /work/kernel/linux-${BASE_VER} && cp arch/x86/boot/bzImage ${INSTALL_DIR}/vmlinuz-${KVER}"
su ${BUILD_USER} -c "cd /work/kernel/linux-${BASE_VER} && cp System.map ${INSTALL_DIR}/System.map-${KVER}"

echo "[SUCCESS] Build environment finished successfully."
