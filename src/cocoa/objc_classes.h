// objc_classes.h: the object model of the Objective-C shim.
//
// Classes are entries in a host table, each with an address in the guest range CLASS_BASE.. so the guest sees them as ordinary
// isa pointers. An object is guest memory: word 0 = isa, payload after it. The game's own classes keep their guest methods
// (Cls.gm); host classes get host handlers (see objc_dispatch.h). Also holds the handlers that are shared by many classes.

#include <sys/stat.h>
#include <libgen.h>

static u32 load_plist(const char *path); // plist.h

#define OA(n) A(oa_base + (n)) // n-th argument after (self, _cmd); oa_base is 3 for _stret
static int oa_base = 2;

typedef struct {
    char *name;
    int super;
    u32 addr;
    struct {
        char *sel;
        u32 imp;
    } gm[32];
    int ngm;
} Cls;
static Cls classes[128];
static int nclasses;

static int cls_find(const char *name) {
    for (int i = 0; i < nclasses; i++)
        if (!strcmp(classes[i].name, name))
            return i;
    return -1;
}
static const char *super_name(const char *n) {
    static const char *const T[][2] = {{"NSResponder", "NSObject"},
                                       {"NSView", "NSResponder"},
                                       {"NSOpenGLView", "NSView"},
                                       {"NSWindow", "NSResponder"},
                                       {"NSApplication", "NSResponder"},
                                       {"NSArray", "NSObject"},
                                       {"NSMutableArray", "NSArray"},
                                       {"NSString", "NSObject"},
                                       {"NSCFString", "NSString"},
                                       {"NSCFConstantString", "NSCFString"},
                                       {"NSDictionary", "NSObject"},
                                       {"NSMutableDictionary", "NSDictionary"},
                                       {"NSNumber", "NSObject"},
                                       {"NSRecursiveLock", "NSObject"},
                                       {0, 0}};
    for (int i = 0; T[i][0]; i++)
        if (!strcmp(n, T[i][0]))
            return T[i][1];
    return strcmp(n, "NSObject") ? "NSObject" : NULL;
}
static int cls_add(const char *name, u32 addr) {
    int i = cls_find(name);
    if (i >= 0)
        return i;
    const char *sn = super_name(name);
    int sup = sn ? cls_find(sn) : -1;
    if (sn && sup < 0)
        sup = cls_add(sn, 0);
    i = nclasses++;
    if (i >= 128)
        die("class table full");
    classes[i].name = strdup(name);
    classes[i].super = sup;
    classes[i].addr = addr ? addr : CLASS_BASE + 0x40 * i;
    wr(classes[i].addr, classes[i].addr, 4);
    wr(classes[i].addr + 4, gstr(name), 4);
    return i;
}
static int cls_of_addr(u32 a) {
    for (int i = 0; i < nclasses; i++)
        if (classes[i].addr == a)
            return i;
    return -1;
}
static int is_kind(int cls, const char *name) {
    for (; cls >= 0; cls = classes[cls].super)
        if (!strcmp(classes[cls].name, name))
            return 1;
    return 0;
}
static int class_of_obj(u32 o) {
    return cls_of_addr(rd(o, 4));
}
static u32 obj_new(int cls, u32 size) {
    u32 o = hmalloc(size < 64 ? 64 : size);
    memset(P(o), 0, size < 64 ? 64 : size);
    wr(o, classes[cls].addr, 4);
    return o;
}
static u32 obj_of(const char *cls) {
    return obj_new(cls_add(cls, 0), 64);
}

// ---- generic handlers ----
typedef int (*OFn)(CPU *, u32 self);
static int oh_self(CPU *c, u32 self) {
    RET(self);
    return 0;
}
static int oh_nil(CPU *c, u32 self) {
    RET(0);
    return 0;
}
static int oh_yes(CPU *c, u32 self) {
    RET(1);
    return 0;
}
static int oh_fp0(CPU *c, u32 self) {
    fpush(c, 0.0);
    return 0;
}
static int oh_alloc(CPU *c, u32 self) {
    int k = cls_of_addr(self);
    RET(obj_new(k, 64));
    return 0;
}
static u32 singleton(const char *cls); // foundation.h
static int oh_ctx_obj(CPU *c, u32 self) {
    RET(singleton("NSOpenGLContext"));
    return 0;
}
static int oh_zero_rect(CPU *c, u32 self) {
    u32 s = A(0);
    memset(P(s), 0, 16);
    RET(s);
    return 0;
}
static int oh_class(CPU *c, u32 self) {
    RET(classes[class_of_obj(self)].addr);
    return 0;
}
