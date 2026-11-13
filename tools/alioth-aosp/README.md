# Mainline Trampoline & Packaging Pipeline

- **`trampoline/preinit1.c`**: Pure C freestanding static init trampoline executed as PID 1. Configures early devtmpfs, serial console, mounts Android rootfs, and hands off to stage 2.
- **`trampoline/mainline_stage2.c`**: Extracts in-memory OVR1 vendor overlay (`mainline_vendor_ovr.bin`), sets up loopback devices, and mounts overlayfs over `/vendor`.
- **`trampoline/stub_sleeper.c`**: 968-byte static sleeper binary that replaces dormant Qualcomm proprietary vendor daemons (e.g. `qseecomd`, `netmgrd`, `qti`) to satisfy init service dependencies without crashes.
- **`pack_ovr_v40.py`**: Deterministic Python packager for OVR1 archive with correct SELinux file contexts and permissions.
- **`build_v40.sh`**: Complete end-to-end boot image build pipeline producing `boot_clover_aosp_mainline_v40.img`.
