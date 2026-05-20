#!/bin/bash
set -e

# Initialize empty mandatory variables
WORKSPACE=""
INSTALL_DIR=""

# --- Help Annotation ---
show_help() {
    cat << EOF
Usage: ${0##*/} --workspace <PATH> --install-dir <PATH>

Install the custom KASAN kernel, modules, and safely configure GRUB on the target VM.
This script must be run as root.

Mandatory Arguments:
  -w, --workspace PATH     Path to the base untracked workspace directory.
  -i, --install-dir PATH   Path where the compiled kernel artifacts were exported.
                           (This is where Script 2 placed the vmlinuz binary,
                           System.map, and the compiled modules folder).
  -h, --help               Display this help and exit

Example:
  ${0##*/} -w /home1/public/ballis/kernel-kasan -i /home1/public/ballis/kernel-kasan/install
EOF
}

# --- Argument Parsing ---
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -w|--workspace) WORKSPACE="$2"; shift ;;
        -i|--install-dir) INSTALL_DIR="$2"; shift ;;
        -h|--help) show_help; exit 0 ;;
        *) echo "[ERROR] Unknown parameter passed: $1"; echo ""; show_help; exit 1 ;;
    esac
    shift
done

# --- Validation ---
if [ -z "$WORKSPACE" ] || [ -z "$INSTALL_DIR" ]; then
    echo "[ERROR] Both --workspace and --install-dir are mandatory parameters."
    echo ""
    show_help
    exit 1
fi

# Ensure absolute paths are provided
if [[ "$WORKSPACE" != /* ]] || [[ "$INSTALL_DIR" != /* ]]; then
    echo "[ERROR] Please provide absolute paths (starting with '/') for directories."
    exit 1
fi

# Ensure script is run as root
if [[ $EUID -ne 0 ]]; then
   echo "[ERROR] This script must be run as root to modify /boot and /lib/modules."
   exit 1
fi

# Ensure the installation directory actually exists before proceeding
if [ ! -d "$INSTALL_DIR" ]; then
    echo "[ERROR] The installation directory ($INSTALL_DIR) does not exist."
    echo "Has the Docker build (Script 2) completed successfully?"
    exit 1
fi

# --- Configuration ---
BASE_VER="5.14.0-570.39.1.el9_6"
KVER="${BASE_VER}.x86_64.kasan"

# --- Execution ---
echo "[*] Copying modules to system directory from ${INSTALL_DIR}..."
cp -r ${INSTALL_DIR}/lib/modules/${KVER} /lib/modules/
rm -f /lib/modules/${KVER}/build
rm -f /lib/modules/${KVER}/source

echo "[*] Installing Kernel and generating Initramfs..."
cp ${INSTALL_DIR}/vmlinuz-${KVER} /boot/
dracut -f /boot/initramfs-${KVER}.img ${KVER}

echo "[*] Configuring GRUB BLS..."
# Find the default entry for the base version to duplicate safely
DEFAULT_ENTRY=$(ls /boot/loader/entries/*-${BASE_VER}.x86_64.conf | head -n 1)

if [ -z "$DEFAULT_ENTRY" ]; then
    echo "[ERROR] Could not find original GRUB entry for ${BASE_VER} to duplicate."
    exit 1
fi

NEW_ENTRY="/boot/loader/entries/custom-kasan.conf"
cp ${DEFAULT_ENTRY} ${NEW_ENTRY}

# Safely replace the version string everywhere to preserve $tuned_initrd and formatting
sed -i "s/${BASE_VER}.x86_64/${KVER}/g" ${NEW_ENTRY}
# Update the title to flag it as KASAN
sed -i "s/Rocky Linux/Rocky Linux KASAN/" ${NEW_ENTRY}

echo "[*] Porting proprietary bxi3 module..."
mkdir -p /lib/modules/${KVER}/weak-updates/drivers/char/
cp -r /lib/modules/${BASE_VER}.x86_64/weak-updates/drivers/char/bxi3 /lib/modules/${KVER}/weak-updates/drivers/char/
chmod -R 755 /lib/modules/${KVER}/weak-updates/

echo "[*] Updating module dependencies..."
depmod -a ${KVER}

echo "[SUCCESS] Installation complete! Run 'grubby --info=ALL' to verify, then reboot."
