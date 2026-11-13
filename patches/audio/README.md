# Qualcomm Audio HAL ACDB Null Dereference Fix

### Problem Statement
On mainline Linux kernels, Qualcomm DSP soundcard mixer controls (such as `CVD Version`) are not registered via downstream ioctls.
When ACDB initialization fails, `platform_stdev_init` in `sound_trigger.primary.kona.so` calls `acdb_loader_deallocate_ACDB`.

### Tombstone Analysis
```
#00 pc 0000f346  /vendor/lib/libacdbloader.so (acdb_loader_deallocate_ACDB+98)
#01 pc 000570b5  /vendor/lib/hw/sound_trigger.primary.kona.so (platform_stdev_init+172)
#02 pc 00055cb5  /vendor/lib/hw/sound_trigger.primary.kona.so (stdev_open+232)
```
Disassembly at `0xf2f8` in Thumb-16 mode:
```asm
0xf2f8:  39 b1    cbz  r1, 0xf30a    ; BUG: Branches to list deallocation if refcount == 0!
0xf2fa:  ...
0xf300:  unlock & return
0xf30a:  deallocate list:
0xf346:  ldr  r1, [r0]               ; CRASH: r0 is NULL because list was never initialized!
```

### Solution
Patch `0xf2f8` from `39 b1` to `11 b1` (`cbz r1, 0xf300`).
If `refcount == 0`, it immediately unlocks the mutex and returns safely.
