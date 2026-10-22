// macho.h: the i386 Mach-O loader.
//
// load_image() maps segments into guest memory, undoes the code scrambling of the main executable (protection.h), builds the
// symbol table, applies rebase/bind opcodes and records the Objective-C sections the shim needs. The file buffer is kept alive
// because symbol names point into it.

static Image *load_image(const char *path, u32 slide, int is_main) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *file = malloc(len);
    if (fread(file, 1, len, f) != (size_t)len) {
        fclose(f);
        free(file);
        return NULL;
    }
    fclose(f);
    struct mach_header *mh = (void *)file;
    if ((size_t)len < sizeof *mh || mh->magic != MH_MAGIC || mh->cputype != CPU_TYPE_I386) {
        free(file);
        return NULL;
    }
    if (nimages == 8) {
        free(file);
        return NULL;
    }
    if (is_main) {
        const char *build = identify_build(file, len);
        if (build)
            printf("recognised build: %s\n", build);
        else
            printf("unrecognised executable: binary patches will not be applied\n");
    }
    Image *im = &images[nimages++];
    memset(im, 0, sizeof *im);
    snprintf(im->path, sizeof im->path, "%s", path);
    im->slide = slide;
    im->is_main = is_main;
    im->lo = ~0u;
    objc_class_sec = objc_class_size = objc_cls_refs = objc_cls_refs_size = 0;
    u32 text_addr = 0, text_size = 0, text_off = 0;
    struct dyld_info_command *di = NULL;
    struct symtab_command *st = NULL;
    u8 *lc = file + sizeof *mh;
    for (u32 i = 0; i < mh->ncmds; i++, lc += ((struct load_command *)lc)->cmdsize) {
        struct load_command *l = (void *)lc;
        if (l->cmd == LC_SEGMENT) {
            struct segment_command *s = (void *)lc;
            im->segs[im->nsegs].vmaddr = s->vmaddr;
            im->segs[im->nsegs].vmsize = s->vmsize;
            im->nsegs++;
            if (s->vmsize && s->vmaddr + slide < im->lo)
                im->lo = s->vmaddr + slide;
            if (s->vmaddr + s->vmsize + slide > im->hi)
                im->hi = s->vmaddr + s->vmsize + slide;
            if (s->filesize)
                memcpy(P(s->vmaddr + slide), file + s->fileoff, s->filesize);
            struct section *sec = (void *)(s + 1);
            for (u32 k = 0; k < s->nsects; k++, sec++) {
                u32 sa = sec->addr + slide;
                if (is_main && !strncmp(sec->sectname, "__text", 16) &&
                    !strncmp(sec->segname, "__TEXT", 16)) {
                    text_addr = sec->addr + slide;
                    text_size = sec->size;
                    text_off = sec->offset;
                }
                if (!strncmp(sec->segname, "__OBJC", 16)) {
                    if (!strncmp(sec->sectname, "__class", 16)) {
                        objc_class_sec = sa;
                        objc_class_size = sec->size;
                    }
                    if (!strncmp(sec->sectname, "__cls_refs", 16)) {
                        objc_cls_refs = sa;
                        objc_cls_refs_size = sec->size;
                    }
                }
                if (!strncmp(sec->sectname, "__mod_init_func", 16)) {
                    im->init_addr = sa;
                    im->init_size = sec->size;
                }
            }
        } else if (l->cmd == LC_DYLD_INFO || l->cmd == LC_DYLD_INFO_ONLY)
            di = (void *)lc;
        else if (l->cmd == LC_SYMTAB)
            st = (void *)lc;
        else if (l->cmd == LC_UNIXTHREAD)
            im->entry = ((u32 *)lc)[2 + 2 + 10]; // cmd,size,flavor,count, then eip is field 10
    }
    if (text_size)
        unscramble_text(text_addr, text_size, text_off, file + text_off, im->entry);
    if (!di || !st) {
        fprintf(stderr, "%s: no LC_DYLD_INFO/LC_SYMTAB\n", path);
        exit(1);
    }
    st_init(&im->syms, st->nsyms);
    struct nlist *nl = (void *)(file + st->symoff);
    for (u32 i = 0; i < st->nsyms; i++)
        if ((nl[i].n_type & N_TYPE) == N_SECT && nl[i].n_value)
            st_add(&im->syms, (char *)file + st->stroff + nl[i].n_un.n_strx, nl[i].n_value + slide);
    cur = im;
    if (di->rebase_size)
        rebase_run(file + di->rebase_off, di->rebase_size);
    bind_run(file + di->bind_off, di->bind_size, 0);
    bind_run(file + di->lazy_bind_off, di->lazy_bind_size, 1);
    bind_run(file + di->weak_bind_off, di->weak_bind_size,
             0); // coalescing: each image resolves weak symbols to itself
    if (is_main)
        printf("loaded %s: %d segments, entry=%08x, %u own symbols, %d import binds -> %d distinct imports, "
               "%d self-binds\n",
               path, im->nsegs, im->entry, im->syms.n, nbound, nthunks, nself);
    else
        printf("loaded %s at %08x-%08x (%u symbols)\n", path, im->lo, im->hi, im->syms.n);
    // the file buffer is kept: symbol names in the SymTab point into it
    return im;
}
