#!/usr/bin/env bash
set -euo pipefail

WORKSPACE="/home/aksha/alioth-mainline-workspace"
NDK="$WORKSPACE/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin"
TRAMP="$WORKSPACE/tools/trampoline"
CLOVER="$WORKSPACE/references/clover"
STAGING="$CLOVER/combined_staging_v31"
PREVIEW="$CLOVER/ovr_preview_v31"
DIST="$WORKSPACE/dist/aosp"
HOST_DIST="/mnt/c/Users/aksha/Desktop/alioth mainline/dist/aosp"
BUILD="$WORKSPACE/kernel/build"

echo "=== 1. Verifying Kernel & Source Payloads ==="
test -s "$BUILD/arch/arm64/boot/Image"
test -s "$BUILD/arch/arm64/boot/dts/qcom/sm8250-xiaomi-alioth.dtb"
test -s "$WORKSPACE/turnip_out/vulkan.adreno.so"
test -s "$WORKSPACE/turnip_out_arm/vulkan.adreno.so"
test -s "$WORKSPACE/patches/gralloc/libadreno_utils_64.orig.so"
test -s "$WORKSPACE/patches/gralloc/libadreno_utils_32.orig.so"
test -s "$WORKSPACE/tools/keymint/android.hardware.security.keymint-service"
test -s "$CLOVER/libdrmutils_patched.so"
test -s "$CLOVER/libsdedrm_patched.so"
test -s "$CLOVER/boot_qti_patched"

echo "=== 2. Deterministically Generating Patched libadreno_utils.so (64-bit & 32-bit, Adreno 650 = 0x06050002) ==="
python3 - << 'PY'
import hashlib, os, struct, subprocess

WS = "/home/aksha/alioth-mainline-workspace"
READELF = os.path.join(WS, "android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf")

specs = [
    (
        "64-bit",
        os.path.join(WS, "patches/gralloc/libadreno_utils_64.orig.so"),
        os.path.join(WS, "patches/gralloc/patched/lib64/libadreno_utils.so"),
        "075f3c073568cc41429a69739882b1d091d60a2390cddd5cdeeef2114fd8af00",
        0x13268,
    ),
    (
        "32-bit",
        os.path.join(WS, "patches/gralloc/libadreno_utils_32.orig.so"),
        os.path.join(WS, "patches/gralloc/patched/lib/libadreno_utils.so"),
        "5686bd3c6c70286e35c995615f42d76de417d938e87b01622bbf030e8094de53",
        0x0dcc4,
    ),
]

for arch, orig_p, out_p, expected_sha, expected_off in specs:
    with open(orig_p, "rb") as f:
        data = bytearray(f.read())
    actual_sha = hashlib.sha256(data).hexdigest()
    assert actual_sha == expected_sha, f"{arch} orig SHA mismatch: {actual_sha} != {expected_sha}"

    # Verify .data section offset via llvm-readelf
    sechdr = subprocess.check_output([READELF, "-S", orig_p], text=True)
    found_off = None
    for line in sechdr.splitlines():
        if ".data " in line and ".data." not in line:
            cols = line.split()
            # Find PROGBITS column and grab Offset two columns later: Address Off Size
            idx = cols.index("PROGBITS")
            found_off = int(cols[idx + 2], 16)
            break
    assert found_off == expected_off, f"{arch} .data offset mismatch: {hex(found_off or 0)} != {hex(expected_off)}"

    cur_val = struct.unpack_from("<i", data, expected_off)[0]
    assert cur_val == -1, f"{arch} g_chip_id at {hex(expected_off)} expected -1, got {cur_val}"
    struct.pack_into("<I", data, expected_off, 0x06050002)

    os.makedirs(os.path.dirname(out_p), exist_ok=True)
    with open(out_p, "wb") as f:
        f.write(data)
    os.chmod(out_p, 0o644)
    print(f"{arch}: patched .data@{hex(expected_off)} g_chip_id (-1 -> 0x06050002) -> {out_p}")
PY

echo "=== 3. Compiling Trampolines & stub_sleeper (-Wall -Wextra -Werror) ==="
cd "$TRAMP"
"$NDK/aarch64-linux-android35-clang" -O2 -static -nostdlib -fno-builtin -Wall -Wextra -Werror stub_sleeper.c -o stub_sleeper
"$NDK/llvm-strip" stub_sleeper
"$NDK/aarch64-linux-android35-clang" -O2 -static -nostdlib -fno-builtin -Wall -Wextra -Werror preinit1.c -o preinit1
"$NDK/llvm-strip" preinit1
"$NDK/aarch64-linux-android35-clang" -O2 -static -nostdlib -fno-builtin -Wall -Wextra -Werror mainline_stage2.c -o mainline_stage2
"$NDK/llvm-strip" mainline_stage2

echo "=== 4. Creating Clean Staging Directory v31 ==="
rm -rf "$STAGING" "$PREVIEW"
mkdir -p "$STAGING" "$PREVIEW" "$DIST"

cp -a "$CLOVER/vendor_ramdisk/." "$STAGING/"
rm -f "$STAGING/init"

cp "$TRAMP/preinit1" "$STAGING/init"
chmod 0755 "$STAGING/init"

cp "$TRAMP/mainline_stage2" "$STAGING/mainline_stage2"
chmod 0755 "$STAGING/mainline_stage2"

cp -a "$CLOVER/boot_ramdisk/init" "$STAGING/init.aosp"
chmod 0755 "$STAGING/init.aosp"
python3 - << 'PY'
path = "/home/aksha/alioth-mainline-workspace/references/clover/combined_staging_v31/init.aosp"
with open(path, "rb") as f:
    data = bytearray(f.read())
old_str = b"/system/bin/init\x00"
new_str = b"/postinstall/ini\x00"
idx = data.find(old_str)
assert idx != -1, "old_str not found in init.aosp"
data[idx:idx+len(old_str)] = new_str
with open(path, "wb") as f:
    f.write(data)
print("Patched init.aosp -> /postinstall/ini cleanly")
PY

echo "=== 5. Packing OVR1 Bundle v31 & Fstab / Props ==="
python3 "$WORKSPACE/pack_ovr_v31.py" "$STAGING/mainline_vendor_ovr.bin" "$PREVIEW"
chmod 0644 "$STAGING/mainline_vendor_ovr.bin"

python3 - << 'PY'
import struct
with open("/home/aksha/alioth-mainline-workspace/references/clover/combined_staging_v31/data_sparse.bin", "wb") as out:
    out.write(b"DATS")
    out.write(struct.pack("<III", 4096, 262144, 0))
PY
chmod 0644 "$STAGING/data_sparse.bin"

mkdir -p "$STAGING/system/etc" "$STAGING/first_stage_ramdisk/system/etc"
cp "$PREVIEW/etc/fstab.qcom" "$STAGING/system/etc/fstab.qcom"
cp "$PREVIEW/etc/fstab.qcom" "$STAGING/first_stage_ramdisk/system/etc/fstab.qcom"
cp "$PREVIEW/etc/selinux/vendor_file_contexts" "$STAGING/vendor_file_contexts"

: > "$STAGING/force_debuggable"
: > "$STAGING/first_stage_ramdisk/force_debuggable"
python3 - << 'PY'
prop_path = "/home/aksha/alioth-mainline-workspace/references/clover/combined_staging_v31/prop.default"
overrides = {
    "ro.crypto.state": "unencrypted",
    "ro.crypto.type": "none",
    "ro.secure": "0",
    "ro.adb.secure": "0",
    "ro.debuggable": "1",
    "ro.force.debuggable": "1",
    "vendor.usb.use_gadget_hal": "0",
    "persist.sys.usb.config": "adb",
    "persist.vendor.usb.config": "adb",
    "ro.surface_flinger.use_vulkan": "true",
    "debug.renderengine.backend": "skiavkthreaded",
    "ro.hwui.use_vulkan": "true",
    "dalvik.vm.usejit": "true",
    "sys.use_memfd": "true",
    "ro.hardware.egl": "angle",
    "persist.graphics.egl": "angle",
    "ro.gfx.angle.supported": "true",
    "vendor.gralloc.disable_ubwc": "1",
    "debug.gralloc.gfx_ubwc_disable": "1",
}
with open(prop_path, "r") as f:
    lines = f.read().splitlines()

seen = set()
out = []
for line in lines:
    stripped = line.strip()
    if stripped and not stripped.startswith("#") and "=" in line:
        k, _ = line.split("=", 1)
        k = k.strip()
        if k in overrides:
            if k in seen:
                continue
            seen.add(k)
            out.append(f"{k}={overrides[k]}")
            continue
    out.append(line)

for k, v in overrides.items():
    if k not in seen:
        out.append(f"{k}={v}")

with open(prop_path, "w") as f:
    f.write("\n".join(out) + "\n")
print("Patched prop.default cleanly (", len(overrides), "overrides applied )")
PY

cat << 'EOF' > "$STAGING/adb_debug.prop"
ro.crypto.state=unencrypted
ro.crypto.type=none
ro.secure=0
ro.adb.secure=0
ro.debuggable=1
ro.force.debuggable=1
persist.sys.usb.config=adb
persist.vendor.usb.config=adb
sys.usb.config=adb
vendor.usb.use_gadget_hal=0
ro.surface_flinger.use_vulkan=true
debug.renderengine.backend=skiavkthreaded
ro.hwui.use_vulkan=true
dalvik.vm.usejit=true
sys.use_memfd=true
ro.hardware.egl=angle
persist.graphics.egl=angle
ro.gfx.angle.supported=true
vendor.gralloc.disable_ubwc=1
debug.gralloc.gfx_ubwc_disable=1
EOF
cp "$STAGING/adb_debug.prop" "$STAGING/first_stage_ramdisk/adb_debug.prop"

echo "=== 6. Packing combined_ramdisk_v31.cpio.gz ==="
RAMDISK="$CLOVER/combined_ramdisk_v31.cpio.gz"
cd "$STAGING"
find . -mindepth 1 | sort | cpio -o -H newc -R 0:0 2>/dev/null | gzip -9 > "$RAMDISK"
ls -lh "$RAMDISK"

echo "=== 7. Packaging boot_clover_aosp_mainline_v31.img ==="
IMAGE_RAW="$BUILD/arch/arm64/boot/Image"
DTB="$BUILD/arch/arm64/boot/dts/qcom/sm8250-xiaomi-alioth.dtb"
OUT="$DIST/boot_clover_aosp_mainline_v31.img"

CMDLINE="clk_ignore_unused pd_ignore_unused arm-smmu.disable_bypass=0 swiotlb=2048 lpm_levels.sleep_disabled=1 service_locator.enable=1 androidboot.usbcontroller=a600000.dwc3 androidboot.usbconfigfs=true loop.max_part=7 cgroup.memory=nokmem,nosocket androidboot.hardware=qcom androidboot.fstab_suffix=qcom androidboot.boot_devices=soc/1d84000.ufshc,soc@0/1d84000.ufshc,1d84000.ufshc androidboot.selinux=permissive panic=0 oops=panic printk.always_kmsg_dump=1 loglevel=8 sysctl.kernel.firmware_config.force_sysfs_fallback=1 firmware_class.path=/vendor/firmware_mnt/image"

mkbootimg \
    --header_version 2 \
    --os_version 17.0.0 \
    --os_patch_level 2026-10 \
    --kernel "$IMAGE_RAW" \
    --ramdisk "$RAMDISK" \
    --dtb "$DTB" \
    --pagesize 4096 \
    --base 0x00000000 \
    --kernel_offset 0x00008000 \
    --ramdisk_offset 0x20000000 \
    --second_offset 0x00000000 \
    --tags_offset 0x00000100 \
    --dtb_offset 0x22000000 \
    --cmdline "$CMDLINE" \
    --output "$OUT"

ls -lh "$OUT"
mkdir -p "$HOST_DIST"
cp "$OUT" "$HOST_DIST/boot_clover_aosp_mainline_v31.img"
ls -lh "$HOST_DIST/boot_clover_aosp_mainline_v31.img"
echo "=== SUCCESS: boot_clover_aosp_mainline_v31.img built! ==="
