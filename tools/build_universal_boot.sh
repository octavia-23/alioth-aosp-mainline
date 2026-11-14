#!/usr/bin/env bash
# ==============================================================================
# Universal Clean Boot Image Builder for Xiaomi POCO F3 (Alioth / SM8250)
# Mainline Linux 6.19.6 + KernelSU-Next + In-Tree Hardware Drivers
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORKSPACE="/home/aksha/alioth-mainline-workspace"
RAMDISK_SRC="$WORKSPACE/references/clover/boot_ramdisk"
DIST_DIR="$WORKSPACE/dist"
HOST_DIST="/mnt/c/Users/aksha/Desktop/alioth mainline/dist"

echo "=========================================================="
echo " Building Universal Alioth Mainline Kernel (Linux 6.19.6) "
echo "=========================================================="

# 1. Compile Kernel Image & DTB
cd "$KERNEL_DIR"
echo "[1/4] Compiling kernel Image & DTB..."
make -j$(nproc) ARCH=arm64 LLVM=1 Image qcom/sm8250-xiaomi-alioth.dtb

IMAGE="$KERNEL_DIR/arch/arm64/boot/Image"
DTB="$KERNEL_DIR/arch/arm64/boot/dts/qcom/sm8250-xiaomi-alioth.dtb"

test -s "$IMAGE" || { echo "Error: Kernel Image build failed"; exit 1; }
test -s "$DTB" || { echo "Error: DTB build failed"; exit 1; }

# 2. Package Clean Generic Ramdisk (No userspace binary byte-patching)
echo "[2/4] Packaging clean generic AOSP first-stage ramdisk..."
mkdir -p "$DIST_DIR"
TMP_RD=$(mktemp -d /tmp/alioth_rd_XXXXXX)
trap "rm -rf '$TMP_RD'" EXIT

cp -a "$RAMDISK_SRC/." "$TMP_RD/"
cd "$TMP_RD"

# Ensure standard permissions
find . -type d -exec chmod 0755 {} +
find . -type f -exec chmod 0644 {} +
chmod 0755 init 2>/dev/null || true

RAMDISK_OUT="$DIST_DIR/generic_ramdisk.cpio.gz"
find . -mindepth 1 | sort | cpio -o -H newc -R 0:0 2>/dev/null | gzip -9 > "$RAMDISK_OUT"
ls -lh "$RAMDISK_OUT"

# 3. Create Android Boot Header v2 Image
echo "[3/4] Creating boot_alioth_universal.img with mkbootimg..."
BOOT_OUT="$DIST_DIR/boot_alioth_universal.img"

CMDLINE="console=tty0 clk_ignore_unused pd_ignore_unused panic=5 androidboot.hardware=qcom swiotlb=2048 androidboot.usbcontroller=a600000.dwc3 androidboot.fstab_suffix=qcom androidboot.boot_devices=soc@0/1d84000.ufshc,soc/1d84000.ufshc,1d84000.ufshc firmware_class.path=/vendor/firmware_mnt/image,/vendor/firmware androidboot.super_partition=system loop.max_part=7 androidboot.selinux=permissive printk.devkmsg=on"

mkbootimg \
  --header_version 2 \
  --pagesize 4096 \
  --base 0x00000000 \
  --kernel_offset 0x00008000 \
  --ramdisk_offset 0x01000000 \
  --tags_offset 0x00000100 \
  --dtb_offset 0x01f00000 \
  --kernel "$IMAGE" \
  --ramdisk "$RAMDISK_OUT" \
  --dtb "$DTB" \
  --cmdline "$CMDLINE" \
  --output "$BOOT_OUT"

# 4. Copy to Windows Workspace
echo "[4/4] Exporting to Windows workspace..."
mkdir -p "$HOST_DIST"
cp "$BOOT_OUT" "$HOST_DIST/boot_alioth_universal.img"

echo "=========================================================="
echo " SUCCESS! Universal boot image ready at:                 "
echo " WSL:     $BOOT_OUT"
echo " Windows: $HOST_DIST/boot_alioth_universal.img"
echo "=========================================================="
ls -lh "$HOST_DIST/boot_alioth_universal.img"
