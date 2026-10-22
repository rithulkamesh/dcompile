// fs.h: CoreServices File Manager (FSRef / fork I/O) on top of POSIX.
//
// An FSRef is 80 opaque bytes holding {index into a path table + 1, magic}. Fork reference numbers are 1-based slots holding host
// file descriptors. PBReadForkAsync reads synchronously and queues the completion callback, which the scheduler runs on a helper
// guest thread.

static char *fs_paths[1024];
static int nfs_paths;
static int fs_forks[64]; // fd + 1; 0 = free

static int fs_ref_make(u32 ref, const char *path) {
    int i;
    for (i = 0; i < nfs_paths; i++)
        if (!strcmp(fs_paths[i], path))
            break;
    if (i == nfs_paths) {
        if (nfs_paths == 1024)
            die("FSRef table full");
        fs_paths[nfs_paths++] = strdup(path);
    }
    memset(P(ref), 0, 80);
    wr(ref, i + 1, 4);
    wr(ref + 4, 0x64634653, 4);
    return 0;
}
static const char *fs_ref_path(u32 ref) {
    u32 i = rd(ref, 4);
    return i && i <= (u32)nfs_paths ? fs_paths[i - 1] : NULL;
}

H(FSPathMakeRef) {
    if (DBG_FLAG("IOLOG"))
        fprintf(stderr, "[io] FSPathMakeRef(%s)\n",
                GS(A(0))); // (const UInt8 *path, FSRef *ref, Boolean *isDirectory)
    struct stat st;
    if (stat(GS(A(0)), &st)) {
        RET((u32)-43);
        return 0;
    } // fnfErr
    char real[PATH_MAX];
    fs_ref_make(A(1), realpath(GS(A(0)), real) ? real : GS(A(0)));
    if (A(2))
        *P(A(2)) = S_ISDIR(st.st_mode) ? 1 : 0;
    RET(0);
    return 0;
}
H(FSGetDataForkName) {
    memset(P(A(0)), 0, 2);
    RET(0);
    return 0;
} // HFSUniStr255 with length 0 = the data fork
H(FSOpenFork) {
    if (DBG_FLAG("IOLOG"))
        fprintf(stderr, "[io] FSOpenFork(%s)\n",
                fs_ref_path(A(0)) ? fs_ref_path(A(0))
                                  : "?"); // (const FSRef *, nameLen, forkName, SInt8 perm, FSIORefNum *out)
    const char *p = fs_ref_path(A(0));
    if (!p) {
        RET((u32)-36);
        return 0;
    }
    int perm = (int)(i8)A(3), fl = (perm & 2) ? ((perm & 1) || perm == 3 ? O_RDWR : O_WRONLY) : O_RDONLY;
    int fd = open(p, fl);
    if (fd < 0) {
        RET((u32)-43);
        return 0;
    }
    for (int i = 1; i < 64; i++)
        if (!fs_forks[i]) {
            fs_forks[i] = fd + 1;
            wr(A(4), i, 2);
            RET(0);
            return 0;
        }
    close(fd);
    RET((u32)-42);
    return 0; // tmfoErr
}
H(FSCloseFork) {
    int r = (int)(i16)A(0);
    if (r > 0 && r < 64 && fs_forks[r]) {
        close(fs_forks[r] - 1);
        fs_forks[r] = 0;
    }
    RET(0);
    return 0;
}
H(FSGetForkSize) {
    int r = (int)(i16)A(0);
    if (r <= 0 || r >= 64 || !fs_forks[r]) {
        RET((u32)-51);
        return 0;
    }
    struct stat st;
    fstat(fs_forks[r] - 1, &st);
    wr(A(1), (u32)st.st_size, 4);
    wr(A(1) + 4, (u32)((u64)st.st_size >> 32), 4);
    RET(0);
    return 0;
}
H(PBReadForkAsync) {
    nreads++;
    u32 p = A(0);
    int r = (int)(i16)rd(p + 0x18, 2);
    i16 res = 0;
    u32 got = 0;
    if (r <= 0 || r >= 64 || !fs_forks[r])
        res = -51; // rfNumErr
    else {
        int fd = fs_forks[r] - 1;
        u32 mode = rd(p + 0x2c, 2) & 3, req = rd(p + 0x24, 4);
        off_t off = (off_t)((u64)rd(p + 0x2e, 4) | (u64)rd(p + 0x32, 4) << 32);
        if (mode == 1)
            lseek(fd, off, SEEK_SET);
        else if (mode == 2)
            lseek(fd, off, SEEK_END);
        else if (mode == 3)
            lseek(fd, off, SEEK_CUR);
        ssize_t n = read(fd, P(rd(p + 0x20, 4)), req);
        got = n < 0 ? 0 : (u32)n;
        if (n < 0)
            res = -36;
        else if (got < req)
            res = -39; // ioErr / eofErr
    }
    wr(p + 0x36, got, 4);
    wr(p + 0x10, (u16)res, 2);
    u32 cb = rd(p + 0x0c, 4);
    if (cb && iocb_n < 256) {
        iocb_q[iocb_n].fn = cb;
        iocb_q[iocb_n++].arg = p;
    }
    RET(0);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry fs_imports[] = {
    IMPORT(FSPathMakeRef), IMPORT(FSGetDataForkName), IMPORT(FSOpenFork), IMPORT(FSCloseFork),
    IMPORT(FSGetForkSize), IMPORT(PBReadForkAsync),
    {0, 0}};
// clang-format on
