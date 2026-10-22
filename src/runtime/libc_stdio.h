// libc_stdio.h: stdio. FILE* values are fake guest objects (FILE_BASE + 0x100*n) mapped to host FILE*.
//

H(fopen) {
    if (log_imports || DBG_FLAG("IOLOG"))
        fprintf(stderr, "[io] fopen(%s, %s)\n", GS(A(0)), GS(A(1)));
    FILE *f = fopen(GS(A(0)), GS(A(1)));
    RET(f ? file_new(f) : 0);
    return 0;
}
H(fclose) {
    RET(fclose(file_of(A(0))));
    return 0;
}
H(popen) {
    FILE *f = popen(GS(A(0)), GS(A(1)));
    RET(f ? file_new(f) : 0);
    return 0;
}
H(pclose) {
    RET(pclose(file_of(A(0))));
    return 0;
}
H(fread) {
    RET(fread(P(A(0)), A(1), A(2), file_of(A(3))));
    return 0;
}
H(fwrite) {
    RET(fwrite(P(A(0)), A(1), A(2), file_of(A(3))));
    return 0;
}
H(fseek) {
    RET(fseek(file_of(A(0)), (int)A(1), (int)A(2)));
    return 0;
}
H(ftell) {
    RET(ftell(file_of(A(0))));
    return 0;
}
H(fflush) {
    RET(A(0) ? fflush(file_of(A(0))) : fflush(NULL));
    return 0;
}
H(fgets) {
    char *r = fgets(GS(A(0)), (int)A(1), file_of(A(2)));
    RET(r ? A(0) : 0);
    return 0;
}
H(fputs) {
    RET(fputs(GS(A(0)), file_of(A(1))));
    return 0;
}
H(fputc) {
    RET(fputc((int)A(0), file_of(A(1))));
    return 0;
}
H(getc) {
    RET(getc(file_of(A(0))));
    return 0;
}
H(ungetc) {
    RET(ungetc((int)A(0), file_of(A(1))));
    return 0;
}
H(ferror) {
    RET(ferror(file_of(A(0))));
    return 0;
}
H(fileno) {
    RET(fileno(file_of(A(0))));
    return 0;
}
H(setvbuf) {
    RET(0);
    return 0;
}
H(flockfile) {
    return 0;
}
H(funlockfile) {
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry libc_stdio_imports[] = {
    IMPORT(fopen), IMPORT(fclose), IMPORT(popen), IMPORT(pclose), IMPORT(fread), IMPORT(fwrite),
    IMPORT(fseek), IMPORT(ftell), IMPORT(fflush), IMPORT(fgets), IMPORT(fputs), IMPORT(fputc), IMPORT(getc),
    IMPORT(ungetc), IMPORT(ferror), IMPORT(fileno), IMPORT(setvbuf), IMPORT(flockfile), IMPORT(funlockfile),
    {0, 0}};
// clang-format on
