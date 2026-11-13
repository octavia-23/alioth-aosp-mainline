# Alioth Mainline Kernel & Userspace Patches

This directory contains standalone, reproducible patch files and sources for building and running Android 17 on Qualcomm SM8250 (Xiaomi Alioth / POCO F3):

### 1. Kernel Patches
- `0001-drm-msm-dpu-sm8250-raise-max-bw-high.patch`: Raises DPU catalog `max_bw_high` to 28.5 Gbps to allow 1080x2400 @ 120Hz client composition without `-E2BIG` errors.
- `0002-turnip-expose-queue-count-2-for-hwui.patch`: Exposes `queueCount = 2` on Turnip Vulkan for Android 17 HWUI RenderThread.
- `0001-dt-bindings-input-awinic-aw86927-Add-Awinic-AW8697.patch`: DT bindings documentation for Awinic AW8697 haptic driver.
- `0002-Input-aw8697-Add-Awinic-AW8697-haptics-driver.patch`: In-tree driver implementation for Awinic AW8697 LRA haptic motor on I2C with clean spinlock/mutex power management.
- `0003-Input-ft8756-Preallocated-SPI-buffers-and-panel-follower.patch`: Preallocated zero-allocation SPI transfer buffers, DRM panel follower binding, and interrupt edge trigger for FocalTech FT8756 touchscreen.
- `0004-dts-alioth-Soundcard-and-touchscreen-panel-link.patch`: Device tree soundcard node configuration and touchscreen panel follower link.

### 2. Audio HAL Patch
- Located in `patches/audio/`: Fixes Qualcomm `libacdbloader.so` null pointer dereference in `acdb_loader_deallocate_ACDB` on mainline Linux.

### 3. Libcutils Ashmem/Memfd Patch
- Located in `patches/cutils/`: Fixes `__ashmem_is_ashmem` in 32-bit and 64-bit `libcutils.so` to prevent seccomp `readlink` EPERM failures in Chromium WebView sandboxed processes.

### 4. Gralloc Adreno Utils Patch
- Located in `patches/gralloc/`: Sets `g_chip_id = 0x06050002` (Adreno 650) in `libadreno_utils.so` (32-bit and 64-bit).

### 5. Packaging & Trampoline Tools
- Located in `tools/`: Source code for freestanding PID 1 trampoline (`preinit1.c`), OVR1 unpacker (`mainline_stage2.c`), daemon stub (`stub_sleeper.c`), and build pipeline (`build_v40.sh`).

---
*Maintained by Akshat Yadav ([@octavia-23](https://github.com/octavia-23))*
