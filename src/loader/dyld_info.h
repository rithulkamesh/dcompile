// dyld_info.h: the compressed dyld opcode streams (rebase, bind, lazy bind, weak bind).
//
// Interprets LC_DYLD_INFO opcodes: rebasing adds the image slide to pointers; binding resolves each imported symbol with
// thunk_for() or, for symbols the images define themselves, to the defining image.

static void rebase_run(const u8 *p, size_t n) {
    int seg = 0;
    u64 off = 0;
    size_t i = 0;
#define DOREBASE()                                                                                           \
    do {                                                                                                     \
        u32 ad = cur->segs[seg].vmaddr + cur->slide + (u32)off;                                              \
        wr(ad, rd(ad, 4) + cur->slide, 4);                                                                   \
        off += 4;                                                                                            \
    } while (0)
    while (i < n) {
        u8 b = p[i++], op = b & 0xF0, imm = b & 0x0F;
        switch (op) {
        case REBASE_OPCODE_DONE:
            return;
        case REBASE_OPCODE_SET_TYPE_IMM:
            break;
        case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
            seg = imm;
            off = uleb(p, &i);
            break;
        case REBASE_OPCODE_ADD_ADDR_ULEB:
            off += uleb(p, &i);
            break;
        case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
            off += imm * 4;
            break;
        case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
            for (int k = 0; k < imm; k++)
                DOREBASE();
            break;
        case REBASE_OPCODE_DO_REBASE_ULEB_TIMES: {
            u64 cnt = uleb(p, &i);
            for (u64 k = 0; k < cnt; k++)
                DOREBASE();
            break;
        }
        case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
            DOREBASE();
            off += uleb(p, &i);
            break;
        case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB: {
            u64 cnt = uleb(p, &i), skip = uleb(p, &i);
            for (u64 k = 0; k < cnt; k++) {
                DOREBASE();
                off += skip;
            }
            break;
        }
        default:
            fprintf(stderr, "bad rebase opcode %02x\n", b);
            exit(1);
        }
    }
}

static void bind_run(const u8 *p, size_t n, int lazy) {
    int ord = 0, type = 1;
    const char *sym = "";
    i64 add = 0;
    int seg = 0;
    u64 off = 0;
    size_t i = 0;
#define DOBIND()                                                                                             \
    do {                                                                                                     \
        u32 ad = cur->segs[seg].vmaddr + cur->slide + (u32)off;                                              \
        u32 own = ord > 0 ? 0 : st_lookup(&cur->syms, sym);                                                  \
        if (own) {                                                                                           \
            wr(ad, own + (u32)add, 4);                                                                       \
            nself++;                                                                                         \
        } else {                                                                                             \
            wr(ad, thunk_for(sym) + (u32)add, 4);                                                            \
            nbound++;                                                                                        \
        }                                                                                                    \
    } while (0)
    while (i < n) {
        u8 b = p[i++], op = b & 0xF0, imm = b & 0x0F;
        switch (op) {
        case BIND_OPCODE_DONE:
            if (!lazy)
                return;
            break;
        case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
            ord = imm;
            break;
        case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
            ord = (int)uleb(p, &i);
            break;
        case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
            ord = imm ? (i8)(0xF0 | imm) : 0;
            break;
        case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
            sym = (const char *)p + i;
            i += strlen(sym) + 1;
            break;
        case BIND_OPCODE_SET_TYPE_IMM:
            type = imm;
            break;
        case BIND_OPCODE_SET_ADDEND_SLEB:
            add = sleb(p, &i);
            break;
        case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
            seg = imm;
            off = uleb(p, &i);
            break;
        case BIND_OPCODE_ADD_ADDR_ULEB:
            off += uleb(p, &i);
            break;
        case BIND_OPCODE_DO_BIND:
            DOBIND();
            off += 4;
            break;
        case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
            DOBIND();
            off += 4 + uleb(p, &i);
            break;
        case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
            DOBIND();
            off += 4 + imm * 4;
            break;
        case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB: {
            u64 cnt = uleb(p, &i), skip = uleb(p, &i);
            for (u64 k = 0; k < cnt; k++) {
                DOBIND();
                off += 4 + skip;
            }
            break;
        }
        default:
            fprintf(stderr, "bad bind opcode %02x\n", b);
            exit(1);
        }
    }
    (void)type;
}

// Maps one i386 Mach-O (main executable or dylib) into guest memory at `slide`, rebases and binds it.
// Imports resolve to host thunks by name; self/weak/flat binds resolve to the image's own symbols.
