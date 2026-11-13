#!/usr/bin/env python3
"""
Patch Android libcutils (32-bit & 64-bit) __ashmem_is_ashmem to support
Chromium / WebView sandboxed renderer processes on Mainline Linux.

Root Cause:
Chromium's seccomp-bpf sandbox blocks readlink(/proc/self/fd/...) with EPERM.
In libcutils/ashmem-dev.cpp, __ashmem_is_ashmem uses readlink to check for '/memfd:'.
Because readlink fails with EPERM, __ashmem_is_ashmem returns -1, causing ashmem_get_size_region,
ashmem_set_prot_region, ashmem_pin_region to fail on all shared memory transfers.

Fix:
Patch __ashmem_is_ashmem entrypoint in both libraries:
- 32-bit (file offset 0x9340): 'movs r0, #1; bx lr' (01 20 70 47)
- 64-bit (file offset 0xfbf0): 'mov w0, #1; ret' (20 00 80 52 c0 03 5f d6)
"""
import sys

def patch_cutils(p32_path, p64_path):
    # 32-bit
    with open(p32_path, "rb") as f:
        d32 = bytearray(f.read())
    assert d32[0x9340:0x9344] in (b"\x70\xb5\x88\xb0", b"\x01\x20\x70\x47")
    d32[0x9340:0x9344] = b"\x01\x20\x70\x47"
    # also ashmem_valid at 0x9294
    d32[0x9294:0x9298] = b"\x01\x20\x70\x47"
    with open(p32_path, "wb") as f:
        f.write(d32)
    print(f"Patched 32-bit: {p32_path}")

    # 64-bit
    with open(p64_path, "rb") as f:
        d64 = bytearray(f.read())
    assert d64[0xfbf0:0xfbf4] in (b"\xff\xc3\x01\xd1", b"\x20\x00\x80\x52")
    d64[0xfbf0:0xfbf8] = b"\x20\x00\x80\x52\xc0\x03\x5f\xd6"
    # also ashmem_valid at 0xfb10
    d64[0xfb10:0xfb18] = b"\x20\x00\x80\x52\xc0\x03\x5f\xd6"
    with open(p64_path, "wb") as f:
        f.write(d64)
    print(f"Patched 64-bit: {p64_path}")

if __name__ == "__main__":
    patch_cutils(sys.argv[1], sys.argv[2])
