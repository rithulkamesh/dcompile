// files.h: the guest-visible FILE table.
//
// stdio FILE* values are fake guest objects at FILE_BASE + 0x100*n; file_of() maps them back to the host FILE*.

static FILE *files[64];
static int nfiles;
static u32 file_new(FILE *f) {
    files[nfiles] = f;
    return FILE_BASE + 0x100 * nfiles++;
}
static FILE *file_of(u32 a) {
    u32 i = (a - FILE_BASE) / 0x100;
    if (a < FILE_BASE || i >= (u32)nfiles)
        die("bad FILE* %08x", a);
    return files[i];
}
