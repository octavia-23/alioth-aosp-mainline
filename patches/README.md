# Alioth Mainline Kernel & Userspace Patches

This directory contains standalone, reproducible patch files for building and running Android 17 on Qualcomm SM8250 (Xiaomi Alioth / POCO F3):

### 1. `0001-drm-msm-dpu-sm8250-raise-max-bw-high.patch`
- **Subsystem**: Linux Kernel (`drivers/gpu/drm/msm/disp/dpu1`)
- **Purpose**: Raises the SM8250 DPU catalog `max_bw_high` threshold from 16.6 Gbps to 28.5 Gbps. Prevents kernel `-E2BIG` rejections (`crtc failed performance check -7`) during 1080x2400 @ 120Hz client composition.

### 2. `0002-turnip-expose-queue-count-2-for-hwui.patch`
- **Subsystem**: Mesa Vulkan Driver (`src/freedreno/vulkan/tu_device.cc`)
- **Purpose**: Exposes `queueCount = 2` on Turnip Vulkan. Satisfies Android 17 HWUI RenderThread requirements and stops crashes on startup.

### 3. `0001-dt-bindings-input-awinic-aw86927-Add-Awinic-AW8697.patch`
- **Subsystem**: Device Tree Bindings (`Documentation/devicetree/bindings/input/`)
- **Purpose**: Adds device tree binding documentation for the Awinic AW8697 haptic driver.

### 4. `0002-Input-aw8697-Add-Awinic-AW8697-haptics-driver.patch`
- **Subsystem**: Linux Input Driver (`drivers/input/misc/aw8697.c`)
- **Purpose**: In-tree driver implementation for the Awinic AW8697 LRA haptic motor on I2C.

---
*Maintained by Akshat Yadav ([@octavia-23](https://github.com/octavia-23))*
