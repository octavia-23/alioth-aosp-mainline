#!/usr/bin/env python3
"""
Patch libadreno_utils.so (32-bit & 64-bit) for Qualcomm SM8250 (Adreno 650).
Patches uninitialized g_chip_id (-1) in .data to 0x06050002.
Prevents SIGSEGV during Gralloc buffer allocation and SurfaceFlinger lock/unlock.
"""
import struct, sys

def patch_adreno_utils(p32_path, p64_path):
    # 32-bit at offset 0xdcc4
    with open(p32_path, "rb") as f:
        d32 = bytearray(f.read())
    struct.pack_into("<I", d32, 0x0dcc4, 0x06050002)
    with open(p32_path, "wb") as f:
        f.write(d32)

    # 64-bit at offset 0x13268
    with open(p64_path, "rb") as f:
        d64 = bytearray(f.read())
    struct.pack_into("<I", d64, 0x13268, 0x06050002)
    with open(p64_path, "wb") as f:
        f.write(d64)
    print("Patched libadreno_utils.so for Adreno 650 (0x06050002)")

if __name__ == "__main__":
    patch_adreno_utils(sys.argv[1], sys.argv[2])
