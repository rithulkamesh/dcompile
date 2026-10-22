// posix.h: files, directories, time and sockets.
//
// The host is the same OS, so flags and errno values match; only struct layouts differ (i386 Darwin). Sockets are refused
// (ENOSYS) because the game is run offline.

#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <utime.h>

static u32 errno_slot(void) {
    static u32 ea;
    if (!ea)
        ea = data_alloc(4);
    return ea;
}
#define SYSRET(expr)                                                                                         \
    do {                                                                                                     \
        long r_ = (long)(expr);                                                                              \
        if (r_ == -1)                                                                                        \
            wr(errno_slot(), errno, 4);                                                                      \
        RET(r_);                                                                                             \
    } while (0)

static void fill_stat(u32 g, const struct stat *s, int is64) {
    memset(P(g), 0, is64 ? 108 : 96);
    if (is64) {
        wr(g, s->st_dev, 4);
        wr(g + 4, s->st_mode, 2);
        wr(g + 6, s->st_nlink, 2);
        wr(g + 8, (u32)s->st_ino, 4);
        wr(g + 12, (u32)(s->st_ino >> 32), 4);
        wr(g + 16, s->st_uid, 4);
        wr(g + 20, s->st_gid, 4);
        wr(g + 24, s->st_rdev, 4);
        wr(g + 28, (u32)s->st_atimespec.tv_sec, 4);
        wr(g + 32, s->st_atimespec.tv_nsec, 4);
        wr(g + 36, (u32)s->st_mtimespec.tv_sec, 4);
        wr(g + 40, s->st_mtimespec.tv_nsec, 4);
        wr(g + 44, (u32)s->st_ctimespec.tv_sec, 4);
        wr(g + 48, s->st_ctimespec.tv_nsec, 4);
        wr(g + 52, (u32)s->st_birthtimespec.tv_sec, 4);
        wr(g + 56, s->st_birthtimespec.tv_nsec, 4);
        wr(g + 60, (u32)s->st_size, 4);
        wr(g + 64, (u32)((u64)s->st_size >> 32), 4);
        wr(g + 68, (u32)s->st_blocks, 4);
        wr(g + 76, s->st_blksize, 4);
    } else {
        wr(g, s->st_dev, 4);
        wr(g + 4, (u32)s->st_ino, 4);
        wr(g + 8, s->st_mode, 2);
        wr(g + 10, s->st_nlink, 2);
        wr(g + 12, s->st_uid, 4);
        wr(g + 16, s->st_gid, 4);
        wr(g + 20, s->st_rdev, 4);
        wr(g + 24, (u32)s->st_atimespec.tv_sec, 4);
        wr(g + 28, s->st_atimespec.tv_nsec, 4);
        wr(g + 32, (u32)s->st_mtimespec.tv_sec, 4);
        wr(g + 36, s->st_mtimespec.tv_nsec, 4);
        wr(g + 40, (u32)s->st_ctimespec.tv_sec, 4);
        wr(g + 44, s->st_ctimespec.tv_nsec, 4);
        wr(g + 48, (u32)s->st_size, 4);
        wr(g + 52, (u32)((u64)s->st_size >> 32), 4);
        wr(g + 56, (u32)s->st_blocks, 4);
        wr(g + 64, s->st_blksize, 4);
    }
}
H(stat) {
    if (log_imports || DBG_FLAG("IOLOG"))
        fprintf(stderr, "[io] stat(%s)\n", GS(A(0)));
    struct stat s;
    long r = stat(GS(A(0)), &s);
    if (r == 0)
        fill_stat(A(1), &s, 0);
    SYSRET(r);
    return 0;
}
H(lstat) {
    struct stat s;
    long r = lstat(GS(A(0)), &s);
    if (r == 0)
        fill_stat(A(1), &s, 0);
    SYSRET(r);
    return 0;
}
H(stat64) {
    struct stat s;
    long r = stat(GS(A(0)), &s);
    if (r == 0)
        fill_stat(A(1), &s, 1);
    SYSRET(r);
    return 0;
}
H(fstat) {
    struct stat s;
    long r = fstat((int)A(0), &s);
    if (r == 0)
        fill_stat(A(1), &s, 0);
    SYSRET(r);
    return 0;
}
H(open) {
    if (log_imports || DBG_FLAG("IOLOG"))
        fprintf(stderr, "[io] open(%s, %x)\n", GS(A(0)), A(1));
    SYSRET(open(GS(A(0)), (int)A(1), (int)A(2)));
    return 0;
}
H(close) {
    if ((int)A(0) <= 2) {
        RET(0);
        return 0;
    }
    SYSRET(close((int)A(0)));
    return 0;
} // never let the guest close the host's stdio
H(read) {
    SYSRET(read((int)A(0), P(A(1)), A(2)));
    return 0;
}
H(write) {
    SYSRET(write((int)A(0), P(A(1)), A(2)));
    return 0;
}
H(writev) {
    long tot = 0;
    for (u32 i = 0; i < A(2); i++) {
        long r = write((int)A(0), P(rd(A(1) + 8 * i, 4)), rd(A(1) + 8 * i + 4, 4));
        if (r < 0) {
            tot = r;
            break;
        }
        tot += r;
    }
    SYSRET(tot);
    return 0;
}
H(lseek) {
    off_t r = lseek((int)A(0), (off_t)((u64)A(1) | (u64)A(2) << 32), (int)A(3));
    if (r == -1)
        wr(errno_slot(), errno, 4);
    RET64((u64)r);
    return 0;
}
H(ftruncate) {
    SYSRET(ftruncate((int)A(0), (off_t)((u64)A(1) | (u64)A(2) << 32)));
    return 0;
}
H(fsync) {
    SYSRET(fsync((int)A(0)));
    return 0;
}
H(fcntl) {
    SYSRET(fcntl((int)A(0), (int)A(1), A(2)));
    return 0;
}
H(flock) {
    SYSRET(flock((int)A(0), (int)A(1)));
    return 0;
}
H(mkdir) {
    SYSRET(mkdir(GS(A(0)), A(1)));
    return 0;
}
H(rmdir) {
    SYSRET(rmdir(GS(A(0))));
    return 0;
}
H(unlink) {
    SYSRET(unlink(GS(A(0))));
    return 0;
}
H(chmod) {
    SYSRET(chmod(GS(A(0)), A(1)));
    return 0;
}
H(getcwd) {
    char *r = getcwd(GS(A(0)), A(1));
    RET(r ? A(0) : 0);
    return 0;
}
H(basename) {
    char t[1024];
    snprintf(t, sizeof t, "%s", GS(A(0)));
    RET(gstr(basename(t)));
    return 0;
}
H(utime) {
    RET(0);
    return 0;
}
H(copyfile) {
    RET((u32)-1);
    return 0;
}
H(ioctl) {
    wr(errno_slot(), ENOTTY, 4);
    RET((u32)-1);
    return 0;
}
H(neg1) {
    wr(errno_slot(), ENOSYS, 4);
    RET((u32)-1);
    return 0;
} // sockets, poll, select, ...
H(null0) {
    RET(0);
    return 0;
}
H(strerror) {
    RET(gstr(strerror((int)A(0))));
    return 0;
}

// directories: DIR* is a small integer handle; readdir fills a guest struct dirent {ino, reclen:2, type:1, namlen:1, name[256]}
static DIR *dirs[16];
static u32 dirent_buf[16];
H(opendir) {
    DIR *d = opendir(GS(A(0)));
    if (!d) {
        wr(errno_slot(), errno, 4);
        RET(0);
        return 0;
    }
    for (int i = 1; i < 16; i++)
        if (!dirs[i]) {
            dirs[i] = d;
            dirent_buf[i] = hmalloc(264);
            RET(DIR_HANDLE_BASE + i * 16);
            return 0;
        }
    closedir(d);
    RET(0);
    return 0;
}
H(readdir) {
    int i = (A(0) - DIR_HANDLE_BASE) / 16;
    struct dirent *e = readdir(dirs[i]);
    if (!e) {
        RET(0);
        return 0;
    }
    u32 g = dirent_buf[i];
    memset(P(g), 0, 264);
    wr(g, (u32)e->d_ino, 4);
    wr(g + 4, 264, 2);
    *P(g + 6) = e->d_type;
    *P(g + 7) = e->d_namlen;
    strncpy(GS(g + 8), e->d_name, 255);
    RET(g);
    return 0;
}
H(closedir) {
    int i = (A(0) - DIR_HANDLE_BASE) / 16;
    closedir(dirs[i]);
    dirs[i] = NULL;
    RET(0);
    return 0;
}

// time: i386 struct tm = 9 ints + gmtoff(4) + zone(4)
static void put_tm(u32 g, const struct tm *t) {
    int f[9] = {t->tm_sec,  t->tm_min,  t->tm_hour, t->tm_mday, t->tm_mon,
                t->tm_year, t->tm_wday, t->tm_yday, t->tm_isdst};
    for (int i = 0; i < 9; i++)
        wr(g + 4 * i, f[i], 4);
    wr(g + 36, (u32)t->tm_gmtoff, 4);
    wr(g + 40, 0, 4);
}
static void get_tm(u32 g, struct tm *t) {
    memset(t, 0, sizeof *t);
    t->tm_sec = rd(g, 4);
    t->tm_min = rd(g + 4, 4);
    t->tm_hour = rd(g + 8, 4);
    t->tm_mday = rd(g + 12, 4);
    t->tm_mon = rd(g + 16, 4);
    t->tm_year = rd(g + 20, 4);
    t->tm_wday = rd(g + 24, 4);
    t->tm_yday = rd(g + 28, 4);
    t->tm_isdst = rd(g + 32, 4);
}
static u32 tm_static;
H(localtime) {
    time_t t = (i32)rd(A(0), 4);
    struct tm r;
    localtime_r(&t, &r);
    if (!tm_static)
        tm_static = hmalloc(48);
    put_tm(tm_static, &r);
    RET(tm_static);
    return 0;
}
H(gmtime) {
    time_t t = (i32)rd(A(0), 4);
    struct tm r;
    gmtime_r(&t, &r);
    if (!tm_static)
        tm_static = hmalloc(48);
    put_tm(tm_static, &r);
    RET(tm_static);
    return 0;
}
H(localtime_r) {
    time_t t = (i32)rd(A(0), 4);
    struct tm r;
    localtime_r(&t, &r);
    put_tm(A(1), &r);
    RET(A(1));
    return 0;
}
H(mktime) {
    struct tm t;
    get_tm(A(0), &t);
    RET((u32)mktime(&t));
    return 0;
}
H(strftime) {
    struct tm t;
    get_tm(A(3), &t);
    RET(strftime(GS(A(0)), A(1), GS(A(2)), &t));
    return 0;
}
H(asctime) {
    struct tm t;
    get_tm(A(0), &t);
    RET(gstr(asctime(&t)));
    return 0;
}
H(rand) {
    RET(rand());
    return 0;
}
H(srand) {
    srand(A(0));
    return 0;
}
H(finite) {
    RET(isfinite(Ad(c, 0)));
    return 0;
}
H(frexp) {
    int e;
    double m = frexp(Ad(c, 0), &e);
    wr(A(2), e, 4);
    fpush(c, m);
    return 0;
}
H(modff) {
    float ip;
    float f = modff(Af(c, 0), &ip);
    u32 w;
    memcpy(&w, &ip, 4);
    wr(A(1), w, 4);
    fpush(c, f);
    return 0;
}
H(strtold) {
    char *e;
    double v = strtod(GS(A(0)), &e);
    ptr_out(A(1), A(0), e);
    fpush(c, v);
    return 0;
}
H(strcoll) {
    RET(strcmp(GS(A(0)), GS(A(1))));
    return 0;
}
H(inet_addr) {
    RET(0xffffffffu);
    return 0;
}

// qsort: shell sort over guest memory, comparator is a guest callback
H(qsort) {
    u32 base = A(0), n = A(1), sz = A(2), cmp = A(3);
    u8 *tmp = malloc(sz);
    for (u32 gap = n / 2; gap > 0; gap /= 2)
        for (u32 i = gap; i < n; i++) {
            memcpy(tmp, P(base + i * sz), sz);
            u32 j = i;
            for (; j >= gap; j -= gap) {
                u32 args[2] = {base + (j - gap) * sz, 0};
                u32 tmpg = hmalloc(sz);
                memcpy(P(tmpg), tmp, sz);
                args[1] = tmpg;
                if ((i32)call_guest(c, cmp, 2, args) <= 0)
                    break;
                memcpy(P(base + j * sz), P(base + (j - gap) * sz), sz);
            }
            memcpy(P(base + j * sz), tmp, sz);
        }
    free(tmp);
    return 0;
}

// sscanf: %d %i %u %x %f %lf %s %c %% with optional width and l/h modifiers; stores through guest pointers
static int do_sscanf(const char *s, const char *p, u32 ap, int *eof) {
    int n = 0;
    *eof = 0;
    for (; *p; p++) {
        if (isspace((u8)*p)) {
            while (isspace((u8)*s))
                s++;
            continue;
        }
        if (*p != '%') {
            if (*s != *p)
                break;
            s++;
            continue;
        }
        p++;
        int width = 0, lng = 0, skip = 0;
        if (*p == '*') {
            skip = 1;
            p++;
        }
        while (isdigit((u8)*p))
            width = width * 10 + *p++ - '0';
        while (*p == 'l' || *p == 'h') {
            if (*p == 'l')
                lng++;
            p++;
        }
        char cv = *p;
        char *e;
        if (cv == '[') { // %[set] / %[^set]: copy matching characters, no leading-space skip
            int neg = 0;
            const char *q = ++p;
            if (*q == '^') {
                neg = 1;
                q++;
            }
            const char *set0 = q;
            if (*q == ']')
                q++;
            while (*q && *q != ']')
                q++;
            char set[256];
            size_t sl = (size_t)(q - set0);
            if (sl > 255)
                sl = 255;
            memcpy(set, set0, sl);
            set[sl] = 0;
            p = q;
            u32 dst2 = skip ? 0 : rd(ap, 4);
            if (!skip)
                ap += 4;
            int k = 0;
            while (*s && (!width || k < width)) {
                int in = 0;
                for (size_t i = 0; i < sl; i++) {
                    if (i + 2 < sl && set[i + 1] == '-') {
                        if ((u8)*s >= (u8)set[i] && (u8)*s <= (u8)set[i + 2])
                            in = 1;
                        i += 2;
                    } else if (*s == set[i])
                        in = 1;
                }
                if (in == neg)
                    break;
                if (!skip)
                    *P(dst2 + k) = *s;
                s++;
                k++;
            }
            if (!k)
                break;
            if (!skip) {
                *P(dst2 + k) = 0;
                n++;
            }
            continue;
        }
        if (cv == '%') {
            if (*s != '%')
                break;
            s++;
            continue;
        }
        if (cv != 'c')
            while (isspace((u8)*s))
                s++;
        if (!*s) {
            if (!n)
                *eof = 1;
            break;
        }
        u32 dst = skip ? 0 : rd(ap, 4);
        if (!skip)
            ap += 4;
        if (cv == 'd' || cv == 'i' || cv == 'u' || cv == 'x') {
            long v = (cv == 'x') ? strtol(s, &e, 16) : strtol(s, &e, cv == 'i' ? 0 : 10);
            if (e == s)
                break;
            s = e;
            if (!skip) {
                wr(dst, (u32)v, 4);
                n++;
            }
        } else if (cv == 'f' || cv == 'e' || cv == 'g') {
            double v = strtod(s, &e);
            if (e == s)
                break;
            s = e;
            if (!skip) {
                if (lng) {
                    u64 t;
                    memcpy(&t, &v, 8);
                    wr(dst, (u32)t, 4);
                    wr(dst + 4, (u32)(t >> 32), 4);
                } else {
                    float f = (float)v;
                    u32 w;
                    memcpy(&w, &f, 4);
                    wr(dst, w, 4);
                }
                n++;
            }
        } else if (cv == 's') {
            int k = 0;
            while (*s && !isspace((u8)*s) && (!width || k < width)) {
                if (!skip)
                    *P(dst + k) = *s;
                s++;
                k++;
            }
            if (!skip) {
                *P(dst + k) = 0;
                n++;
            }
        } else if (cv == 'c') {
            if (!skip) {
                *P(dst) = *s;
                n++;
            }
            s++;
        } else
            die("sscanf: unsupported %%%c", cv);
    }
    return n;
}
H(sscanf) {
    int eof;
    int n = do_sscanf(GS(A(0)), GS(A(1)), c->r[ESP] + 12, &eof);
    RET(eof ? (u32)-1 : (u32)n);
    return 0;
}
static void narrow_w(u32 w, char *out, size_t cap) {
    size_t k = 0;
    for (; rd(w + 4 * k, 4) && k + 1 < cap; k++) {
        u32 ch = rd(w + 4 * k, 4);
        out[k] = ch < 128 ? (char)ch : '?';
    }
    out[k] = 0;
}
H(swscanf) {
    char s[2048], f[512];
    int eof;
    narrow_w(A(0), s, sizeof s);
    narrow_w(A(1), f, sizeof f);
    int n = do_sscanf(s, f, c->r[ESP] + 12, &eof);
    RET(eof ? (u32)-1 : (u32)n);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry posix_imports[] = {
    IMPORT(stat), IMPORT(lstat), IMPORT(stat64), IMPORT(fstat), IMPORT(open), IMPORT(close), IMPORT(read),
    IMPORT(write), IMPORT(writev), IMPORT(lseek), IMPORT(ftruncate), IMPORT(fsync), IMPORT(fcntl),
    IMPORT(flock), IMPORT(mkdir), IMPORT(rmdir), IMPORT(unlink), IMPORT(chmod), IMPORT(getcwd),
    IMPORT(basename), IMPORT(utime), IMPORT(copyfile), IMPORT(ioctl), IMPORT(strerror), IMPORT(opendir),
    IMPORT(readdir), IMPORT(closedir), IMPORT(localtime), IMPORT(gmtime), IMPORT(localtime_r),
    IMPORT(mktime), IMPORT(strftime), IMPORT(asctime), IMPORT(rand), IMPORT(srand), IMPORT(finite),
    IMPORT(frexp), IMPORT(modff), IMPORT(strtold), IMPORT(strcoll), IMPORT(inet_addr), IMPORT(qsort),
    IMPORT(sscanf), IMPORT(swscanf), {"_accept", h_neg1}, {"_bind", h_neg1}, {"_connect", h_neg1},
    {"_listen", h_neg1}, {"_getsockname", h_neg1}, {"_getsockopt", h_neg1}, {"_setsockopt", h_neg1},
    {"_socket", h_neg1}, {"_recv", h_neg1}, {"_recvfrom", h_neg1}, {"_send", h_neg1}, {"_sendto", h_neg1},
    {"_select", h_neg1}, {"_poll", h_neg1}, {"_popen", h_neg1}, {"_system", h_neg1}, {"_alarm", h_neg1},
    {0, 0}};
// clang-format on
