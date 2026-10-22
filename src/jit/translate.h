// translate.h: basic-block translation driver.
//
// Decodes forward from a guest address until a control-flow instruction, runs the flag-liveness analysis, calls the generators
// and links the finished block into the cache.

static void write_tail(u32 *bsite, u32 eip) {
    for (int i = 0; i < nexits; i++) {
        u32 *stub = jp;
        a_movi(0, exits[i].tgt);
        a_b(t_exit);
        *exits[i].site = 0x14000000u | ((u32)(stub - exits[i].site) & 0x3ffffff);
        if (exits[i].tgt < THUNK_BASE && !jnochain) {
            Pend *p = &pend_pool[npend++];
            u32 h = (exits[i].tgt >> 2) & 0x3fff;
            p->site = exits[i].site;
            p->tgt = exits[i].tgt;
            p->next = phead[h];
            phead[h] = p;
        }
    }
    u32 *lb = jp; // budget exhausted before this block: put the count back, leave with eip = block start
    a_memi(STRXI, 9, XC, OFF(jb));
    a_movi(0, eip);
    a_b(t_exit);
    *bsite |= ((u32)(lb - bsite) & 0x3fff) << 5;
}

static Block *jit_translate(u32 eip) {
    if (nblocks >= MAXBLK - 2 || cache_top > cache_lim || npend > (1u << 20) - 4 * MAXB)
        jit_flush();
    Ins ins[MAXB + 1];
    int n = 0;
    u32 a = eip, ended = 0;
    while (n < MAXB) {
        decode(a, &ins[n]);
        a += ins[n].len;
        int cl = ins[n].cls;
        n++;
        if (cl != CL_NAT && cl != CL_FB) {
            ended = 1;
            break;
        }
    }
    Ins *T = &ins[n - 1];
    u8 lo;
    if (!ended)
        lo = flags_in(a);
    else
        switch (T->cls) {
        case CL_JMP:
        case CL_CALL:
            lo = flags_in(T->tgt);
            break;
        case CL_JCC:
            lo = flags_in(T->tgt) | flags_in(a);
            break;
        case CL_RET:
        case CL_JMPI:
        case CL_CALLI:
            lo = 0;
            break;
        default:
            lo = F_ALL;
        }
    u8 li;
    {
        u8 L = lo;
        for (int i = n - 1; i >= 0; i--) {
            ins[i].live = L;
            L = (L & ~ins[i].fw) | ins[i].fr;
        }
        li = L;
    }

    wp(0);
    u32 *start = cache_top;
    jp = start;
    nexits = 0;
    fstate = FS_STRUCT;
    cinv = 0;
    need_load = 0;
    need_store = 1; // host regs may be newer than the CPU struct (chained entry)
    a_memi(LDRXI, 9, XC, OFF(jb));
    a_immx(SUBI, 10, 9, n);
    a_memi(STRXI, 10, XC, OFF(jb));
    u32 *bsite = jp;
    a_tbnz(9, 63, 0);
    int last = ended ? n - 1 : n;
    for (int i = 0; i < last; i++)
        gen_ins(&ins[i]);
    if (!ended) {
        if (need_load)
            sync_load();
        flush_flags(lo);
        set_flo(lo);
        exit_to(a);
    } else
        switch (T->cls) {
        case CL_JMP:
            if (need_load)
                sync_load();
            flush_flags(T->live);
            set_flo(lo);
            exit_to(T->tgt);
            break;
        case CL_CALL:
            if (need_load)
                sync_load();
            a_movi(0, a);
            a_imm(SUBI, GR(ESP), GR(ESP), 4);
            st(4, 0, GR(ESP));
            flush_flags(T->live);
            set_flo(lo);
            exit_to(T->tgt);
            break;
        case CL_RET:
            if (need_load)
                sync_load();
            a_mem(LDRW, 0, XG, GR(ESP));
            add_const(GR(ESP), GR(ESP), 4 + (T->op == 0xC2 ? T->imm : 0), 9);
            set_flo(0);
            a_b(t_ibtc);
            break;
        case CL_JMPI:
        case CL_CALLI: {
            if (need_load)
                sync_load();
            Opd s = opd_rm(T);
            int v = rdv(s, 4, 0, 0);
            if (v != 0)
                a_mov(0, v);
            if (T->cls == CL_CALLI) {
                a_movi(1, a);
                a_imm(SUBI, GR(ESP), GR(ESP), 4);
                st(4, 1, GR(ESP));
            }
            set_flo(0);
            a_b(t_ibtc);
            break;
        }
        case CL_JCC:
            set_flo(lo);
            gen_jcc(T);
            break;
        default: // CL_TFB: execute it with the interpreter, then continue at wherever it left eip
            gen_fb(T);
            set_flo(F_ALL);
            a_memi(LDRWI, 0, XC, OFF(eip));
            a_b(t_exit_ns);
            break;
        }
    write_tail(bsite, eip);
    Block *b = &bpool[nblocks++];
    b->guest = eip;
    b->gend = a;
    b->code = start;
    b->end = jp;
    b->nins = n;
    b->liveout = lo;
    b->livein = li;
    blk_insert(b);
    ibtc_put(b);
    if (!jnochain) { // chain blocks that were waiting for this one
        Pend **pp = &phead[(eip >> 2) & 0x3fff];
        while (*pp) {
            Pend *p = *pp;
            if (p->tgt == eip) {
                *p->site = 0x14000000u | ((u32)(start - p->site) & 0x3ffffff);
                sys_icache_invalidate(p->site, 4);
                *pp = p->next;
            } else
                pp = &p->next;
        }
    }
    cache_top = jp;
    jit_prot_range(eip, a);
    wp(1);
    sys_icache_invalidate(start, (u8 *)jp - (u8 *)start);
    st_trans++;
    if (jdump && (jdump == 1 ||
                  jdump == eip)) { // JITDUMP=hexaddr (or 1 = all): guest bytes and host words, see jit_dis.sh
        fprintf(stderr, "[jitdump] block %08x..%08x live-out=%02x live-in=%02x\n  guest:", eip, a, lo, li);
        for (u32 q = eip; q < a; q++)
            fprintf(stderr, " %02x", *P(q));
        fprintf(stderr, "\n  host:");
        for (u32 *q = start; q < jp; q++)
            fprintf(stderr, " %08x", *q);
        fprintf(stderr, "\n");
    }
    return b;
}
