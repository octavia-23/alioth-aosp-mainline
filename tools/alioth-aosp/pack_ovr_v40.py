REMOTEPROC_RC = """# Auto-start Hexagon DSP remoteprocs and Mainline Boot Fix
service vendor.mainline_boot /vendor/bin/mainline_boot.sh
    class core
    user root
    group root system
    seclabel u:r:vendor_init:s0
    oneshot
    disabled

on post-fs-data
    write /sys/devices/platform/soc@0/17300000.remoteproc/remoteproc/remoteproc2/state start
    write /sys/devices/platform/soc@0/5c00000.remoteproc/remoteproc/remoteproc0/state start
    write /sys/devices/platform/soc@0/8300000.remoteproc/remoteproc/remoteproc1/state start
    start vendor.mainline_boot
"""

MAINLINE_BOOT_SH = """#!/system/bin/sh
# Alioth Mainline: Automated LockSettings & KeyMint Cleanup
# PureSoftKeyMint is software-backed with an ephemeral master key.
# Stale SyntheticPassword blobs cause LockSettingsService to crash in an infinite loop.
rm -rf /data/system_de/0/spblob/*
rm -f /data/system/locksettings.db*
rm -f /data/misc/keystore/persistent.sqlite
mkdir -p -m 0700 /data/system_de/0/spblob
chown system:system /data/system_de/0/spblob

# 1. Wi-Fi HAL driver state param
ln -sf /dev/null /dev/wlan

# 2. System-wide ashmem-compatible libcutils bind mount (32-bit & 64-bit)
mount -o bind /vendor/lib/libcutils.so /system/lib/libcutils.so
mount -o bind /vendor/lib64/libcutils.so /system/lib64/libcutils.so

# 3. HWUI rendering configuration (disable partial updates to prevent damage region abort)
setprop debug.hwui.use_partial_updates false

# 4. Display backlight & brightness
echo 1024 > /sys/class/backlight/ae94000.dsi.0/brightness 2>/dev/null || true

# Background worker: Wait for system boot completion, unlock display, configure brightness & power
(
    while [ "$(getprop sys.boot_completed)" != "1" ]; do
        sleep 1
    done
    sleep 2
    /system/bin/settings put system screen_brightness 255
    /system/bin/settings put system screen_off_timeout 2147483647
    /system/bin/svc power stayon true
    /system/bin/echo 1024 > /sys/class/backlight/ae94000.dsi.0/brightness 2>/dev/null || true
    /system/bin/input keyevent 82
    /system/bin/input swipe 540 2000 540 500 200
    /system/bin/wm dismiss-keyguard
) &
"""

#!/usr/bin/env python3
"""Build clean vendor overlay tree and pack into OVR1 archive."""

import os
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

WS = "/home/aksha/alioth-mainline-workspace"
CLOVER = os.path.join(WS, "references", "clover")
VENDOR_IMG = os.path.join(CLOVER, "extracted", "vendor_mainline.img")
OUT_BIN = sys.argv[1]
OVR_TREE_PREVIEW = sys.argv[2]

DIR_CTXS = {
    "lib": "u:object_r:vendor_file:s0",
    "lib/hw": "u:object_r:vendor_hal_file:s0",
    "lib64": "u:object_r:vendor_file:s0",
    "lib64/hw": "u:object_r:vendor_hal_file:s0",
    "bin": "u:object_r:vendor_file:s0",
    "bin/hw": "u:object_r:vendor_file:s0",
    "etc": "u:object_r:vendor_configs_file:s0",
    "etc/init": "u:object_r:vendor_configs_file:s0",
    "etc/init/hw": "u:object_r:vendor_configs_file:s0",
    "etc/selinux": "u:object_r:vendor_configs_file:s0",
    "etc/vintf": "u:object_r:vendor_configs_file:s0",
    "etc/vintf/manifest": "u:object_r:vendor_configs_file:s0",
    "firmware": "u:object_r:vendor_firmware_file:s0",
    "firmware/qcom": "u:object_r:vendor_firmware_file:s0",
    "firmware/qcom/sm8250": "u:object_r:vendor_firmware_file:s0",
    "firmware/qcom/sm8250/xiaomi": "u:object_r:vendor_firmware_file:s0",
    "firmware/qcom/sm8250/xiaomi/alioth": "u:object_r:vendor_firmware_file:s0",
    "firmware/ath11k": "u:object_r:vendor_firmware_file:s0",
    "firmware/ath11k/QCA6390": "u:object_r:vendor_firmware_file:s0",
    "firmware/ath11k/QCA6390/hw2.0": "u:object_r:vendor_firmware_file:s0",
    "firmware/qca": "u:object_r:vendor_firmware_file:s0",
}

def read_vendor_file(rel_path: str) -> str:
    cmd = ["debugfs", "-R", f"cat {rel_path}", VENDOR_IMG]
    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    if not res.stdout:
        raise RuntimeError(f"Empty or missing file in vendor_mainline.img: {rel_path}")
    return res.stdout

def verify_vendor_exists(rel_path: str) -> None:
    res = subprocess.run(["debugfs", "-R", f"stat {rel_path}", VENDOR_IMG],
                         capture_output=True, text=True, check=True)
    if "Inode:" not in res.stdout:
        raise RuntimeError(f"Missing expected base vendor asset: {rel_path}")

def strip_service_block(text: str, service_name: str) -> str:
    out = []
    skipping = False
    prefix = f"service {service_name} "
    for line in text.splitlines(keepends=True):
        if skipping:
            if line.strip() == "" or (line[0] not in (" ", "\t") and not line.startswith("#")):
                skipping = False
                if line.strip() == "":
                    continue
            else:
                continue
        if line.startswith(prefix) or line.strip() == f"service {service_name}":
            skipping = True
            continue
        out.append(line)
    return "".join(out)

def transform_init_qcom_rc(text: str) -> str:
    text = strip_service_block(text, "vendor.qti.hardware.display.composer")
    lines = []
    for line in text.splitlines(keepends=True):
        s = line.strip()
        if s == "start vendor.hwcomposer-2-1":
            continue
        if s == "onrestart restart surfaceflinger":
            continue
        lines.append(line)
    out = "".join(lines)
    assert "vendor.qti.hardware.display.composer" not in out
    assert "vendor.hwcomposer-2-1" not in out
    assert "onrestart restart surfaceflinger" not in out
    return out

def transform_allocator_rc(text: str) -> str:
    lines = [l for l in text.splitlines(keepends=True)
             if l.strip() != "onrestart restart surfaceflinger"]
    out = "".join(lines)
    assert "onrestart restart surfaceflinger" not in out
    return out

def disable_rc_services(
    text: str,
    target_services: set[str] | None = None,
    target_starts: set[str] | None = None,
) -> str:
    """Disable services (all or named) in an .rc file and comment out matching 'start <svc>' triggers."""
    out_lines = []
    in_service = False
    service_should_disable = False
    service_has_disabled = False
    service_lines = []
    matched_services: set[str] = set()
    matched_starts: set[str] = set()
    svc_count = 0

    def flush_service():
        nonlocal service_lines, service_has_disabled, service_should_disable
        if not service_lines:
            return
        if service_should_disable and not service_has_disabled:
            out_lines.append(service_lines[0])
            out_lines.append("    disabled\n")
            out_lines.extend(service_lines[1:])
        else:
            out_lines.extend(service_lines)
        service_lines = []
        service_has_disabled = False
        service_should_disable = False

    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        is_top = bool(line) and (line[0] not in (" ", "\t", "#")) and stripped != ""
        if in_service:
            if is_top or stripped == "":
                flush_service()
                in_service = False
            else:
                if stripped == "disabled":
                    service_has_disabled = True
                service_lines.append(line)
                continue

        if line.startswith("service "):
            parts = stripped.split()
            svc_name = parts[1] if len(parts) >= 2 else ""
            in_service = True
            service_has_disabled = False
            service_should_disable = (target_services is None) or (svc_name in target_services)
            if service_should_disable:
                svc_count += 1
                matched_services.add(svc_name)
            service_lines = [line]
            continue

        if stripped.startswith("start "):
            parts = stripped.split()
            svc_name = parts[1] if len(parts) >= 2 else ""
            if (target_starts is None) or (svc_name in target_starts):
                indent = line[: len(line) - len(line.lstrip())]
                out_lines.append(f"{indent}# mainline disabled: {stripped}\n")
                matched_starts.add(svc_name)
                continue

        if target_starts is None and stripped.startswith("wait "):
            indent = line[: len(line) - len(line.lstrip())]
            out_lines.append(f"{indent}# mainline disabled wait: {stripped}\n")
            continue

        out_lines.append(line)

    if in_service:
        flush_service()

    assert svc_count > 0, "Expected at least one matching service block in .rc"
    if target_services is not None:
        missing = target_services - matched_services
        assert not missing, f"Target services missing in .rc: {missing}"
    if target_starts is not None:
        missing_starts = target_starts - matched_starts
        assert not missing_starts, f"Target start triggers missing in .rc: {missing_starts}"
    return "".join(out_lines)


def transform_vendor_build_prop(text: str) -> str:
    """Override /vendor/build.prop vendor-owned graphics properties cleanly (system/sys props live in prop.default)."""
    overrides = {
        "ro.hardware.egl": "angle",
        "ro.gfx.angle.supported": "true",
        "vendor.gralloc.disable_ubwc": "1",
        "ro.hwui.use_vulkan": "false",
        "debug.hwui.renderer": "skiagl",
        "debug.hwui.use_partial_updates": "false",
    }
    seen = set()
    out_lines = []
    for line in text.splitlines(keepends=True):
        s = line.strip()
        if s and not s.startswith("#") and "=" in s:
            k = s.split("=", 1)[0].strip()
            if k in overrides:
                out_lines.append(f"{k}={overrides[k]}\n")
                seen.add(k)
                continue
        out_lines.append(line)
    if out_lines and not out_lines[-1].endswith("\n"):
        out_lines[-1] += "\n"
    for k, v in overrides.items():
        if k not in seen:
            out_lines.append(f"{k}={v}\n")
    out = "".join(out_lines)
    for k, v in overrides.items():
        assert f"{k}={v}\n" in out, f"Missing {k}={v} in /vendor/build.prop"
    return out

def transform_boringssl_rc(text: str) -> str:
    lines = [l for l in text.splitlines(keepends=True) if "reboot_on_failure" not in l]
    out = "".join(lines)
    assert "reboot_on_failure" not in out
    return out

def transform_vendor_manifest_xml(text: str) -> str:
    drop_hals = (
        "android.hardware.gatekeeper",
        "android.hardware.keymaster",
        "vendor.qti.hardware.qseecom",
        "vendor.qti.hardware.qteeconnector",
        "vendor.qti.hardware.soter",
        "vendor.qti.hardware.tui_comm",
        "vendor.xiaomi.hardware.mlipay",
        "android.hardware.sensors",
    )
    pattern = re.compile(r"\s*<hal\b[^>]*>.*?</hal>", re.DOTALL)
    def repl(m):
        blk = m.group(0)
        for h in drop_hals:
            if f"<name>{h}</name>" in blk:
                return ""
        return blk
    out = pattern.sub(repl, text)
    ET.fromstring(out)
    for h in drop_hals:
        assert h not in out, f"HAL {h} still present in manifest.xml!"
    return out

def transform_vendor_fc(text: str) -> str:
    extra = (
        "\n# Alioth Mainline overlay additions\n"
        "/postinstall(/.*)?                                         u:object_r:system_file:s0\n"
        "/postinstall/ini                                           u:object_r:init_exec:s0\n"
    )
    if "/postinstall/ini" not in text:
        text = text.rstrip() + "\n" + extra
    return text

def build_fstab_qcom() -> str:
    return (
        "system                                                  /system                ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
        "system_ext                                              /system_ext            ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
        "product                                                 /product               ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
        "vendor                                                  /vendor                ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
        "odm                                                     /odm                   ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
        "/dev/block/by-name/metadata                             /metadata              ext4    noatime,nosuid,nodev,discard                         wait,check,formattable\n"
        "/dev/block/by-name/userdata                             /data                  f2fs    noatime,nosuid,nodev,discard                         latemount,wait,check,formattable,quota\n"
        "/dev/block/by-name/persist                              /mnt/vendor/persist    ext4    noatime,nosuid,nodev,barrier=1                       wait,check\n"
        "/dev/block/by-name/misc                                 /misc                  emmc    defaults                                             defaults\n"
        "/devices/platform/soc@0/*.ssusb/*.dwc3/xhci-hcd.*.auto* /storage/usbotg        vfat    nosuid,nodev                                         wait,voldmanaged=usbotg:auto\n"
        "/dev/block/by-name/spunvm                               /mnt/vendor/spunvm     vfat    rw,shortname=lower,uid=1000,gid=1000,dmask=007,fmask=007,context=u:object_r:vendor_spunvm_file:s0 wait\n"
        "/dev/block/by-name/modem                                /vendor/firmware_mnt   vfat    ro,shortname=lower,uid=1000,gid=1000,dmask=227,fmask=337,context=u:object_r:firmware_file:s0 wait,slotselect\n"
        "/dev/block/by-name/dsp                                  /vendor/dsp            ext4    ro,nosuid,nodev,barrier=1                            wait,slotselect\n"
        "/dev/block/by-name/bluetooth                            /vendor/bt_firmware    vfat    ro,shortname=lower,uid=1002,gid=3002,dmask=227,fmask=337,context=u:object_r:bt_firmware_file:s0 wait,slotselect\n"
        "/dev/block/zram0                                        none                   swap    defaults                                             zramsize=4294967296,max_comp_streams=8\n"
    )

KEYMINT_RC = """service vendor.keymint-default /vendor/bin/hw/android.hardware.security.keymint-service
    class early_hal
    interface aidl android.hardware.security.keymint.IKeyMintDevice/default
    interface aidl android.hardware.security.keymint.IRemotelyProvisionedComponent/default
    interface aidl android.hardware.security.sharedsecret.ISharedSecret/default
    interface aidl android.hardware.security.secureclock.ISecureClock/default
    user system
    group system drmrpc
    seclabel u:r:hal_keymint_default:s0
"""

KEYMINT_XML = """<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>android.hardware.security.keymint</name>
        <version>5</version>
        <interface>
            <name>IKeyMintDevice</name>
            <instance>default</instance>
        </interface>
    </hal>
    <hal format="aidl">
        <name>android.hardware.security.keymint</name>
        <version>3</version>
        <interface>
            <name>IRemotelyProvisionedComponent</name>
            <instance>default</instance>
        </interface>
    </hal>
    <hal format="aidl">
        <name>android.hardware.security.sharedsecret</name>
        <version>1</version>
        <interface>
            <name>ISharedSecret</name>
            <instance>default</instance>
        </interface>
    </hal>
    <hal format="aidl">
        <name>android.hardware.security.secureclock</name>
        <version>1</version>
        <interface>
            <name>ISecureClock</name>
            <instance>default</instance>
        </interface>
    </hal>
</manifest>
"""

HWC3_RC = """service vendor.hwcomposer-3 /vendor/bin/hw/android.hardware.composer.hwc3-service.drm
    class early_hal animation
    interface aidl android.hardware.graphics.composer3.IComposer/default
    user system
    group graphics drmrpc
    capabilities SYS_NICE
    seclabel u:r:hal_graphics_composer_default:s0
    onrestart restart surfaceflinger
    task_profiles ServiceCapacityLow
"""

HWC3_XML = """<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>android.hardware.graphics.composer3</name>
        <version>5</version>
        <interface>
            <name>IComposer</name>
            <instance>default</instance>
        </interface>
    </hal>
</manifest>
"""

EMPTY_MANIFEST_XML = '<manifest version="9.0" type="device"/>\n'

def main():
    # Confirm base vendor already owns HWC3 V5 unmodified
    # HWC3 provided self-contained in overlay

    ET.fromstring(KEYMINT_XML)
    ET.fromstring(EMPTY_MANIFEST_XML)

    # (rel_path, mode, ctx, bytes_data)
    files = []

    def add_bytes(rel_path: str, mode: int, ctx: str, data: bytes):
        assert not rel_path.startswith("/")
        files.append((rel_path, mode, ctx, data))

    def add_file(rel_path: str, mode: int, ctx: str, src_path: str):
        with open(src_path, "rb") as f:
            data = f.read()
        assert len(data) > 0, f"Empty source binary/lib: {src_path}"
        add_bytes(rel_path, mode, ctx, data)

    # 1. Configs / VINTF / RCs
    add_bytes("etc/fstab.qcom", 0o644, "u:object_r:vendor_configs_file:s0",
              build_fstab_qcom().encode())
    add_bytes("etc/selinux/vendor_file_contexts", 0o644, "u:object_r:vendor_configs_file:s0",
              transform_vendor_fc(read_vendor_file("/etc/selinux/vendor_file_contexts")).encode())
    add_bytes("etc/init/hw/init.qcom.rc", 0o644, "u:object_r:vendor_configs_file:s0",
              transform_init_qcom_rc(read_vendor_file("/etc/init/hw/init.qcom.rc")).encode())
    add_bytes("etc/init/vendor.qti.hardware.display.allocator-service.rc", 0o644, "u:object_r:vendor_configs_file:s0",
              transform_allocator_rc(read_vendor_file("/etc/init/vendor.qti.hardware.display.allocator-service.rc")).encode())
    add_bytes("build.prop", 0o644, "u:object_r:vendor_file:s0",
              transform_vendor_build_prop(read_vendor_file("/build.prop")).encode())
    add_bytes("etc/init/hw/init.target.rc", 0o644, "u:object_r:vendor_configs_file:s0",
              disable_rc_services(
                  read_vendor_file("/etc/init/hw/init.target.rc"),
                  target_services={"vendor.lowi", "vendor.per_mgr", "vendor.per_proxy", "vendor.nv_mac"},
                  target_starts={"vendor.sensors", "vendor.per_proxy"},
              ).encode())
    for disabled_rc in (
        "etc/init/vendor.qti.hardware.qteeconnector@1.0-service.rc",
        "etc/init/vendor.qti.hardware.soter@1.0-service.rc",
        "etc/init/feature_enabler_client.rc",
        "etc/init/ssgtzd.rc",
        "etc/init/vendor.xiaomi.hardware.mlipay@1.1-service.rc",
        "etc/init/vendor.qti.hardware.tui_comm@1.0-service-qti.rc",
        "etc/init/vendor.qti.adsprpc-service.rc",
        "etc/init/vendor.qti.cdsprpc-service.rc",
        "etc/init/vendor.sensors.sscrpcd.rc",
        "etc/init/cnd.rc",
        "etc/init/vendor.qti.hardware.dsp@1.0-service.rc",
        "etc/init/android.hardware.keymaster@4.0-service-qti.rc",
        "etc/init/android.hardware.gatekeeper@1.0-service-qti.rc",
        "etc/init/vendor.qti.hardware.qseecom@1.0-service.rc",
        "etc/init/qseecomd.rc",
        "etc/init/vendor.display.color@1.0-service.rc",
        "etc/init/vendor.qti.tftp.rc",
        "etc/init/init.time_daemon.rc",
        "etc/init/init.mi_thermald.rc",
        "etc/init/netmgrd.rc",
        "etc/init/android.hardware.biometrics.fingerprint-service.xiaomi.rc",
        "etc/init/android.hardware.sensors@1.0-service.rc",
    ):
        add_bytes(disabled_rc, 0o644, "u:object_r:vendor_configs_file:s0",
                  disable_rc_services(read_vendor_file("/" + disabled_rc)).encode())
    add_bytes("etc/init/boringssl_self_test.rc", 0o644, "u:object_r:vendor_configs_file:s0",
              transform_boringssl_rc(read_vendor_file("/etc/init/boringssl_self_test.rc")).encode())
    add_bytes("etc/init/android.hardware.security.keymint-service.rc", 0o644, "u:object_r:vendor_configs_file:s0",
              KEYMINT_RC.encode())
    add_bytes("etc/vintf/manifest.xml", 0o644, "u:object_r:vendor_configs_file:s0",
              transform_vendor_manifest_xml(read_vendor_file("/etc/vintf/manifest.xml")).encode())
    add_bytes("etc/vintf/manifest/android.hardware.security.keymint-service.xml", 0o644, "u:object_r:vendor_configs_file:s0",
              KEYMINT_XML.encode())
    add_bytes("etc/vintf/manifest/vendor.qti.hardware.display.composer-service.xml", 0o644, "u:object_r:vendor_configs_file:s0",
              EMPTY_MANIFEST_XML.encode())
    # Add Wi-Fi QCA6390 ath11k firmware
    ath_dir = os.path.join(WS, "firmware/ath11k/QCA6390/hw2.0")
    if os.path.exists(ath_dir):
        for fw in sorted(os.listdir(ath_dir)):
            fp = os.path.join(ath_dir, fw)
            if os.path.isfile(fp):
                add_file(f"firmware/ath11k/QCA6390/hw2.0/{fw}", 0o644, "u:object_r:vendor_firmware_file:s0", fp)

    # Add Bluetooth QCA firmware
    qca_dir = os.path.join(WS, "firmware/qca")
    if os.path.exists(qca_dir):
        for fw in sorted(os.listdir(qca_dir)):
            fp = os.path.join(qca_dir, fw)
            if os.path.isfile(fp):
                add_file(f"firmware/qca/{fw}", 0o644, "u:object_r:vendor_firmware_file:s0", fp)

    # Add squashed DSP firmware
    fw_dir = os.path.join("/home/aksha/alioth-mainline-workspace", "firmware/qcom/sm8250/xiaomi/alioth")
    for mbn in ["adsp.mbn", "cdsp.mbn", "slpi.mbn", "a650_zap.mbn"]:
        p = os.path.join(fw_dir, mbn)
        if os.path.isfile(p):
            add_file(f"firmware/qcom/sm8250/xiaomi/alioth/{mbn}", 0o644, "u:object_r:vendor_firmware_file:s0", p)

    add_bytes("etc/vintf/manifest/manifest_vendor.xiaomi.hardware.mlipay.xml", 0o644, "u:object_r:vendor_configs_file:s0",
              EMPTY_MANIFEST_XML.encode())

    # 2. Graphics (64-bit + 32-bit Turnip + patched Adreno utils + drmutils + sdedrm)
    add_file("lib64/hw/vulkan.adreno.so", 0o644, "u:object_r:same_process_hal_file:s0",
             os.path.join(WS, "turnip_out", "vulkan.adreno.so"))
    add_file("lib/hw/vulkan.adreno.so", 0o644, "u:object_r:same_process_hal_file:s0",
             os.path.join(WS, "turnip_out_arm", "vulkan.adreno.so"))
    add_file("lib64/libadreno_utils.so", 0o644, "u:object_r:same_process_hal_file:s0",
             os.path.join(WS, "patches", "gralloc", "patched", "lib64", "libadreno_utils.so"))
    add_file("lib/libadreno_utils.so", 0o644, "u:object_r:same_process_hal_file:s0",
             os.path.join(WS, "patches", "gralloc", "patched", "lib", "libadreno_utils.so"))
    add_file("lib64/libdrmutils.so", 0o644, "u:object_r:same_process_hal_file:s0",
             os.path.join(CLOVER, "libdrmutils_patched.so"))
    add_file("lib64/libsdedrm.so", 0o644, "u:object_r:vendor_file:s0",
             os.path.join(CLOVER, "libsdedrm_patched.so"))

    # 3. Bootctl + Camera/QTEE/QSEECOM stubs
    add_file("bin/hw/android.hardware.boot-service.qti", 0o755, "u:object_r:hal_bootctl_default_exec:s0",
             os.path.join(CLOVER, "boot_qti_patched"))
    add_file("bin/hw/android.hardware.camera.provider@2.4-service_64", 0o755, "u:object_r:hal_camera_default_exec:s0",
             os.path.join(WS, "tools", "trampoline", "stub_sleeper"))
    add_file("bin/hw/vendor.qti.hardware.qteeconnector@1.0-service", 0o755, "u:object_r:vendor_hal_qteeconnector_qti_exec:s0",
             os.path.join(WS, "tools", "trampoline", "stub_sleeper"))
    add_file("bin/qseecomd", 0o755, "u:object_r:tee_exec:s0",
             os.path.join(WS, "tools", "trampoline", "stub_sleeper"))
    add_file("bin/adpl", 0o755, "u:object_r:vendor_adpl_exec:s0",
             os.path.join(WS, "tools", "trampoline", "stub_sleeper"))
    add_file("bin/qti", 0o755, "u:object_r:vendor_qti_exec:s0",
             os.path.join(WS, "tools", "trampoline", "stub_sleeper"))
    add_file("bin/netmgrd", 0o755, "u:object_r:vendor_netmgrd_exec:s0",
             os.path.join(WS, "tools", "trampoline", "stub_sleeper"))
    add_file("bin/hw/android.hardware.sensors@1.0-service", 0o755, "u:object_r:hal_sensors_default_exec:s0",
             os.path.join(WS, "tools", "trampoline", "stub_sleeper"))
    # HWC3 DRM Service & NDK library (self-contained, vendor independent)
    add_file("bin/hw/android.hardware.composer.hwc3-service.drm", 0o755, "u:object_r:hal_graphics_composer_default_exec:s0",
             os.path.join(WS, "tools", "composer_prebuilt", "hwc3-service.drm"))
    add_file("lib64/android.hardware.graphics.composer3-V5-ndk.so", 0o644, "u:object_r:same_process_hal_file:s0",
             os.path.join(WS, "tools", "composer_prebuilt", "composer3-V5-ndk.so"))
    add_bytes("etc/init/hwc3-drm.rc", 0o644, "u:object_r:vendor_configs_file:s0", HWC3_RC.encode())
    add_bytes("etc/init/remoteproc.rc", 0o644, "u:object_r:vendor_configs_file:s0", REMOTEPROC_RC.encode())
    add_bytes("bin/mainline_boot.sh", 0o755, "u:object_r:vendor_file:s0", MAINLINE_BOOT_SH.encode())
    add_bytes("etc/vintf/manifest/hwc3-drm.xml", 0o644, "u:object_r:vendor_configs_file:s0", HWC3_XML.encode())
    # 4. PureSoftKeyMint V5 service + 12 KeyMint/RKP libraries
    add_file("bin/hw/android.hardware.security.keymint-service", 0o755, "u:object_r:hal_keymint_default_exec:s0",
             os.path.join(WS, "tools", "keymint", "android.hardware.security.keymint-service"))

    km_libs = [
        "android.hardware.security.keymint-V5-ndk.so",
        "android.hardware.security.rkp-V3-ndk.so",
        "android.hardware.security.secureclock-V1-ndk.so",
        "android.hardware.security.sharedsecret-V1-ndk.so",
        "lib_android_keymaster_keymint_utils_fake_latest.so",
        "libcppbor.so",
        "libcppcose_rkp.so",
        "libkeymaster_messages.so",
        "libkeymaster_portable.so",
        "libkeymint_fake_latest.so",
        "libpuresoftkeymasterdevice.so",
        "libsoft_attestation_cert.so",
    ]
    for name in km_libs:
        add_file(f"lib64/{name}", 0o644, "u:object_r:vendor_file:s0",
                 os.path.join(WS, "tmp_probe", "p19", name))

    # 5. Wi-Fi HAL patched libraries (prevent integer underflow abort)
    add_file("lib64/libwifi-hal.so", 0o644, "u:object_r:vendor_file:s0",
             os.path.join(WS, "patches", "wifi", "libwifi-hal.so"))
    add_file("lib64/libwifi-hal-qcom.so", 0o644, "u:object_r:vendor_file:s0",
             os.path.join(WS, "patches", "wifi", "libwifi-hal-qcom.so"))

    # 6. Ashmem-compatible libcutils (32-bit & 64-bit)
    add_file("lib64/libcutils.so", 0o644, "u:object_r:vendor_file:s0",
             os.path.join(WS, "patches", "cutils", "libcutils_64.so"))
    add_file("lib/libcutils.so", 0o644, "u:object_r:vendor_file:s0",
             os.path.join(WS, "patches", "cutils", "libcutils_32.so"))

    # Audio HAL safe deallocate patch (prevents SIGSEGV in acdb_loader_deallocate_ACDB)
    add_file("lib/libacdbloader.so", 0o644, "u:object_r:vendor_file:s0",
             os.path.join(WS, "patches", "audio", "libacdbloader.so"))

    # Derive all needed directories top-down
    needed_dirs = set()
    for rel_path, _, _, _ in files:
        parts = rel_path.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            d = "/".join(parts[:i])
            assert d in DIR_CTXS, f"Missing DIR_CTXS mapping for {d}"
            needed_dirs.add(d)

    sorted_dirs = sorted(needed_dirs, key=lambda s: (s.count("/"), s))
    sorted_files = sorted(files, key=lambda x: x[0])

    # Write preview tree for inspection/verification (not packed into cpio directly)
    if OVR_TREE_PREVIEW:
        for rel_path, _, _, data in sorted_files:
            out_p = os.path.join(OVR_TREE_PREVIEW, rel_path)
            os.makedirs(os.path.dirname(out_p), exist_ok=True)
            with open(out_p, "wb") as f:
                f.write(data)

    total_entries = len(sorted_dirs) + len(sorted_files)
    raw_bytes = 0
    with open(OUT_BIN, "wb") as out:
        out.write(b"OVR1")
        out.write(struct.pack("<I", total_entries))
        for d in sorted_dirs:
            pb = d.encode()
            cb = DIR_CTXS[d].encode()
            out.write(struct.pack("<BIHHI", ord("D"), 0o755, len(pb), len(cb), 0))
            out.write(pb)
            out.write(cb)
        for rel_path, mode, ctx, data in sorted_files:
            pb = rel_path.encode()
            cb = ctx.encode()
            out.write(struct.pack("<BIHHI", ord("F"), mode, len(pb), len(cb), len(data)))
            out.write(pb)
            out.write(cb)
            out.write(data)
            raw_bytes += len(data)

    print(f"Packed OVR1: {len(sorted_dirs)} dirs + {len(sorted_files)} files = {raw_bytes/1024/1024:.2f} MiB -> {OUT_BIN}")
    for d in sorted_dirs:
        print(f"  [DIR 0755] {d:55s} ({DIR_CTXS[d]})")
    for rel_path, mode, ctx, data in sorted_files:
        print(f"  [FIL {mode:04o}] {rel_path:55s} {len(data):8d}B ({ctx})")

if __name__ == "__main__":
    main()
