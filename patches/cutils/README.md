# Chromium / WebView Ashmem Sandboxing Fix on Mainline

### Problem Statement
When running modern Android (Android 14+) on Mainline Linux kernels without `/dev/ashmem`, Android relies on `memfd` backed memory regions (`sys.use_memfd=true`).

However, `libcutils/ashmem-dev.cpp` validates memfd file descriptors by calling:
```cpp
android::base::Readlink(StringPrintf("/proc/self/fd/%d", fd), &path);
```
Inside Chromium sandboxed renderers (`com.android.webview:sandboxed_process0`), Chromium's strict `seccomp-bpf` sandbox blocks `readlinkat` on `/proc/self/fd/*` with `EPERM` ("Operation not permitted").

This caused `__ashmem_is_ashmem` to fail and log thousands of:
`readlink(/proc/self/fd/...) failed: Operation not permitted`
Consequently, `ashmem_get_size_region`, `ashmem_set_prot_region`, and memory buffer sharing failed, preventing web browsers from rendering content.

### Solution
Patch `__ashmem_is_ashmem` to return `1` (valid shared memory) directly:
- **32-bit (`libcutils.so`)**: File offset `0x9340` -> `01 20 70 47` (`movs r0, #1; bx lr`)
- **64-bit (`libcutils.so`)**: File offset `0xfbf0` -> `20 00 80 52 c0 03 5f d6` (`mov w0, #1; ret`)
