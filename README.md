# Mainline Linux 6.19 for Xiaomi Alioth (POCO F3 / Mi 11X / Redmi K40)

An upstream-aligned, genuine mainline Linux 6.19.6 kernel with a decoupled Qualcomm MSM DPU/DSI display stack and Adreno 650 KGSL GPU driver, designed for AOSP and custom ROM bring-up.

- **Lead Developer**: Akshat Yadav ([@octavia-23](https://github.com/octavia-23))
- **Kernel Base**: Linux 6.19.6
- **Target Device**: Xiaomi Alioth / Aliothin (`M2012K11AC`, `M2012K11AG`, `M2012K11AI`)
- **SoC**: Qualcomm SM8250-AC (Snapdragon 870)
- **Official LineageOS Device Tree**: [octavia-23/lineage-alioth-mainline](https://github.com/octavia-23/lineage-alioth-mainline)

---

## 🚀 Hardware Status & Bring-Up Matrix

| Subsystem | Driver / Hardware | Status | Technical Notes |
| :--- | :--- | :---: | :--- |
| **CPU** | 8x Kryo 585 (Cortex-A77 + A55) | ✅ Working | Full frequency scaling & SMP active |
| **Storage** | UFS 3.1 host controller (`ufshc`) | ✅ Working | Mounts physical `super` partition, F2FS & EROFS |
| **Display** | Samsung AMS667XX01 120Hz E4 AMOLED | ✅ Working | Mainline DSI DRM/KMS (`ae94000.dsi.0`), 1080x2400 @ 120Hz |
| **DPU Throughput** | Snapdragon 870 DPU | ✅ Working | Bandwidth ceiling raised to 28.5 Gbps (`max_bw_high`) |
| **Touchscreen** | FocalTech FT8756 (FT3518) | ✅ Working | Multi-touch and gesture events routed via in-tree driver |
| **GPU / Vulkan** | Qualcomm Adreno 650 | ✅ In-Tree | In-tree KGSL (`/dev/kgsl-3d0`) + Mesa Turnip (`vulkan.adreno.so`) |
| **Keystore** | PureSoftKeyMint V5 AIDL | ✅ Working | Bypasses hardware QSEECom/QTEE trustzone dependencies |
| **Backlight** | DSI PWM / Panel Backlight | ✅ Working | 10-bit dynamic brightness control |
| **Haptics** | Awinic AW8697 LRA Motor | 🔄 In-Tree | Device tree & driver bound at I2C `0x5a` |
| **Wi-Fi** | Qualcomm QCA6390 (`ath11k`) | ❌ Incomplete | Driver & firmware staged; requires AOSP `nl80211` Wi-Fi HAL |
| **Audio** | Qualcomm WCD9385 / CS35L41 | ❌ Missing HAL | Stock CAF HAL expects `/dev/msm_audio_*`; no ALSA HAL configured |
| **Cellular / Calls** | Qualcomm Baseband / RIL | ❌ Stubs Only | `adpl`, `netmgrd`, `qti` are 968-byte stubs; no Hexagon IPC path |
| **Camera** | Spectra 480 ISP | ❌ Stub Only | `android.hardware.camera.provider` is a 968-byte stub |
| **Sensors** | Sensor Hub (SLPI) | ❌ Stub Only | `android.hardware.sensors` is a 968-byte stub |

---

## 🛠️ Key Architectural Implementations

### 1. Decoupled Dual-Stack Display & GPU Linkage
Mainline Linux `msm_drm` normally attempts to bind both the Qualcomm DPU display engine and the Adreno GPU. In this tree:
- `drivers/gpu/drm/msm/Makefile` excludes DRM Adreno compilation when `CONFIG_QCOM_KGSL=y` is active.
- DRM Adreno probe/registration calls are stubbed to no-ops in `msm_gpu.h` and `msm_drv.c`.
- **Result**: `CONFIG_DRM_MSM` exclusively manages DPU KMS and DSI panel output (`/dev/dri/card0`), while `CONFIG_QCOM_KGSL` exclusively manages the Adreno 650 GPU (`/dev/kgsl-3d0`) without symbol clashes or device binding races.

### 2. DPU Bandwidth Ceiling Patch (`dpu_6_0_sm8250.h`)
Stock upstream sets the SM8250 DPU `max_bw_high` to 16.6 Gbps. At 1080x2400 @ 120Hz during Android client composition, bandwidth calculations exceed 17.6 Gbps, causing the kernel to reject atomic commits with `-E2BIG` (`crtc failed performance check -7`). Raising `max_bw_high` to 28.5 Gbps allows full 120Hz frame composition without dropped frames.

### 3. Dual Firmware Search Paths
Mainline remoteproc drivers and the Adreno 650 Zap shader (`a650_zap.mbn`) live under `/vendor/firmware/qcom/sm8250/xiaomi/alioth/`, while stock modem firmware lives in `/vendor/firmware_mnt/image/`. The kernel command line binds both:
```text
firmware_class.path=/vendor/firmware_mnt/image,/vendor/firmware
```

---

## 📦 Building the Kernel

### 1. Prerequisites
```bash
sudo apt update && sudo apt install -y \
    build-essential bison flex libssl-dev libelf-dev dwarves pahole \
    clang lld llvm device-tree-compiler
```

### 2. Compile Kernel Image & DTB
```bash
# Clone the repository
git clone https://github.com/octavia-23/alioth-aosp-mainline.git -b main
cd alioth-aosp-mainline

# Configure using the official alioth AOSP defconfig
make ARCH=arm64 LLVM=1 alioth_aosp_defconfig

# Compile Image and Alioth Device Tree Blob
make -j$(nproc) ARCH=arm64 LLVM=1 Image qcom/sm8250-xiaomi-alioth.dtb
```

---

## 🚀 Deployment & ROM Integration

### Method A: Build from Source via LineageOS Device Tree (Recommended)
Use the official clean-room LineageOS device tree overlay:
👉 **[octavia-23/lineage-alioth-mainline](https://github.com/octavia-23/lineage-alioth-mainline)**

Add the local manifest to your LineageOS / AOSP source tree:
```xml
<!-- .repo/local_manifests/alioth-mainline.xml -->
<manifest>
  <remote name="octavia" fetch="https://github.com/octavia-23" />
  <project name="alioth-aosp-mainline" path="kernel/xiaomi/alioth-mainline" remote="octavia" revision="main" />
  <project name="lineage-alioth-mainline" path="device/xiaomi/alioth-mainline" remote="octavia" revision="main" />
  <project name="LineageOS/android_device_xiaomi_alioth" path="device/xiaomi/alioth" remote="github" />
  <project name="LineageOS/android_device_xiaomi_sm8250-common" path="device/xiaomi/sm8250-common" remote="github" />
</manifest>
```
Then build normally:
```bash
source build/envsetup.sh
lunch lineage_alioth_mainline-userdebug
m bacon
```

### Method B: Universal Boot Image & AnyKernel3 Drop-In
- **Universal Boot Image**: Flashable via `fastboot flash boot boot_alioth_universal.img`.
- **AnyKernel3 Package**: Flashes over existing boot images via custom recovery.
- **Requirement**: The target ROM userspace **must** include a DRM-KMS hardware composer (`android.hardware.composer.hwc3-service.drm`) and Turnip Vulkan. It is incompatible with stock Qualcomm CAF display HALs (`hwcomposer.qcom.so`).

*(Note: The early v30 prototype booted via a dual-stage pivot_root RAM trampoline on Android 17 / Clover AOSP; this has been superseded by the native overlay and universal boot image above).*

---

## 📜 License & Credits

- **Kernel**: Licensed under [GNU General Public License v2.0 (GPL-2.0)](COPYING).
- **Device Tree Overlay**: Licensed under [Apache-2.0](https://github.com/octavia-23/lineage-alioth-mainline/blob/main/LICENSE).
- **Reference**: Based on upstream SM8250 Linux tree by the postmarketOS community.
- **Author**: Akshat Yadav ([@octavia-23](https://github.com/octavia-23))
