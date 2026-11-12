# Android 17 (SDK 37) on Mainline Linux 6.19 — Xiaomi Alioth (POCO F3 / Mi 11X / Redmi K40)

An upstream-aligned, genuine mainline Linux 6.19 kernel port booting real Android 17 userspace (Clover ROM / AOSP) with hardware display, Vulkan acceleration, and multi-touch interaction.

- **Lead Developer**: Akshat Yadav ([@octavia-23](https://github.com/octavia-23))
- **Kernel Version**: `6.19.6-alioth-octavia23`
- **Target Device**: Xiaomi Alioth / Aliothin (`M2012K11AC`, `M2012K11AG`, `M2012K11AI`)
- **SoC**: Qualcomm SM8250-AC (Snapdragon 870)

---

## 🚀 Hardware Status

| Hardware Block | Subsystem | Status | Notes |
| :--- | :--- | :---: | :--- |
| **CPU** | 8x Kryo 585 (Cortex-A77 + A55) | ✅ Working | Full frequency scaling & SMP active |
| **Display** | Samsung AMS667XX01 120Hz E4 AMOLED | ✅ Working | Mainline DSI DRM/KMS (`ae94000.dsi.0`), full 1080x2400 @ 120Hz |
| **DPU Throughput** | Snapdragon 870 DPU | ✅ Working | Custom bandwidth patch (28.5 Gbps) stopping CRTC atomic commit drops |
| **Touchscreen** | FocalTech FT8756 (FT3518) | ✅ Working | Multi-touch, gesture, and input events routed through `system_server` |
| **GPU / Vulkan** | Qualcomm Adreno 650 | ✅ Working | Mesa 25.0.0 Turnip Vulkan (patched `queueCount = 2`) + Google ANGLE |
| **SurfaceFlinger** | Android Compositor | ✅ Working | Hardware-accelerated client and layer composition |
| **Keystore / Security** | Android KeyMint AIDL | ✅ Working | PureSoftKeyMint V5 userspace provider handling encryption |
| **Storage / Root** | UFS 3.1 (`/dev/block/sda*`) | ✅ Working | F2FS data partition & ext4 first-stage mounts |
| **Backlight** | DSI PWM / Panel Backlight | ✅ Working | Full 10-bit dynamic brightness control |
| **Haptics** | Awinic AW8697 LRA Motor | 🔄 In-Tree | Device tree & driver bound at I2C `0x5a` |

---

## 🛠️ Key Technical Breakthroughs

1. **DPU Bandwidth Ceiling Patch (`dpu_6_0_sm8250.h`)**:
   Standard mainline sets SM8250 `max_bw_high` to 16.6 Gbps. At 1080x2400 @ 120Hz under Android client composition, bandwidth calculations exceeded 17.6 Gbps, causing kernel `-E2BIG` rejections (`crtc89 failed performance check -7`). Raising this threshold to 28.5 Gbps allows full-speed composition without dropped frames.

2. **Mesa Turnip Vulkan Multi-Queue Support**:
   Android HWUI RenderThread requires at least 2 queue families. Patching `tu_device.cc` in Mesa Turnip to expose `queueCount = 2` resolves HWUI assertion aborts.

3. **PureSoftKeyMint V5 AIDL Integration**:
   Bypasses proprietary Qualcomm QSEECOM/QTEE trustzone dependencies on mainline by hosting a pure software KeyMint V5 service directly on AIDL.

4. **Zero-Flashing Safe Trampoline Architecture**:
   Never flashes physical device partitions. Boots entirely via `fastboot boot <image>` using a dual-stage pivot_root trampoline (`mainline_stage2`).

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

# Configure using the official alioth AOSP config
make ARCH=arm64 LLVM=1 alioth_aosp_defconfig

# Compile
make -j$(nproc) ARCH=arm64 LLVM=1 Image qcom/sm8250-xiaomi-alioth.dtb
```

---

## ⚡ Booting via Fastboot

```bash
# Boot into Fastboot mode on your POCO F3 (Volume Down + Power)
fastboot boot dist/boot_clover_aosp_mainline_v30.img
```

---

## 📜 License & Credits

- **Kernel**: Licensed under [GNU General Public License v2.0 (GPL-2.0)](LICENSE).
- **Reference**: Based on upstream SM8250 Linux tree by the postmarketOS community and `nasaarmas`.
- **Author**: Akshat Yadav ([@octavia-23](https://github.com/octavia-23))
