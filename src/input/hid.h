// hid.h: a fake IOHID layer.
//
// The game reads its keyboard and mouse through IOHIDManager / IOHIDQueue, indexed by HID usage. This module exposes one keyboard
// (usage page 7, usages 4..231) and one mouse (buttons, X/Y/wheel); host key and mouse events are pushed into every started queue.
// The game's own key bindings (DefaultBindings.map) refer to these usages.

enum { HID_KBD = 1, HID_MOUSE = 2 };
static struct {
    u32 h;
    int page, usage;
} hid_mgr[8];
static int hid_nmgr;
static u32 hid_dev[3]; // [HID_KBD], [HID_MOUSE]: guest handles
static u32 hid_sets[8];
static int hid_nsets;
static struct {
    u32 h;
    int dev;
} hid_elem_dev[512];
static int hid_nelem_dev;
static struct {
    u32 h, dev;
    int head, tail;
    struct {
        u32 elem;
        int v;
    } q[256];
    int on;
} hid_q[8];
static int hid_nq;
static u32 hid_elem[3][256 + 8], hid_elem_n[3]; // element handles per device: page/usage at +0/+4
static void hid_init(void) {
    if (hid_dev[HID_KBD])
        return;
    hid_dev[HID_KBD] = hmalloc(16);
    hid_dev[HID_MOUSE] = hmalloc(16);
    for (int d = HID_KBD; d <= HID_MOUSE; d++) {
        int n = 0;
        if (d == HID_KBD)
            for (int u = 4; u <= 231; u++) {
                u32 e = hmalloc(16);
                wr(e, 7, 4);
                wr(e + 4, u, 4);
                wr(e + 8, d, 4);
                hid_elem[d][n++] = e;
            }
        else {
            static const int pu[][2] = {{9, 1}, {9, 2}, {9, 3}, {1, 0x30}, {1, 0x31}, {1, 0x38}};
            for (int i = 0; i < 6; i++) {
                u32 e = hmalloc(16);
                wr(e, pu[i][0], 4);
                wr(e + 4, pu[i][1], 4);
                wr(e + 8, d, 4);
                hid_elem[d][n++] = e;
            }
        }
        hid_elem_n[d] = n;
    }
}
static int hid_dict_int(u32 d, const char *name) { // CFNumber stored under the CFString key `name`, or -1
    for (u32 i = 0; d && i < rd(d + 4, 4); i++) {
        u32 k = rd(rd(d + 12, 4) + 4 * i, 4), v = rd(rd(d + 16, 4) + 4 * i, 4);
        if (is_string(k) && !strcmp(cstr_of(k), name))
            return (i32)rd(v + 4, 4);
    }
    return -1;
}
H(IOHIDManagerCreate) {
    hid_init();
    if (hid_nmgr < 8) {
        hid_mgr[hid_nmgr].h = hmalloc(16);
        hid_mgr[hid_nmgr].page = hid_mgr[hid_nmgr].usage = 0;
        RET(hid_mgr[hid_nmgr++].h);
    } else
        RET(0);
    return 0;
}
H(IOHIDManagerSetDeviceMatching) {
    for (int i = 0; i < hid_nmgr; i++)
        if (hid_mgr[i].h == A(0)) {
            hid_mgr[i].page = hid_dict_int(A(1), "DeviceUsagePage");
            hid_mgr[i].usage = hid_dict_int(A(1), "DeviceUsage");
            if (hid_mgr[i].page < 0)
                hid_mgr[i].page = hid_dict_int(A(1), "PrimaryUsagePage");
            if (hid_mgr[i].usage < 0)
                hid_mgr[i].usage = hid_dict_int(A(1), "PrimaryUsage");
            if (DBG_FLAG("HIDLOG"))
                fprintf(stderr, "[hid] manager %08x matching page=%d usage=%d (dict keys:", A(0),
                        hid_mgr[i].page, hid_mgr[i].usage);
            if (DBG_FLAG("HIDLOG")) {
                for (u32 k = 0; k < rd(A(1) + 4, 4); k++)
                    fprintf(stderr, " %s", cstr_of(rd(rd(A(1) + 12, 4) + 4 * k, 4)));
                fprintf(stderr, ")\n");
            }
        }
    RET(0);
    return 0;
}
H(IOHIDManagerCopyDevices) {
    int pg = 0, us = 0;
    for (int i = 0; i < hid_nmgr; i++)
        if (hid_mgr[i].h == A(0)) {
            pg = hid_mgr[i].page;
            us = hid_mgr[i].usage;
        }
    int d = (pg == 1 && us == 6) ? HID_KBD : (pg == 1 && us == 2) ? HID_MOUSE : 0;
    if (!d || hid_nsets >= 8) {
        RET(0);
        return 0;
    }
    u32 s = hmalloc(16);
    wr(s, 1, 4);
    wr(s + 4, hid_dev[d], 4);
    hid_sets[hid_nsets++] = s;
    RET(s);
    return 0;
}
static int hid_is_dev(u32 o) {
    return o && (o == hid_dev[HID_KBD] || o == hid_dev[HID_MOUSE]);
}
static int hid_is_set(u32 o) {
    for (int i = 0; i < hid_nsets; i++)
        if (hid_sets[i] == o)
            return 1;
    return 0;
}
H(IOHIDDeviceGetTypeID) {
    RET(0x77);
    return 0;
}
H(IOHIDDeviceOpen) {
    RET(0);
    return 0;
}
H(IOHIDDeviceClose) {
    RET(0);
    return 0;
}
H(IOHIDDeviceCopyMatchingElements) {
    int d = A(0) == hid_dev[HID_KBD] ? HID_KBD : HID_MOUSE;
    u32 a = arr_new();
    for (int i = 0; i < (int)hid_elem_n[d]; i++)
        arr_add(a, hid_elem[d][i]);
    RET(a);
    return 0;
}
H(IOHIDElementGetUsage) {
    RET(rd(A(0) + 4, 4));
    return 0;
}
H(IOHIDElementGetUsagePage) {
    RET(rd(A(0), 4));
    return 0;
}
H(IOHIDQueueCreate) {
    if (hid_nq >= 8) {
        RET(0);
        return 0;
    }
    hid_q[hid_nq].h = hmalloc(16);
    hid_q[hid_nq].dev = A(1) == hid_dev[HID_MOUSE] ? HID_MOUSE : HID_KBD;
    hid_q[hid_nq].head = hid_q[hid_nq].tail = 0;
    RET(hid_q[hid_nq++].h);
    return 0;
}
H(IOHIDQueueAddElement) {
    RET(0);
    return 0;
}
H(IOHIDQueueStart) {
    for (int i = 0; i < hid_nq; i++)
        if (hid_q[i].h == A(0))
            hid_q[i].on = 1;
    RET(0);
    return 0;
}
H(IOHIDQueueStop) {
    for (int i = 0; i < hid_nq; i++)
        if (hid_q[i].h == A(0))
            hid_q[i].on = 0;
    RET(0);
    return 0;
}
H(IOHIDQueueCopyNextValueWithTimeout) {
    for (int i = 0; i < hid_nq; i++)
        if (hid_q[i].h == A(0) && hid_q[i].head != hid_q[i].tail) {
            static u32 vals[64];
            static int vi;
            if (!vals[0])
                for (int k = 0; k < 64; k++)
                    vals[k] = hmalloc(16);
            u32 v = vals[vi++ & 63];
            int s = hid_q[i].head++ & 255;
            if (DBG_FLAG("HIDLOG"))
                fprintf(stderr, "[hid] dev%d queue %08x delivers page=%d usage=%d value=%d\n", hid_q[i].dev,
                        A(0), rd(hid_q[i].q[s].elem, 4), rd(hid_q[i].q[s].elem + 4, 4), hid_q[i].q[s].v);
            wr(v, hid_q[i].q[s].elem, 4);
            wr(v + 4, hid_q[i].q[s].v, 4);
            RET(v);
            return 0;
        }
    RET(0);
    return 0;
}
H(IOHIDValueGetElement) {
    RET(rd(A(0), 4));
    return 0;
}
H(IOHIDValueGetIntegerValue) {
    RET(rd(A(0) + 4, 4));
    c->r[EDX] = (i32)rd(A(0) + 4, 4) < 0 ? 0xffffffffu : 0;
    return 0;
}
static void hid_push(int dev, int page, int usage,
                     int v) { // host event -> every started queue of that device
    hid_init();
    u32 e = 0;
    for (int i = 0; i < (int)hid_elem_n[dev]; i++)
        if ((int)rd(hid_elem[dev][i], 4) == page && (int)rd(hid_elem[dev][i] + 4, 4) == usage)
            e = hid_elem[dev][i];
    if (!e)
        return;
    for (int i = 0; i < hid_nq; i++)
        if (hid_q[i].on && hid_q[i].dev == dev && ((hid_q[i].tail - hid_q[i].head) & 0xffff) < 255) {
            int s = hid_q[i].tail++ & 255;
            hid_q[i].q[s].elem = e;
            hid_q[i].q[s].v = v;
        }
}
static const int mac2hid[128] = {
    // macOS virtual keycode -> HID keyboard usage
    4,  22, 7,  9,   11, 10, 29, 27, 6,  25, 100, 5,  20, 26, 8,  21, 28, 23,  30,  31,  32, 33,
    35, 34, 46, 38,  36, 45, 37, 39, 48, 18, 24,  47, 12, 19, 40, 15, 13, 51,  14,  52,  49, 54,
    56, 17, 16, 55,  43, 44, 53, 42, 0,  41, 0,   0,  0,  0,  0,  0,  0,  0,   0,   0,   0,  99,
    0,  85, 0,  87,  0,  83, 0,  0,  0,  84, 88,  0,  86, 45, 0,  0,  0,  98,  89,  90,  91, 92,
    93, 94, 95, 0,   96, 97, 0,  0,  62, 63, 64,  60, 65, 66, 0,  68, 0,  104, 107, 105, 0,  67,
    0,  69, 0,  106, 73, 74, 75, 76, 61, 77, 59,  78, 58, 80, 79, 81, 82, 0};
static void hid_key(int keycode, int down) {
    if (keycode >= 0 && keycode < 128 && mac2hid[keycode])
        hid_push(HID_KBD, 7, mac2hid[keycode], down ? 1 : 0);
}
// NSEvent modifier flags: shift/ctrl/alt/cmd -> HID usages 225/224/226/227
static void hid_modifiers(int flags) {
    static int last;
    static const struct {
        int mask, usage;
    } m[] = {{0x20000, 225}, {0x40000, 224}, {0x80000, 226}, {0x100000, 227}};
    for (int i = 0; i < 4; i++)
        if ((flags & m[i].mask) != (last & m[i].mask))
            hid_push(HID_KBD, 7, m[i].usage, (flags & m[i].mask) != 0);
    last = flags;
}
static void hid_mouse(int type, int button, float dx,
                      float dy) { // HGE types: 3/4 button down/up, 5 moved (deltas), 6 scroll
    if (type == 3 || type == 4)
        hid_push(HID_MOUSE, 9, button + 1, type == 3);
    else if (type == 5) {
        if ((int)dx)
            hid_push(HID_MOUSE, 1, 0x30, (int)dx);
        if ((int)dy)
            hid_push(HID_MOUSE, 1, 0x31, (int)dy);
    } else if (type == 6 && (int)dy)
        hid_push(HID_MOUSE, 1, 0x38, (int)dy);
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry hid_imports[] = {
    {"_IOHIDManagerCreate", h_IOHIDManagerCreate},
    {"_IOHIDManagerSetDeviceMatching", h_IOHIDManagerSetDeviceMatching},
    {"_IOHIDManagerCopyDevices", h_IOHIDManagerCopyDevices},
    {"_IOHIDDeviceGetTypeID", h_IOHIDDeviceGetTypeID}, {"_IOHIDDeviceOpen", h_IOHIDDeviceOpen},
    {"_IOHIDDeviceClose", h_IOHIDDeviceClose},
    {"_IOHIDDeviceCopyMatchingElements", h_IOHIDDeviceCopyMatchingElements},
    {"_IOHIDElementGetUsage", h_IOHIDElementGetUsage},
    {"_IOHIDElementGetUsagePage", h_IOHIDElementGetUsagePage}, {"_IOHIDQueueCreate", h_IOHIDQueueCreate},
    {"_IOHIDQueueAddElement", h_IOHIDQueueAddElement}, {"_IOHIDQueueStart", h_IOHIDQueueStart},
    {"_IOHIDQueueStop", h_IOHIDQueueStop},
    {"_IOHIDQueueCopyNextValueWithTimeout", h_IOHIDQueueCopyNextValueWithTimeout},
    {"_IOHIDValueGetElement", h_IOHIDValueGetElement},
    {"_IOHIDValueGetIntegerValue", h_IOHIDValueGetIntegerValue},
    {0, 0}};
// clang-format on
