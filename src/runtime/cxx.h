// cxx.h: the slice of libstdc++ 4.2 (i386 ABI) imported by the game: COW std::string, the red-black tree helpers behind std::map/set,
//
// operator new/delete and the __cxa guard helpers. iostreams are no-op stubs.
//
// std::string layout: the object is one pointer _M_p to the characters; just before them sits a _Rep {length, capacity, refcount}.

#define NPOS 0xffffffffu
static u32 empty_rep(void) {
    static u32 a;
    if (!a)
        a = data_sym("__ZNSs4_Rep20_S_empty_rep_storageE");
    return a;
} // _Rep*
static u32 s_rep(u32 s) {
    return rd(s, 4) - 12;
}
static u32 s_len(u32 s) {
    return rd(s_rep(s), 4);
}
static u32 s_cap(u32 s) {
    return rd(s_rep(s) + 4, 4);
}
static const char *s_ptr(u32 s) {
    return GS(rd(s, 4));
}

static u32 rep_create(u32 cap) { // returns pointer to the characters of a new rep
    u32 r = hmalloc(12 + cap + 1);
    wr(r, 0, 4);
    wr(r + 4, cap, 4);
    wr(r + 8, 0, 4);
    *P(r + 12) = 0;
    return r + 12;
}
static void s_release(u32 s) {
    u32 r = s_rep(s);
    if (r != empty_rep())
        free_guest(r);
}
static void s_assign(u32 s, const char *bytes, u32 n) { // *s = bytes[0..n); `bytes` is host memory
    u32 d = rep_create(n);
    memcpy(P(d), bytes, n);
    *P(d + n) = 0;
    wr(d - 12, n, 4);
    u32 old = s_rep(s);
    wr(s, d, 4);
    if (old != empty_rep())
        free_guest(old);
}
static void s_init(u32 s, const char *bytes, u32 n) { // placement-construct
    if (!n) {
        wr(s, empty_rep() + 12, 4);
        return;
    }
    u32 d = rep_create(n);
    memcpy(P(d), bytes, n);
    *P(d + n) = 0;
    wr(d - 12, n, 4);
    wr(s, d, 4);
}
static char *s_dup(u32 s) {
    u32 n = s_len(s);
    char *t = malloc(n + 1);
    memcpy(t, s_ptr(s), n);
    t[n] = 0;
    return t;
}
static void throw_out_of_range(const char *w) {
    die("std::out_of_range: %s", w);
}

H(ZNSsC1EPKcRKSaIcE) {
    s_init(A(0), GS(A(1)), strlen(GS(A(1))));
    return 0;
}
H(ZNSsC1EPKcmRKSaIcE) {
    s_init(A(0), GS(A(1)), A(2));
    return 0;
}
H(ZNSsC1ERKSs) {
    char *t = s_dup(A(1));
    s_init(A(0), t, s_len(A(1)));
    free(t);
    return 0;
}
H(ZNSsC1ERKSsmm) {
    u32 n = s_len(A(1)), pos = A(2), cnt = A(3);
    if (pos > n)
        throw_out_of_range("basic_string::basic_string");
    if (cnt > n - pos)
        cnt = n - pos;
    s_init(A(0), s_ptr(A(1)) + pos, cnt);
    return 0;
}
H(ZNSsC1EmcRKSaIcE) {
    u32 n = A(1);
    char *t = malloc(n + 1);
    memset(t, (int)A(2), n);
    s_init(A(0), t, n);
    free(t);
    return 0;
}
H(ZNSsD1Ev) {
    s_release(A(0));
    return 0;
}
H(ZNSs4_Rep9_S_createEmmRKSaIcE) {
    RET(rep_create(A(0)) - 12);
    return 0;
} // static: (capacity, old_capacity, alloc)
H(ZNSs4_Rep10_M_destroyERKSaIcE) {
    if (A(0) != empty_rep())
        free_guest(A(0));
    return 0;
}
H(ZNSs12_M_leak_hardEv) {
    u32 r = s_rep(A(0));
    if (r != empty_rep())
        wr(r + 8, (u32)-1, 4);
    return 0;
}
H(ZNSs9_M_mutateEmmm) { // (this, pos, len1, len2): replace len1 chars at pos by len2 uninitialised ones
    u32 s = A(0), pos = A(1), len1 = A(2), len2 = A(3), n = s_len(s), nn = n + len2 - len1;
    u32 d = rep_create(nn);
    memcpy(P(d), s_ptr(s), pos);
    memcpy(P(d + pos + len2), s_ptr(s) + pos + len1, n - pos - len1);
    *P(d + nn) = 0;
    wr(d - 12, nn, 4);
    u32 old = s_rep(s);
    wr(s, d, 4);
    if (old != empty_rep())
        free_guest(old);
    return 0;
}
H(ZNSs7reserveEm) {
    u32 s = A(0), want = A(1);
    if (want <= s_cap(s))
        return 0;
    char *t = s_dup(s);
    u32 n = s_len(s), d = rep_create(want);
    memcpy(P(d), t, n + 1);
    wr(d - 12, n, 4);
    free(t);
    u32 old = s_rep(s);
    wr(s, d, 4);
    if (old != empty_rep())
        free_guest(old);
    return 0;
}
static void s_append(u32 s, const char *b, u32 n) {
    char *t = s_dup(s);
    u32 len = s_len(s);
    char *u = malloc(len + n + 1);
    memcpy(u, t, len);
    memcpy(u + len, b, n);
    s_assign(s, u, len + n);
    free(t);
    free(u);
}
H(ZNSs6appendEPKcm) {
    char *t = malloc(A(2) + 1);
    memcpy(t, P(A(1)), A(2));
    s_append(A(0), t, A(2));
    free(t);
    RET(A(0));
    return 0;
}
H(ZNSs6appendERKSs) {
    char *t = s_dup(A(1));
    s_append(A(0), t, s_len(A(1)));
    free(t);
    RET(A(0));
    return 0;
}
H(ZNSs6appendERKSsmm) {
    u32 n = s_len(A(1)), pos = A(2), cnt = A(3);
    if (pos > n)
        throw_out_of_range("basic_string::append");
    if (cnt > n - pos)
        cnt = n - pos;
    char *t = s_dup(A(1));
    s_append(A(0), t + pos, cnt);
    free(t);
    RET(A(0));
    return 0;
}
H(ZNSs6appendEmc) {
    u32 n = A(1);
    char *t = malloc(n + 1);
    memset(t, (int)A(2), n);
    s_append(A(0), t, n);
    free(t);
    RET(A(0));
    return 0;
}
H(ZNSs6assignEPKcm) {
    char *t = malloc(A(2) + 1);
    memcpy(t, P(A(1)), A(2));
    s_assign(A(0), t, A(2));
    free(t);
    RET(A(0));
    return 0;
}
H(ZNSs6assignERKSs) {
    if (A(0) != A(1)) {
        char *t = s_dup(A(1));
        s_assign(A(0), t, s_len(A(1)));
        free(t);
    }
    RET(A(0));
    return 0;
}
H(ZNSs6resizeEmc) {
    u32 s = A(0), n = A(1), len = s_len(s);
    char c0 = (char)A(2);
    char *t = s_dup(s);
    char *u = malloc(n + 1);
    memcpy(u, t, len < n ? len : n);
    if (n > len)
        memset(u + len, c0, n - len);
    s_assign(s, u, n);
    free(t);
    free(u);
    return 0;
}
H(ZNSs7replaceEmmPKcm) { // (this, pos, len1, s, len2)
    u32 s = A(0), pos = A(1), len1 = A(2), n2 = A(4), n = s_len(s);
    if (pos > n)
        throw_out_of_range("basic_string::replace");
    if (len1 > n - pos)
        len1 = n - pos;
    char *t = s_dup(s), *r = malloc(n + n2 + 1);
    memcpy(r, t, pos);
    memcpy(r + pos, P(A(3)), n2);
    memcpy(r + pos + n2, t + pos + len1, n - pos - len1);
    s_assign(s, r, n - len1 + n2);
    free(t);
    free(r);
    RET(A(0));
    return 0;
}
H(ZNKSs7compareEPKc) {
    int r = strcmp(s_ptr(A(0)), GS(A(1)));
    RET(r < 0 ? -1 : r > 0);
    return 0;
}
H(ZNKSs7compareEmmRKSs) {
    u32 s = A(0), pos = A(1), n1 = A(2), o = A(3), n = s_len(s);
    if (pos > n)
        throw_out_of_range("basic_string::compare");
    if (n1 > n - pos)
        n1 = n - pos;
    u32 n2 = s_len(o);
    int r = memcmp(s_ptr(s) + pos, s_ptr(o), n1 < n2 ? n1 : n2);
    if (!r)
        r = n1 < n2 ? -1 : n1 > n2;
    RET(r < 0 ? -1 : r > 0);
    return 0;
}
static u32 s_find(u32 s, const char *needle, u32 pos, u32 n) {
    u32 len = s_len(s);
    if (n == 0)
        return pos <= len ? pos : NPOS;
    for (u32 i = pos; i + n <= len; i++)
        if (!memcmp(s_ptr(s) + i, needle, n))
            return i;
    return NPOS;
}
H(ZNKSs4findEPKcmm) {
    RET(s_find(A(0), GS(A(1)), A(2), A(3)));
    return 0;
}
H(ZNKSs4findEcm) {
    char ch = (char)A(1);
    RET(s_find(A(0), &ch, A(2), 1));
    return 0;
}
H(ZNKSs5rfindEcm) {
    u32 s = A(0), len = s_len(s), i = A(2) < len ? A(2) : (len ? len - 1 : NPOS);
    if (!len) {
        RET(NPOS);
        return 0;
    }
    for (;; i--) {
        if (s_ptr(s)[i] == (char)A(1)) {
            RET(i);
            return 0;
        }
        if (!i)
            break;
    }
    RET(NPOS);
    return 0;
}
H(ZNKSs13find_first_ofEPKcmm) {
    u32 s = A(0), len = s_len(s);
    for (u32 i = A(2); i < len; i++)
        if (memchr(P(A(1)), s_ptr(s)[i], A(3))) {
            RET(i);
            return 0;
        }
    RET(NPOS);
    return 0;
}
H(ZNKSs17find_first_not_ofEPKcmm) {
    u32 s = A(0), len = s_len(s);
    for (u32 i = A(2); i < len; i++)
        if (!memchr(P(A(1)), s_ptr(s)[i], A(3))) {
            RET(i);
            return 0;
        }
    RET(NPOS);
    return 0;
}
H(ZNKSs4copyEPcmm) {
    u32 s = A(0), n = s_len(s), cnt = A(2), pos = A(3);
    if (pos > n)
        throw_out_of_range("basic_string::copy");
    if (cnt > n - pos)
        cnt = n - pos;
    memcpy(P(A(1)), s_ptr(s) + pos, cnt);
    RET(cnt);
    return 0;
}

// ---- iostream / locale: constructors and destructors are no-ops, output operators return the stream ----
H(cxx_noop) {
    return 0;
}
H(cxx_ret_this) {
    RET(A(0));
    return 0;
}
H(cxx_ret_arg1) {
    RET(A(1));
    return 0;
}
H(cxx_ret0) {
    RET(0);
    return 0;
}
H(ZNKSt9basic_iosIcSt11char_traitsIcEE5widenEc) {
    RET(A(1) & 0xff);
    return 0;
}

// ---- exceptions / RTTI: not supported yet ----
H(cxx_die_throw) {
    die("C++ exception thrown by a dylib (%s)", tnames[c->eip - THUNK_BASE]);
}
H(cxa_guard_acquire) {
    if (rd(A(0), 1))
        RET(0);
    else
        RET(1);
    return 0;
}
H(cxa_guard_release) {
    wr(A(0), 1, 1);
    return 0;
}
H(cxa_pure_virtual) {
    die("pure virtual method called");
}
H(Znwm) {
    RET(hmalloc(A(0)));
    return 0;
}
H(ZdlPv) {
    free_guest(A(0));
    return 0;
}

// ---- std::_Rb_tree helpers (port of libstdc++ rbtree.cc) ----
// node: color(0 red, 1 black) at +0, parent +4, left +8, right +12
#define RB_COLOR(n) rd((n), 4)
#define RB_PARENT(n) rd((n) + 4, 4)
#define RB_LEFT(n) rd((n) + 8, 4)
#define RB_RIGHT(n) rd((n) + 12, 4)
#define RB_SETC(n, v) wr((n), (v), 4)
#define RB_SETP(n, v) wr((n) + 4, (v), 4)
#define RB_SETL(n, v) wr((n) + 8, (v), 4)
#define RB_SETR(n, v) wr((n) + 12, (v), 4)
static u32 rb_inc(u32 x) {
    if (RB_RIGHT(x)) {
        x = RB_RIGHT(x);
        while (RB_LEFT(x))
            x = RB_LEFT(x);
    } else {
        u32 y = RB_PARENT(x);
        while (x == RB_RIGHT(y)) {
            x = y;
            y = RB_PARENT(y);
        }
        if (RB_RIGHT(x) != y)
            x = y;
    }
    return x;
}
static u32 rb_dec(u32 x) {
    if (RB_COLOR(x) == 0 && RB_PARENT(RB_PARENT(x)) == x)
        x = RB_RIGHT(x);
    else if (RB_LEFT(x)) {
        u32 y = RB_LEFT(x);
        while (RB_RIGHT(y))
            y = RB_RIGHT(y);
        x = y;
    } else {
        u32 y = RB_PARENT(x);
        while (x == RB_LEFT(y)) {
            x = y;
            y = RB_PARENT(y);
        }
        x = y;
    }
    return x;
}
static void rb_rotl(u32 x, u32 *root) {
    u32 y = RB_RIGHT(x);
    RB_SETR(x, RB_LEFT(y));
    if (RB_LEFT(y))
        RB_SETP(RB_LEFT(y), x);
    RB_SETP(y, RB_PARENT(x));
    if (x == *root)
        *root = y;
    else if (x == RB_LEFT(RB_PARENT(x)))
        RB_SETL(RB_PARENT(x), y);
    else
        RB_SETR(RB_PARENT(x), y);
    RB_SETL(y, x);
    RB_SETP(x, y);
}
static void rb_rotr(u32 x, u32 *root) {
    u32 y = RB_LEFT(x);
    RB_SETL(x, RB_RIGHT(y));
    if (RB_RIGHT(y))
        RB_SETP(RB_RIGHT(y), x);
    RB_SETP(y, RB_PARENT(x));
    if (x == *root)
        *root = y;
    else if (x == RB_RIGHT(RB_PARENT(x)))
        RB_SETR(RB_PARENT(x), y);
    else
        RB_SETL(RB_PARENT(x), y);
    RB_SETR(y, x);
    RB_SETP(x, y);
}
H(ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base) {
    RET(rb_inc(A(0)));
    return 0;
}
H(ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base) {
    RET(rb_dec(A(0)));
    return 0;
}
H(ZSt29_Rb_tree_insert_and_rebalancebPSt18_Rb_tree_node_baseS0_RS_) { // (insert_left, x, p, header)
    u32 left = A(0) & 0xff, x = A(1), p = A(2), h = A(3), root = RB_PARENT(h);
    RB_SETP(x, p);
    RB_SETL(x, 0);
    RB_SETR(x, 0);
    RB_SETC(x, 0);
    if (left) {
        RB_SETL(p, x);
        if (p == h) {
            RB_SETP(h, x);
            RB_SETR(h, x);
        } else if (p == RB_LEFT(h))
            RB_SETL(h, x);
    } else {
        RB_SETR(p, x);
        if (p == RB_RIGHT(h))
            RB_SETR(h, x);
    }
    root = RB_PARENT(h); // `root` aliases header->parent in libstdc++
    while (x != root && RB_COLOR(RB_PARENT(x)) == 0) {
        u32 xpp = RB_PARENT(RB_PARENT(x));
        if (RB_PARENT(x) == RB_LEFT(xpp)) {
            u32 y = RB_RIGHT(xpp);
            if (y && RB_COLOR(y) == 0) {
                RB_SETC(RB_PARENT(x), 1);
                RB_SETC(y, 1);
                RB_SETC(xpp, 0);
                x = xpp;
            } else {
                if (x == RB_RIGHT(RB_PARENT(x))) {
                    x = RB_PARENT(x);
                    rb_rotl(x, &root);
                }
                RB_SETC(RB_PARENT(x), 1);
                RB_SETC(xpp, 0);
                rb_rotr(xpp, &root);
            }
        } else {
            u32 y = RB_LEFT(xpp);
            if (y && RB_COLOR(y) == 0) {
                RB_SETC(RB_PARENT(x), 1);
                RB_SETC(y, 1);
                RB_SETC(xpp, 0);
                x = xpp;
            } else {
                if (x == RB_LEFT(RB_PARENT(x))) {
                    x = RB_PARENT(x);
                    rb_rotr(x, &root);
                }
                RB_SETC(RB_PARENT(x), 1);
                RB_SETC(xpp, 0);
                rb_rotl(xpp, &root);
            }
        }
    }
    RB_SETC(root, 1);
    RB_SETP(h, root);
    return 0;
}
H(ZSt28_Rb_tree_rebalance_for_erasePSt18_Rb_tree_node_baseRS_) { // (z, header) -> node to deallocate
    u32 z = A(0), h = A(1), root = RB_PARENT(h), leftmost = RB_LEFT(h), rightmost = RB_RIGHT(h), y = z, x = 0,
        xp = 0;
    if (!RB_LEFT(y))
        x = RB_RIGHT(y);
    else if (!RB_RIGHT(y))
        x = RB_LEFT(y);
    else {
        y = RB_RIGHT(y);
        while (RB_LEFT(y))
            y = RB_LEFT(y);
        x = RB_RIGHT(y);
    }
    if (y != z) {
        RB_SETP(RB_LEFT(z), y);
        RB_SETL(y, RB_LEFT(z));
        if (y != RB_RIGHT(z)) {
            xp = RB_PARENT(y);
            if (x)
                RB_SETP(x, xp);
            RB_SETL(RB_PARENT(y), x);
            RB_SETR(y, RB_RIGHT(z));
            RB_SETP(RB_RIGHT(z), y);
        } else
            xp = y;
        if (root == z)
            root = y;
        else if (RB_LEFT(RB_PARENT(z)) == z)
            RB_SETL(RB_PARENT(z), y);
        else
            RB_SETR(RB_PARENT(z), y);
        RB_SETP(y, RB_PARENT(z));
        u32 t = RB_COLOR(y);
        RB_SETC(y, RB_COLOR(z));
        RB_SETC(z, t);
        y = z;
    } else {
        xp = RB_PARENT(y);
        if (x)
            RB_SETP(x, xp);
        if (root == z)
            root = x;
        else if (RB_LEFT(RB_PARENT(z)) == z)
            RB_SETL(RB_PARENT(z), x);
        else
            RB_SETR(RB_PARENT(z), x);
        if (leftmost == z) {
            if (!RB_RIGHT(z))
                leftmost = RB_PARENT(z);
            else {
                u32 m = x;
                while (RB_LEFT(m))
                    m = RB_LEFT(m);
                leftmost = m;
            }
        }
        if (rightmost == z) {
            if (!RB_LEFT(z))
                rightmost = RB_PARENT(z);
            else {
                u32 m = x;
                while (RB_RIGHT(m))
                    m = RB_RIGHT(m);
                rightmost = m;
            }
        }
    }
    if (RB_COLOR(y) != 0) {
        while (x != root && (x == 0 || RB_COLOR(x) == 1)) {
            if (x == RB_LEFT(xp)) {
                u32 w = RB_RIGHT(xp);
                if (RB_COLOR(w) == 0) {
                    RB_SETC(w, 1);
                    RB_SETC(xp, 0);
                    rb_rotl(xp, &root);
                    w = RB_RIGHT(xp);
                }
                if ((!RB_LEFT(w) || RB_COLOR(RB_LEFT(w)) == 1) &&
                    (!RB_RIGHT(w) || RB_COLOR(RB_RIGHT(w)) == 1)) {
                    RB_SETC(w, 0);
                    x = xp;
                    xp = RB_PARENT(xp);
                } else {
                    if (!RB_RIGHT(w) || RB_COLOR(RB_RIGHT(w)) == 1) {
                        RB_SETC(RB_LEFT(w), 1);
                        RB_SETC(w, 0);
                        rb_rotr(w, &root);
                        w = RB_RIGHT(xp);
                    }
                    RB_SETC(w, RB_COLOR(xp));
                    RB_SETC(xp, 1);
                    if (RB_RIGHT(w))
                        RB_SETC(RB_RIGHT(w), 1);
                    rb_rotl(xp, &root);
                    break;
                }
            } else {
                u32 w = RB_LEFT(xp);
                if (RB_COLOR(w) == 0) {
                    RB_SETC(w, 1);
                    RB_SETC(xp, 0);
                    rb_rotr(xp, &root);
                    w = RB_LEFT(xp);
                }
                if ((!RB_RIGHT(w) || RB_COLOR(RB_RIGHT(w)) == 1) &&
                    (!RB_LEFT(w) || RB_COLOR(RB_LEFT(w)) == 1)) {
                    RB_SETC(w, 0);
                    x = xp;
                    xp = RB_PARENT(xp);
                } else {
                    if (!RB_LEFT(w) || RB_COLOR(RB_LEFT(w)) == 1) {
                        RB_SETC(RB_RIGHT(w), 1);
                        RB_SETC(w, 0);
                        rb_rotl(w, &root);
                        w = RB_LEFT(xp);
                    }
                    RB_SETC(w, RB_COLOR(xp));
                    RB_SETC(xp, 1);
                    if (RB_LEFT(w))
                        RB_SETC(RB_LEFT(w), 1);
                    rb_rotr(xp, &root);
                    break;
                }
            }
        }
        if (x)
            RB_SETC(x, 1);
    }
    RB_SETP(h, root);
    RB_SETL(h, leftmost);
    RB_SETR(h, rightmost);
    RET(y);
    return 0;
}

// name table: (symbol, handler)

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry cxx_imports[] = {
    {"__ZNSsC1EPKcRKSaIcE", h_ZNSsC1EPKcRKSaIcE}, {"__ZNSsC1EPKcmRKSaIcE", h_ZNSsC1EPKcmRKSaIcE},
    {"__ZNSsC1ERKSs", h_ZNSsC1ERKSs}, {"__ZNSsC1ERKSsmm", h_ZNSsC1ERKSsmm},
    {"__ZNSsC1EmcRKSaIcE", h_ZNSsC1EmcRKSaIcE}, {"__ZNSsD1Ev", h_ZNSsD1Ev},
    {"__ZNSs4_Rep9_S_createEmmRKSaIcE", h_ZNSs4_Rep9_S_createEmmRKSaIcE},
    {"__ZNSs4_Rep10_M_destroyERKSaIcE", h_ZNSs4_Rep10_M_destroyERKSaIcE},
    {"__ZNSs12_M_leak_hardEv", h_ZNSs12_M_leak_hardEv}, {"__ZNSs9_M_mutateEmmm", h_ZNSs9_M_mutateEmmm},
    {"__ZNSs7reserveEm", h_ZNSs7reserveEm}, {"__ZNSs6appendEPKcm", h_ZNSs6appendEPKcm},
    {"__ZNSs6appendERKSs", h_ZNSs6appendERKSs}, {"__ZNSs6appendERKSsmm", h_ZNSs6appendERKSsmm},
    {"__ZNSs6appendEmc", h_ZNSs6appendEmc}, {"__ZNSs6assignEPKcm", h_ZNSs6assignEPKcm},
    {"__ZNSs6assignERKSs", h_ZNSs6assignERKSs}, {"__ZNSs6resizeEmc", h_ZNSs6resizeEmc},
    {"__ZNSs7replaceEmmPKcm", h_ZNSs7replaceEmmPKcm}, {"__ZNKSs7compareEPKc", h_ZNKSs7compareEPKc},
    {"__ZNKSs7compareEmmRKSs", h_ZNKSs7compareEmmRKSs}, {"__ZNKSs4findEPKcmm", h_ZNKSs4findEPKcmm},
    {"__ZNKSs4findEcm", h_ZNKSs4findEcm}, {"__ZNKSs5rfindEcm", h_ZNKSs5rfindEcm},
    {"__ZNKSs13find_first_ofEPKcmm", h_ZNKSs13find_first_ofEPKcmm},
    {"__ZNKSs17find_first_not_ofEPKcmm", h_ZNKSs17find_first_not_ofEPKcmm},
    {"__ZNKSs4copyEPcmm", h_ZNKSs4copyEPcmm}, {"__ZNSt8ios_base4InitC1Ev", h_cxx_noop},
    {"__ZNSt8ios_base4InitD1Ev", h_cxx_noop}, {"__ZNSt6localeC1Ev", h_cxx_noop},
    {"__ZNSt6localeD1Ev", h_cxx_noop}, {"__ZNSt8ios_baseC2Ev", h_cxx_noop},
    {"__ZNSt8ios_baseD2Ev", h_cxx_noop}, {"__ZNSdD2Ev", h_cxx_noop},
    {"__ZNSt9basic_iosIcSt11char_traitsIcEE4initEPSt15basic_streambufIcS1_E", h_cxx_noop},
    {"__ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate", h_cxx_noop},
    {"__ZNKSt9basic_iosIcSt11char_traitsIcEE5widenEc", h_ZNKSt9basic_iosIcSt11char_traitsIcEE5widenEc},
    {"__ZNSolsEi", h_cxx_ret_this}, {"__ZNSo5writeEPKci", h_cxx_ret_this},
    {"__ZNSi4readEPci", h_cxx_ret_this},
    {"__ZNSt13basic_filebufIcSt11char_traitsIcEE4openEPKcSt13_Ios_Openmode", h_cxx_ret0},
    {"__ZSt17__throw_bad_allocv", h_cxx_die_throw}, {"__ZSt19__throw_logic_errorPKc", h_cxx_die_throw},
    {"__ZSt20__throw_length_errorPKc", h_cxx_die_throw},
    {"__ZSt20__throw_out_of_rangePKc", h_cxx_die_throw}, {"___cxa_begin_catch", h_cxx_die_throw},
    {"___cxa_end_catch", h_cxx_die_throw}, {"___cxa_rethrow", h_cxx_die_throw},
    {"___dynamic_cast", h_cxx_die_throw}, {"___cxa_guard_acquire", h_cxa_guard_acquire},
    {"___cxa_guard_release", h_cxa_guard_release}, {"___cxa_pure_virtual", h_cxa_pure_virtual},
    {"__Znwm", h_Znwm}, {"__Znam", h_Znwm}, {"__ZdlPv", h_ZdlPv}, {"__ZdaPv", h_ZdlPv},
    {"__ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base", h_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base},
    {"__ZSt18_Rb_tree_incrementPKSt18_Rb_tree_node_base", h_ZSt18_Rb_tree_incrementPSt18_Rb_tree_node_base},
    {"__ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base", h_ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base},
    {"__ZSt18_Rb_tree_decrementPKSt18_Rb_tree_node_base", h_ZSt18_Rb_tree_decrementPSt18_Rb_tree_node_base},
    {0, 0}};
// clang-format on
