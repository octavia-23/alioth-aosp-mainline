#include "tiny_sys.h"

static char *aosp_argv[] = { "init", (char *)0 };
static char *aosp_envp[] = { (char *)0 };

int main_c(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    sys_mknodat(AT_FDCWD, "/dev/kmsg", S_IFCHR | 0644, (1UL << 8) | 11UL);
    sys_mknodat(AT_FDCWD, "/dev/tty0", S_IFCHR | 0666, (4UL << 8) | 0UL);

    long tty = sys_openat(AT_FDCWD, "/dev/tty0", O_WRONLY, 0);
    if (tty >= 0) {
        write_all((int)tty, "[ALIOTH MAINLINE] Pre-init v25 starting...\n", 43);
        sys_call1(__NR_close, tty);
    }
    klog_raw("[preinit1] start v25 (OVR1 + PureSoftKeyMint + SM8250 Gralloc)", 0x7fffffffL);

    /* 1. Mount /postinstall tmpfs (MS_MOVE'd across SwitchRoot by /init.aosp) */
    sys_mkdirat(AT_FDCWD, "/postinstall", 0755);
    long mrc = sys_mount("tmpfs", "/postinstall", "tmpfs", 0, "mode=0755");
    klog_raw("[preinit1] mount tmpfs /postinstall rc=", mrc);
    set_selinux_ctx("/postinstall", "u:object_r:system_file:s0");

    /* 2. Stage stage2 trampoline as /postinstall/ini (exact 17-byte match) */
    copy_file("/mainline_stage2", "/postinstall/ini", 0755);
    set_selinux_ctx("/postinstall/ini", "u:object_r:init_exec:s0");
    sys_unlinkat(AT_FDCWD, "/mainline_stage2", 0);

    /* 3. Stage OVR1 vendor overlay archive & data sparse image */
    copy_file("/mainline_vendor_ovr.bin", "/postinstall/vendor_ovr.bin", 0644);
    set_selinux_ctx("/postinstall/vendor_ovr.bin", "u:object_r:system_file:s0");
    sys_unlinkat(AT_FDCWD, "/mainline_vendor_ovr.bin", 0);

    copy_file("/data_sparse.bin", "/postinstall/data_sparse.bin", 0644);
    set_selinux_ctx("/postinstall/data_sparse.bin", "u:object_r:system_file:s0");
    sys_unlinkat(AT_FDCWD, "/data_sparse.bin", 0);

    /* 4. Pre-create Device Mapper control nodes */
    sys_mkdirat(AT_FDCWD, "/dev/mapper", 0755);
    sys_mknodat(AT_FDCWD, "/dev/mapper/control", S_IFCHR | 0600, (10UL << 8) | 236UL);
    sys_mknodat(AT_FDCWD, "/dev/device-mapper", S_IFCHR | 0600, (10UL << 8) | 236UL);

    /* 5. Pre-create physical partition symlinks in /dev/block/by-name/ */
    sys_mkdirat(AT_FDCWD, "/dev/block", 0755);
    sys_mkdirat(AT_FDCWD, "/dev/block/by-name", 0755);
    sys_mkdirat(AT_FDCWD, "/dev/block/platform", 0755);
    sys_mkdirat(AT_FDCWD, "/dev/block/platform/soc", 0755);
    sys_mkdirat(AT_FDCWD, "/dev/block/platform/soc@0", 0755);

    sys_symlinkat("/dev/sda30", AT_FDCWD, "/dev/block/by-name/super");
    sys_symlinkat("/dev/sda18", AT_FDCWD, "/dev/block/by-name/metadata");
    sys_symlinkat("/dev/sda35", AT_FDCWD, "/dev/block/by-name/userdata");
    sys_symlinkat("/dev/sda31", AT_FDCWD, "/dev/block/by-name/vbmeta_system_a");
    sys_symlinkat("/dev/sda32", AT_FDCWD, "/dev/block/by-name/vbmeta_system_b");
    sys_symlinkat("/dev/sde12", AT_FDCWD, "/dev/block/by-name/boot_a");
    sys_symlinkat("/dev/sde37", AT_FDCWD, "/dev/block/by-name/boot_b");
    sys_symlinkat("/dev/sde25", AT_FDCWD, "/dev/block/by-name/vendor_boot_a");
    sys_symlinkat("/dev/sde49", AT_FDCWD, "/dev/block/by-name/vendor_boot_b");

    sys_symlinkat("/devices/platform/soc@0/1d84000.ufshc", AT_FDCWD, "/dev/block/platform/soc/1d84000.ufshc");
    sys_symlinkat("/devices/platform/soc@0/1d84000.ufshc", AT_FDCWD, "/dev/block/platform/soc@0/1d84000.ufshc");
    sys_symlinkat("/dev/block/platform/soc@0/1d84000.ufshc", AT_FDCWD, "/dev/block/bootdevice");

    klog_raw("[preinit1] execve /init.aosp", 0x7fffffffL);
    long erc = sys_execve("/init.aosp", aosp_argv, aosp_envp);
    klog_raw("[preinit1] execve /init.aosp failed rc=", erc);

    while (1) {
        struct { long s; long ns; } ts;
        ts.s = 60;
        ts.ns = 0;
        sys_call2(__NR_nanosleep, (long)&ts, 0);
    }
    return 1;
}

void _start(void) {
    __asm__ volatile(
        "mov x0, #1\n"
        "adrp x1, aosp_argv\n"
        "add x1, x1, :lo12:aosp_argv\n"
        "adrp x2, aosp_envp\n"
        "add x2, x2, :lo12:aosp_envp\n"
        "bl main_c\n"
        "1:\n"
        "wfe\n"
        "b 1b\n"
    );
}
