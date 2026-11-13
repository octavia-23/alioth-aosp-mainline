#!/usr/bin/env python3
"""
Patch Qualcomm Audio HAL libacdbloader.so to prevent fatal NULL pointer dereference
on uninitialized ACDB loader deallocations on Mainline Linux.

Root Cause:
acdb_loader_deallocate_ACDB had an opcode at 0xf2f8: 'cbz r1, 0xf30a' (39 b1)
which unconditionally jumped to circular linked-list deallocation even when refcount == 0.
When *head is NULL, 'ldr r1, [r0]' at 0xf346 dereferenced 0x0, crashing android.hardware.audio.service.

Fix:
Replace '39 b1' with '11 b1' ('cbz r1, 0xf300'), which immediately unlocks the mutex
and returns cleanly if refcount is 0.
"""
import sys

def patch_acdb(so_path):
    with open(so_path, "rb") as f:
        data = bytearray(f.read())
    
    # Verify opcode at 0xf2f8: 39 b1 -> cbz r1, 0xf30a
    assert data[0xf2f8:0xf2fa] == b"\x39\xb1", f"Unexpected bytes at 0xf2f8: {data[0xf2f8:0xf2fa].hex()}"
    data[0xf2f8:0xf2fa] = b"\x11\xb1"
    
    with open(so_path, "wb") as f:
        f.write(data)
    print(f"Successfully patched {so_path} (0xf2f8: 39 b1 -> 11 b1)")

if __name__ == "__main__":
    patch_acdb(sys.argv[1])
