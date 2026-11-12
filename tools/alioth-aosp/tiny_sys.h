#ifndef TINY_SYS_H
#define TINY_SYS_H

#define AT_FDCWD (-100)
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR   02
#define O_CREAT  0100
#define O_TRUNC  01000

#define S_IFCHR  0020000
#define S_IFBLK  0060000

#define MS_RDONLY  1
#define MS_REMOUNT 32
#define MS_BIND    4096

#define LOOP_SET_FD 0x4C00

static inline long sys_call1(long nr, long a0) {
    register long x0 __asm__("x0") = a0;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");
    return x0;
}

static inline long sys_call2(long nr, long a0, long a1) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory", "cc");
    return x0;
}

static inline long sys_call3(long nr, long a0, long a1, long a2) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory", "cc");
    return x0;
}

static inline long sys_call4(long nr, long a0, long a1, long a2, long a3) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory", "cc");
    return x0;
}

static inline long sys_call5(long nr, long a0, long a1, long a2, long a3, long a4) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x8) : "memory", "cc");
    return x0;
}

#define __NR_setxattr  5
#define __NR_ioctl     29
#define __NR_mknodat   33
#define __NR_mkdirat   34
#define __NR_unlinkat  35
#define __NR_symlinkat 36
#define __NR_mount     40
#define __NR_ftruncate 46
#define __NR_fchmodat  53
#define __NR_openat    56
#define __NR_close     57
#define __NR_lseek     62
#define __NR_read      63
#define __NR_write     64
#define __NR_pwrite64  68
#define __NR_exit      93
#define __NR_nanosleep 101
#define __NR_execve    221

static inline long sys_openat(long dfd, const char *p, int flags, int mode) {
    return sys_call4(__NR_openat, dfd, (long)p, flags, mode);
}

static inline long sys_mkdirat(long dfd, const char *p, int mode) {
    return sys_call3(__NR_mkdirat, dfd, (long)p, mode);
}

static inline long sys_mknodat(long dfd, const char *p, int mode, unsigned long dev) {
    return sys_call4(__NR_mknodat, dfd, (long)p, mode, dev);
}

static inline long sys_symlinkat(const char *target, long dfd, const char *linkpath) {
    return sys_call3(__NR_symlinkat, (long)target, dfd, (long)linkpath);
}

static inline long sys_unlinkat(long dfd, const char *p, int flags) {
    return sys_call3(__NR_unlinkat, dfd, (long)p, flags);
}

static inline long sys_fchmodat(long dfd, const char *p, int mode) {
    return sys_call4(__NR_fchmodat, dfd, (long)p, mode, 0);
}

static inline long sys_mount(const char *dev, const char *dir, const char *type,
                             unsigned long flags, const void *data) {
    return sys_call5(__NR_mount, (long)dev, (long)dir, (long)type, flags, (long)data);
}

static inline long sys_ioctl(int fd, unsigned long req, unsigned long arg) {
    return sys_call3(__NR_ioctl, fd, req, arg);
}

static inline long sys_execve(const char *f, char *const *argv, char *const *envp) {
    return sys_call3(__NR_execve, (long)f, (long)argv, (long)envp);
}

static inline int write_all(int fd, const char *buf, int len) {
    int total = 0;
    while (total < len) {
        long n = sys_call3(__NR_write, fd, (long)(buf + total), len - total);
        if (n <= 0) return -1;
        total += (int)n;
    }
    return total;
}

static inline void klog_raw(const char *msg, long val) {
    long fd = sys_openat(AT_FDCWD, "/dev/kmsg", O_WRONLY, 0);
    if (fd < 0) return;
    int len = 0;
    while (msg[len]) len++;
    write_all((int)fd, msg, len);

    if (val != 0x7fffffffL) {
        char buf[32];
        int i = 0;
        unsigned long uval = (unsigned long)val;
        if (val < 0) {
            buf[i++] = '-';
            uval = (unsigned long)(-val);
        }
        char tmp[24];
        int ti = 0;
        if (uval == 0) tmp[ti++] = '0';
        else {
            while (uval > 0) {
                tmp[ti++] = '0' + (uval % 10);
                uval /= 10;
            }
        }
        for (int j = ti - 1; j >= 0; j--) buf[i++] = tmp[j];
        buf[i++] = '\n';
        write_all((int)fd, buf, i);
    } else {
        write_all((int)fd, "\n", 1);
    }
    sys_call1(__NR_close, fd);
}

static inline int set_selinux_ctx(const char *path, const char *ctx) {
    int len = 0;
    while (ctx[len]) len++;
    long rc = sys_call5(__NR_setxattr, (long)path, (long)"security.selinux", (long)ctx, len + 1, 0);
    return (int)rc;
}

static inline int copy_file(const char *src, const char *dst, int mode) {
    long sfd = sys_openat(AT_FDCWD, src, O_RDONLY, 0);
    if (sfd < 0) {
        klog_raw("[copy] open src failed: ", sfd);
        return -1;
    }
    long dfd = sys_openat(AT_FDCWD, dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (dfd < 0) {
        sys_call1(__NR_close, sfd);
        klog_raw("[copy] open dst failed: ", dfd);
        return -2;
    }

    char buf[16384];
    while (1) {
        long n = sys_call3(__NR_read, sfd, (long)buf, sizeof(buf));
        if (n <= 0) break;
        write_all((int)dfd, buf, (int)n);
    }
    sys_call1(__NR_close, sfd);
    sys_call1(__NR_close, dfd);
    return 0;
}

/* Custom sparse unpacker for fast ext4 loop6 init (DATS magic) */
static inline int unpack_sparse_ext4(const char *sparse_path, const char *raw_path) {
    long sfd = sys_openat(AT_FDCWD, sparse_path, O_RDONLY, 0);
    if (sfd < 0) return -1;

    char hdr[16];
    long n = sys_call3(__NR_read, sfd, (long)hdr, 16);
    if (n < 16 || hdr[0] != 'D' || hdr[1] != 'A' || hdr[2] != 'T' || hdr[3] != 'S') {
        sys_call1(__NR_close, sfd);
        return -2;
    }

    unsigned int blk_sz = *(unsigned int *)(hdr + 4);
    unsigned int tot_blks = *(unsigned int *)(hdr + 8);
    unsigned int num_recs = *(unsigned int *)(hdr + 12);

    long rfd = sys_openat(AT_FDCWD, raw_path, O_RDWR | O_CREAT | O_TRUNC, 0660);
    if (rfd < 0) {
        sys_call1(__NR_close, sfd);
        return -3;
    }

    /* ftruncate to full image size instantaneously */
    long long total_bytes = (long long)tot_blks * blk_sz;
    sys_call2(__NR_ftruncate, rfd, (long)total_bytes);

    char blk_buf[4096];
    for (unsigned int i = 0; i < num_recs; i++) {
        unsigned int blk_idx = 0;
        long rn = sys_call3(__NR_read, sfd, (long)&blk_idx, 4);
        if (rn < 4) break;
        rn = sys_call3(__NR_read, sfd, (long)blk_buf, blk_sz);
        if (rn < (long)blk_sz) break;
        long long offset = (long long)blk_idx * blk_sz;
        sys_call4(__NR_pwrite64, rfd, (long)blk_buf, blk_sz, (long)offset);
    }

    sys_call1(__NR_close, sfd);
    return (int)rfd;
}

/* Filter needle lines from RC configs */
static inline int patch_file_lines(const char *src, const char *dst,
                                  const char *const *needles, int n_needles) {
    long sfd = sys_openat(AT_FDCWD, src, O_RDONLY, 0);
    if (sfd < 0) return -1;
    long dfd = sys_openat(AT_FDCWD, dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dfd < 0) {
        sys_call1(__NR_close, sfd);
        return -2;
    }

    char line[1024];
    int lp = 0;
    char ch;
    while (sys_call3(__NR_read, sfd, (long)&ch, 1) == 1) {
        if (ch == '\n') {
            line[lp] = '\0';
            int drop = 0;
            for (int k = 0; k < n_needles; k++) {
                const char *nd = needles[k];
                int nlen = 0;
                while (nd[nlen]) nlen++;
                for (int p = 0; p <= lp - nlen; p++) {
                    int match = 1;
                    for (int m = 0; m < nlen; m++) {
                        if (line[p + m] != nd[m]) { match = 0; break; }
                    }
                    if (match) { drop = 1; break; }
                }
                if (drop) break;
            }
            if (!drop) {
                line[lp++] = '\n';
                write_all((int)dfd, line, lp);
            }
            lp = 0;
        } else {
            if (lp < (int)sizeof(line) - 2) {
                line[lp++] = ch;
            }
        }
    }
    if (lp > 0) {
        line[lp++] = '\n';
        write_all((int)dfd, line, lp);
    }
    sys_call1(__NR_close, sfd);
    sys_call1(__NR_close, dfd);
    return 0;
}


static inline unsigned int dec_u32_le(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static inline unsigned short dec_u16_le(const unsigned char *p) {
    return (unsigned short)((unsigned int)p[0] | ((unsigned int)p[1] << 8));
}

static inline int read_exact(int fd, void *buf, unsigned int len) {
    unsigned int off = 0;
    unsigned char *p = (unsigned char *)buf;
    while (off < len) {
        long n = sys_call3(__NR_read, fd, (long)(p + off), len - off);
        if (n <= 0) return -1;
        off += (unsigned int)n;
    }
    return 0;
}

/* Unpack OVR1 overlay tree archive (dirs + files + modes + security.selinux xattrs) */
static inline int unpack_ovr_bundle(const char *bundle_path, const char *dest_root) {
    long sfd = sys_openat(AT_FDCWD, bundle_path, O_RDONLY, 0);
    if (sfd < 0) return -1;

    unsigned char hdr[8];
    if (read_exact((int)sfd, hdr, 8) < 0 ||
        hdr[0] != 'O' || hdr[1] != 'V' || hdr[2] != 'R' || hdr[3] != '1') {
        sys_call1(__NR_close, sfd);
        return -2;
    }
    unsigned int count = dec_u32_le(hdr + 4);

    int root_len = 0;
    while (dest_root[root_len]) root_len++;
    if (root_len <= 0 || root_len >= 120) {
        sys_call1(__NR_close, sfd);
        return -3;
    }

    sys_mkdirat(AT_FDCWD, dest_root, 0755);
    sys_fchmodat(AT_FDCWD, dest_root, 0755);
    set_selinux_ctx(dest_root, "u:object_r:vendor_file:s0");

    unsigned char io_buf[16384];
    for (unsigned int i = 0; i < count; i++) {
        unsigned char rhdr[13];
        if (read_exact((int)sfd, rhdr, 13) < 0) {
            sys_call1(__NR_close, sfd);
            return -4;
        }
        unsigned char etype = rhdr[0];
        unsigned int mode = dec_u32_le(rhdr + 1);
        unsigned int plen = dec_u16_le(rhdr + 5);
        unsigned int clen = dec_u16_le(rhdr + 7);
        unsigned int dlen = dec_u32_le(rhdr + 9);

        if (plen == 0 || plen >= 256 || clen == 0 || clen >= 128) {
            sys_call1(__NR_close, sfd);
            return -5;
        }

        char full_path[400];
        for (int j = 0; j < root_len; j++) full_path[j] = dest_root[j];
        full_path[root_len] = '/';
        if (read_exact((int)sfd, full_path + root_len + 1, plen) < 0) {
            sys_call1(__NR_close, sfd);
            return -6;
        }
        full_path[root_len + 1 + plen] = '\0';

        char ctx_buf[128];
        if (read_exact((int)sfd, ctx_buf, clen) < 0) {
            sys_call1(__NR_close, sfd);
            return -7;
        }
        ctx_buf[clen] = '\0';

        if (etype == 'D') {
            if (dlen != 0) {
                sys_call1(__NR_close, sfd);
                return -8;
            }
            sys_mkdirat(AT_FDCWD, full_path, (int)mode);
            sys_fchmodat(AT_FDCWD, full_path, (int)mode);
            set_selinux_ctx(full_path, ctx_buf);
        } else if (etype == 'F') {
            long dfd = sys_openat(AT_FDCWD, full_path, O_WRONLY | O_CREAT | O_TRUNC, (int)mode);
            if (dfd < 0) {
                sys_call1(__NR_close, sfd);
                return -9;
            }
            unsigned int rem = dlen;
            while (rem > 0) {
                unsigned int chunk = rem > (unsigned int)sizeof(io_buf) ? (unsigned int)sizeof(io_buf) : rem;
                if (read_exact((int)sfd, io_buf, chunk) < 0) {
                    sys_call1(__NR_close, dfd);
                    sys_call1(__NR_close, sfd);
                    return -10;
                }
                if (write_all((int)dfd, (const char *)io_buf, (int)chunk) < 0) {
                    sys_call1(__NR_close, dfd);
                    sys_call1(__NR_close, sfd);
                    return -11;
                }
                rem -= chunk;
            }
            sys_call1(__NR_close, dfd);
            sys_fchmodat(AT_FDCWD, full_path, (int)mode);
            set_selinux_ctx(full_path, ctx_buf);
        } else {
            sys_call1(__NR_close, sfd);
            return -12;
        }
    }

    sys_call1(__NR_close, sfd);
    return 0;
}

#endif
