#include "tiny_sys.h"

static inline void patch_and_bind(const char *orig_path, const char *tmp_path,
                                  const char *selinux_ctx,
                                  const char *const *needles, int n_needles) {
    int prc = patch_file_lines(orig_path, tmp_path, needles, n_needles);
    klog_raw("[stage2] patch rc=", prc);
    if (prc == 0) {
        set_selinux_ctx(tmp_path, selinux_ctx);
        long brc = sys_mount(tmp_path, orig_path, (void *)0, MS_BIND, (void *)0);
        klog_raw("[stage2] bind rc=", brc);
    }
}

static void setup_data_loop6(void) {
    int img_fd = unpack_sparse_ext4("/postinstall/data_sparse.bin", "/dev/.data_loop.img");
    klog_raw("[stage2] unpack data_sparse fd/rc=", img_fd);
    sys_unlinkat(AT_FDCWD, "/postinstall/data_sparse.bin", 0);
    if (img_fd < 0) return;

    const char *tmp_loop6 = "/dev/.ml_loop6";
    sys_mknodat(AT_FDCWD, tmp_loop6, S_IFBLK | 0660, (7UL << 8) | (6UL << 3));
    long lfd = sys_openat(AT_FDCWD, tmp_loop6, O_RDWR, 0);
    if (lfd >= 0) {
        long rc = sys_ioctl((int)lfd, LOOP_SET_FD, img_fd);
        klog_raw("[stage2] LOOP_SET_FD(loop6) rc=", rc);
        sys_call1(__NR_close, lfd);
    } else {
        klog_raw("[stage2] open /dev/.ml_loop6 failed rc=", lfd);
    }
    sys_call1(__NR_close, img_fd);
    sys_unlinkat(AT_FDCWD, "/dev/.data_loop.img", 0);
    sys_unlinkat(AT_FDCWD, tmp_loop6, 0);
}

int main_c(int argc, char **argv, char **envp) {
    sys_mknodat(AT_FDCWD, "/dev/kmsg", S_IFCHR | 0644, (1UL << 8) | 11UL);
    sys_mknodat(AT_FDCWD, "/dev/tty0", S_IFCHR | 0666, (4UL << 8) | 0UL);
    long tty = sys_openat(AT_FDCWD, "/dev/tty0", O_WRONLY, 0);
    if (tty >= 0) {
        write_all((int)tty, "[ALIOTH MAINLINE] Stage 2 v25: Overlay /vendor + Init...\n", 57);
        sys_call1(__NR_close, tty);
    }
    klog_raw("[stage2] start v25 (OVR1 + KeyMint V5 + Turnip + Adreno650)", 0x7fffffffL);

    /* 1. Unpack vendor_ovr once and mount read-only overlayfs over /vendor */
    long sentinel = sys_openat(AT_FDCWD, "/postinstall/.ovr_mounted", O_RDONLY, 0);
    if (sentinel < 0) {
        int urc = unpack_ovr_bundle("/postinstall/vendor_ovr.bin", "/postinstall/vendor_ovr");
        klog_raw("[stage2] unpack vendor_ovr rc=", urc);
        sys_unlinkat(AT_FDCWD, "/postinstall/vendor_ovr.bin", 0);

        if (urc == 0) {
            long ovr_rc = sys_mount("overlay", "/vendor", "overlay", MS_RDONLY,
                                    "lowerdir=/postinstall/vendor_ovr:/vendor");
            klog_raw("[stage2] overlay /vendor rc=", ovr_rc);
            if (ovr_rc == 0) {
                long sfd = sys_openat(AT_FDCWD, "/postinstall/.ovr_mounted",
                                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (sfd >= 0) {
                    sys_call1(__NR_close, sfd);
                    set_selinux_ctx("/postinstall/.ovr_mounted", "u:object_r:system_file:s0");
                }
            }
        }
    } else {
        sys_call1(__NR_close, sentinel);
        klog_raw("[stage2] vendor_ovr already mounted, skipping", 0x7fffffffL);
    }

    setup_data_loop6();

    /* 2. DRM Device Nodes (/dev/dri/card0, renderD128, controlD64) */
    sys_mkdirat(AT_FDCWD, "/dev/dri", 0755);
    set_selinux_ctx("/dev/dri", "u:object_r:gpu_device:s0");

    sys_unlinkat(AT_FDCWD, "/dev/dri/card0", 0);
    sys_mknodat(AT_FDCWD, "/dev/dri/card0", S_IFCHR | 0666, (226UL << 8) | 0UL);
    sys_fchmodat(AT_FDCWD, "/dev/dri/card0", 0666);
    set_selinux_ctx("/dev/dri/card0", "u:object_r:graphics_device:s0");

    sys_unlinkat(AT_FDCWD, "/dev/dri/renderD128", 0);
    sys_mknodat(AT_FDCWD, "/dev/dri/renderD128", S_IFCHR | 0666, (226UL << 8) | 128UL);
    sys_fchmodat(AT_FDCWD, "/dev/dri/renderD128", 0666);
    set_selinux_ctx("/dev/dri/renderD128", "u:object_r:gpu_device:s0");

    sys_unlinkat(AT_FDCWD, "/dev/dri/controlD64", 0);
    sys_mknodat(AT_FDCWD, "/dev/dri/controlD64", S_IFCHR | 0666, (226UL << 8) | 64UL);
    sys_fchmodat(AT_FDCWD, "/dev/dri/controlD64", 0666);
    sys_fchmodat(AT_FDCWD, "/sys/class/backlight/ae94000.dsi.0/brightness", 0666);
    sys_fchmodat(AT_FDCWD, "/sys/class/backlight/ae94000.dsi.0/bl_power", 0666);
    set_selinux_ctx("/dev/dri/controlD64", "u:object_r:graphics_device:s0");

    long ufd0 = sys_openat(AT_FDCWD, "/sys/class/drm/card0/uevent", O_WRONLY, 0);
    if (ufd0 >= 0) { write_all((int)ufd0, "add\n", 4); sys_call1(__NR_close, ufd0); }
    long ufd1 = sys_openat(AT_FDCWD, "/sys/class/drm/renderD128/uevent", O_WRONLY, 0);
    if (ufd1 >= 0) { write_all((int)ufd1, "add\n", 4); sys_call1(__NR_close, ufd1); }

    /* Input devices for touchscreen & keys */
    sys_mkdirat(AT_FDCWD, "/dev/input", 0755);
    set_selinux_ctx("/dev/input", "u:object_r:input_device:s0");

    for (unsigned int i = 0; i < 8; i++) {
        char path[32];
        char uevent[64];
        path[0] = '/'; path[1] = 'd'; path[2] = 'e'; path[3] = 'v';
        path[4] = '/'; path[5] = 'i'; path[6] = 'n'; path[7] = 'p';
        path[8] = 'u'; path[9] = 't'; path[10] = '/'; path[11] = 'e';
        path[12] = 'v'; path[13] = 'e'; path[14] = 'n'; path[15] = 't';
        path[16] = (char)('0' + i); path[17] = '\0';

        sys_unlinkat(AT_FDCWD, path, 0);
        sys_mknodat(AT_FDCWD, path, S_IFCHR | 0666, (13UL << 8) | (64UL + (unsigned long)i));
        sys_fchmodat(AT_FDCWD, path, 0666);
        set_selinux_ctx(path, "u:object_r:input_device:s0");

        /* trigger uevent */
        uevent[0] = '/'; uevent[1] = 's'; uevent[2] = 'y'; uevent[3] = 's';
        uevent[4] = '/'; uevent[5] = 'c'; uevent[6] = 'l'; uevent[7] = 'a';
        uevent[8] = 's'; uevent[9] = 's'; uevent[10] = '/'; uevent[11] = 'i';
        uevent[12] = 'n'; uevent[13] = 'p'; uevent[14] = 'u'; uevent[15] = 't';
        uevent[16] = '/'; uevent[17] = 'e'; uevent[18] = 'v'; uevent[19] = 'e';
        uevent[20] = 'n'; uevent[21] = 't'; uevent[22] = (char)('0' + i);
        uevent[23] = '/'; uevent[24] = 'u'; uevent[25] = 'e'; uevent[26] = 'v';
        uevent[27] = 'e'; uevent[28] = 'n'; uevent[29] = 't'; uevent[30] = '\0';
        long fd = sys_openat(AT_FDCWD, uevent, O_WRONLY, 0);
        if (fd >= 0) { write_all((int)fd, "add\n", 4); sys_call1(__NR_close, fd); }
    }

    /* 3. Patch & bind /system/etc/init RCs (vendor RCs/XMLs/libs are handled by overlay) */
    static const char *const init_needles[] = {
        "vdc ",
        "wait_for_prop keystore.module_hash.sent",
        "wait_for_prop odsign.key.done",
        "wait_for_prop odsign.verification.done",
        "reboot_on_failure",
    };
    patch_and_bind("/system/etc/init/hw/init.rc", "/postinstall/init.rc",
                   "u:object_r:system_file:s0", init_needles, 5);

    static const char *const ks_needles[] = { "critical" };
    patch_and_bind("/system/etc/init/keystore2.rc", "/postinstall/keystore2.rc",
                   "u:object_r:system_file:s0", ks_needles, 1);

    static const char *const fhc_needles[] = { "UPDATABLE_CRASHING", "BOOT_FAILURE" };
    patch_and_bind("/system/etc/init/flags_health_check.rc", "/postinstall/flags_health_check.rc",
                   "u:object_r:system_file:s0", fhc_needles, 2);

    static const char *const rof_needles[] = { "reboot_on_failure" };
    patch_and_bind("/system/etc/init/apexd.rc", "/postinstall/apexd.rc",
                   "u:object_r:system_file:s0", rof_needles, 1);
    patch_and_bind("/system/etc/init/vold.rc", "/postinstall/vold.rc",
                   "u:object_r:system_file:s0", rof_needles, 1);
    patch_and_bind("/system/etc/init/netd.rc", "/postinstall/netd.rc",
                   "u:object_r:system_file:s0", rof_needles, 1);

    static const char *const bpf_needles[] = {
        "wait_for_prop bpf.progs_loaded",
        "reboot_on_failure",
    };
    patch_and_bind("/system/etc/init/netbpfload.rc", "/postinstall/netbpfload.rc",
                   "u:object_r:system_file:s0", bpf_needles, 2);

    /* 4. Exec unpatched /system/bin/init with original arguments (e.g. selinux_setup) */
    char *new_argv[16];
    int n = argc < 15 ? argc : 15;
    new_argv[0] = "/system/bin/init";
    for (int i = 1; i < n; i++) new_argv[i] = argv[i];
    new_argv[n] = (char *)0;

    tty = sys_openat(AT_FDCWD, "/dev/tty0", O_WRONLY, 0);
    if (tty >= 0) {
        write_all((int)tty, "[ALIOTH MAINLINE] Starting Android /system/bin/init...\n", 55);
        sys_call1(__NR_close, tty);
    }

    klog_raw("[stage2] exec /system/bin/init argc=", argc);
    long irc = sys_execve("/system/bin/init", new_argv, envp);
    klog_raw("[stage2] execve /system/bin/init failed rc=", irc);
    return 1;
}

void _start(void) {
    __asm__ volatile(
        "ldr x0, [sp]\n"
        "add x1, sp, #8\n"
        "add x2, x0, #1\n"
        "lsl x2, x2, #3\n"
        "add x2, x1, x2\n"
        "bl main_c\n"
        "1:\n"
        "wfe\n"
        "b 1b\n"
    );
}
