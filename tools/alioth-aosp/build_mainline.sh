#!/usr/bin/env bash
#
# Alioth Mainline Linux 6.19 (Parity with rmuxnet/linux 7.4-rc3)
# Clean AOSP Boot Image Builder
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

export ARCH=arm64
export CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

echo "=== [1/4] Preparing Kconfig for Alioth AOSP Mainline ==="
cd "${KERNEL_DIR}"
make defconfig alioth-android-base.config alioth-android.config alioth-prune.config

echo "=== [2/4] Compiling Kernel Image.gz & Alioth DTB ==="
make -j"$(nproc)" Image.gz qcom/sm8250-xiaomi-alioth.dtb

DTB="${KERNEL_DIR}/arch/arm64/boot/dts/qcom/sm8250-xiaomi-alioth.dtb"
IMAGE="${KERNEL_DIR}/arch/arm64/boot/Image.gz"

if [[ ! -f "${IMAGE}" ]] || [[ ! -f "${DTB}" ]]; then
    echo "ERROR: Kernel image or DTB not found!" >&2
    exit 1
fi

echo "=== [3/4] Kernel and DTB Built Successfully ==="
echo "Image.gz: $(ls -lh "${IMAGE}" | awk '{print $5, $9}')"
echo "DTB:      $(ls -lh "${DTB}" | awk '{print $5, $9}')"

CMDLINE="console=tty0 console=ttyMSM0,115200n8 earlycon androidboot.hardware=qcom androidboot.memcg=1 androidboot.usbcontroller=a600000.usb loop.max_part=7"

echo "=== [4/4] Mainline boot parameters ==="
echo "Boot command line: ${CMDLINE}"
echo "Use standard AOSP mkbootimg (v2/v3/v4) with stock generic ramdisk to package boot.img."
