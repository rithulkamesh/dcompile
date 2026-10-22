// objc_dispatch.h: method tables and message dispatch (objc_msgSend and friends).
//
// OTAB maps (class, selector) to host handlers. msg_send() walks the class chain: a guest method (the game's own class) wins,
// then a host handler, then the superclass. Unknown messages are logged once and answered with nil/0.

typedef struct {
    const char *cls;
    char kind;
    const char *sel;
    OFn fn;
} OEnt; // kind: 'C' class method, 'I' instance method
static const OEnt OTAB[] = {
    {"NSView", 'I', "enterFullScreenMode:withOptions:", oh_view_fullscreen},
    {"NSWindow", 'I', "setContentSize:", oh_win_setsize},
    {"NSWindow", 'I', "frame", oh_win_frame},
    {"NSView", 'I', "frame", oh_view_frame},
    {"NSOpenGLPixelFormat", 'I', "initWithAttributes:", oh_pf_init},
    {"NSOpenGLView", 'I', "initWithFrame:pixelFormat:", oh_glview_init},
    {"NSOpenGLView", 'I', "openGLContext", oh_glview_ctx},
    {"NSOpenGLContext", 'I', "makeCurrentContext", oh_ctx_make_current},
    {"NSOpenGLContext", 'C', "currentContext", oh_ctx_current},
    {"NSOpenGLContext", 'I', "flushBuffer", oh_ctx_flush},
    {"NSOpenGLContext", 'I', "update", oh_ctx_update},
    {"NSOpenGLContext", 'I', "CGLContextObj", oh_ctx_cgl},
    {"NSOpenGLContext", 'I', "setValues:forParameter:", oh_ctx_setvalues},
    {"NSWindow", 'I', "initWithContentRect:styleMask:backing:defer:", oh_win_init},
    {"NSWindow", 'I', "makeKeyAndOrderFront:", oh_win_show},
    {"NSWindow", 'I', "center", oh_win_center},
    {"NSWindow", 'I', "setContentView:", oh_win_setcontent2},
    {"NSWindow", 'I', "contentView", oh_win_content2},
    {"NSResponder", 'I', "setNextResponder:", oh_win_setnext},
    {"NSResponder", 'I', "nextResponder", oh_win_next},
    {"NSScreen", 'I', "frame", oh_screen_frame2},
    {"NSScreen", 'I', "deviceDescription", oh_screen_desc},
    {"NSApplication", 'I', "nextEventMatchingMask:untilDate:inMode:dequeue:", oh_app_nextevent},
    {"NSApplication", 'I', "sendEvent:", oh_app_send},
    {"NSEvent", 'I', "type", oh_ev_type},
    {"NSEvent", 'I', "characters", oh_ev_chars},
    {"NSEvent", 'I', "buttonNumber", oh_ev_button},
    {"NSEvent", 'I', "deltaX", oh_ev_dx},
    {"NSEvent", 'I', "deltaY", oh_ev_dy},
    {"NSEvent", 'I', "deltaZ", oh_ev_dz},
    {"NSEvent", 'C', "mouseLocation", oh_ev_mouseloc},
    {"NSRunLoop", 'I', "addTimer:forMode:", oh_rl_addtimer},
    {"NSObject", 'C', "alloc", oh_alloc},
    {"NSObject", 'C', "new", oh_alloc},
    {"NSObject", 'I', "init", oh_self},
    {"NSObject", 'I', "retain", oh_self},
    {"NSObject", 'I', "release", oh_nil},
    {"NSObject", 'I', "autorelease", oh_self},
    {"NSObject", 'I', "dealloc", oh_nil},
    {"NSObject", 'I', "class", oh_class},
    {"NSAutoreleasePool", 'I', "drain", oh_nil},
    {"NSString", 'C', "stringWithCString:", oh_str_with_c},
    {"NSString", 'I', "UTF8String", oh_str_utf8},
    {"NSString", 'I', "cStringUsingEncoding:", oh_str_utf8},
    {"NSString", 'I', "getCString:maxLength:encoding:", oh_str_getc},
    {"NSString", 'I', "isEqualToString:", oh_str_eq},
    {"NSString", 'I', "hasPrefix:", oh_str_prefix},
    {"NSString", 'I', "intValue", oh_str_int},
    {"NSString", 'I', "stringByDeletingLastPathComponent", oh_str_dirname},
    {"NSString", 'I', "mutableCopy", oh_str_copy},
    {"NSString", 'I', "getCharacters:range:", oh_str_getchars},
    {"NSString", 'I', "writeToFile:atomically:", oh_yes},
    {"NSBundle", 'C', "mainBundle", oh_bundle_main},
    {"NSBundle", 'C', "bundleWithPath:", oh_bundle_with},
    {"NSBundle", 'C', "loadNibNamed:owner:", oh_yes},
    {"NSBundle", 'I', "bundlePath", oh_bundle_path},
    {"NSBundle", 'I', "resourcePath", oh_bundle_res},
    {"NSBundle", 'I', "executablePath", oh_bundle_exe},
    {"NSFileManager", 'C', "defaultManager", NULL /* singleton, see below */},
    {"NSFileManager", 'I', "fileExistsAtPath:", oh_fm_exists},
    {"NSFileManager", 'I', "changeCurrentDirectoryPath:", oh_fm_chdir},
    {"NSProcessInfo", 'C', "processInfo", NULL},
    {"NSProcessInfo", 'I', "arguments", oh_pi_args},
    {"NSTask", 'I', "setLaunchPath:", oh_nil},
    {"NSTask", 'I', "setArguments:", oh_nil},
    {"NSTask", 'I', "launch", oh_nil},
    {"NSTask", 'I', "setCurrentDirectoryPath:", oh_nil},
    {"NSTask", 'I', "arguments", oh_arr_new},
    {"NSMutableArray", 'C', "array", oh_arr_new},
    {"NSArray", 'I', "addObject:", oh_arr_add},
    {"NSArray", 'I', "count", oh_arr_count},
    {"NSArray", 'I', "objectAtIndex:", oh_arr_at},
    {"NSArray", 'I', "removeAllObjects", oh_arr_clear},
    {"NSArray", 'I', "removeObjectsInRange:", oh_arr_rmrange},
    {"NSDictionary", 'C', "dictionaryWithObject:forKey:", oh_dict_obj_key},
    {"NSDictionary", 'C', "dictionaryWithContentsOfFile:", oh_dict_file},
    {"NSDictionary", 'I', "objectForKey:", oh_dict_get},
    {"NSDictionary", 'I', "valueForKey:", oh_dict_get},
    {"NSDictionary", 'I', "setObject:forKey:", oh_dict_set},
    {"NSNumber", 'C', "numberWithBool:", oh_num_bool},
    {"NSNumber", 'I', "intValue", oh_num_int},
    {"NSNumber", 'I', "unsignedIntValue", oh_num_int},
    {"NSDate", 'C', "date", oh_date_now},
    {"NSDate", 'C', "dateWithTimeIntervalSinceNow:", oh_date_since_now},
    {"NSDate", 'I', "timeIntervalSinceDate:", oh_date_diff},
    {"NSRunLoop", 'C', "mainRunLoop", NULL},
    {"NSRunLoop", 'I', "addTimer:forMode:", oh_nil},
    {"NSRunLoop", 'I', "limitDateForMode:", oh_runloop_limit},
    {"NSTimer", 'C', "timerWithTimeInterval:target:selector:userInfo:repeats:", oh_timer_make},
    {"NSThread", 'C', "sleepForTimeInterval:", oh_sleep},
    {"NSRecursiveLock", 'I', "lock", oh_nil},
    {"NSRecursiveLock", 'I', "unlock", oh_nil},
    {"NSApplication", 'C', "sharedApplication", NULL},
    {"NSApplication", 'I', "setDelegate:", oh_app_delegate},
    {"NSApplication", 'I', "finishLaunching", oh_nil},
    {"NSApplication", 'I', "isActive", oh_yes},
    {"NSApplication", 'I', "mainMenu", oh_nil},
    {"NSApplication", 'I', "mainWindow", oh_nil},
    {"NSApplication", 'I', "sendEvent:", oh_nil},
    {"NSApplication", 'I', "updateWindows", oh_nil},
    {"NSApplication", 'I', "nextEventMatchingMask:untilDate:inMode:dequeue:", oh_nil}, // no events yet
    {"NSScreen", 'C', "mainScreen", oh_screen_main},
    {"NSScreen", 'C', "screens", oh_screens},
    {"NSScreen", 'I', "frame", oh_screen_frame},
    {"NSScreen", 'I', "deviceDescription", oh_empty_dict},
    {"NSWindow", 'I', "initWithContentRect:styleMask:backing:defer:", oh_self},
    {"NSWindow", 'I', "makeKeyAndOrderFront:", oh_nil},
    {"NSWindow", 'I', "setContentView:", oh_win_setcontent},
    {"NSWindow", 'I', "contentView", oh_win_content},
    {"NSWindow", 'I', "center", oh_nil},
    {"NSWindow", 'I', "setAcceptsMouseMovedEvents:", oh_nil},
    {"NSWindow", 'I', "contentRectForFrameRect:", oh_zero_rect},
    {"NSWindow", 'I', "makeMainWindow", oh_nil},
    {"NSWindow", 'I', "isKeyWindow", oh_yes},
    {"NSWindow", 'I', "screen", oh_screen_main},
    {"NSWindow", 'I', "makeFirstResponder:", oh_yes},
    {"NSResponder", 'I', "nextResponder", oh_nil},
    {"NSResponder", 'I', "setNextResponder:", oh_nil},
    {"NSView", 'I', "enterFullScreenMode:withOptions:", oh_yes},
    {"NSView", 'I', "exitFullScreenModeWithOptions:", oh_nil},
    {"NSView", 'I', "isInFullScreenMode", oh_nil},
    {"NSView", 'I', "screen", oh_screen_main},
    {"NSOpenGLView", 'I', "initWithFrame:pixelFormat:", oh_self},
    {"NSOpenGLView", 'I', "openGLContext", oh_ctx_obj},
    {"NSOpenGLContext", 'C', "currentContext", oh_ctx_obj},
    {"NSOpenGLContext", 'I', "makeCurrentContext", oh_nil},
    {"NSOpenGLContext", 'I', "flushBuffer", oh_nil},
    {"NSOpenGLContext", 'I', "update", oh_nil},
    {"NSOpenGLContext", 'I', "CGLContextObj", oh_nil},
    {"NSOpenGLContext", 'I', "setValues:forParameter:", oh_nil},
    {"NSOpenGLPixelFormat", 'I', "initWithAttributes:", oh_self},
    {"NSEvent", 'C', "mouseLocation", oh_fp_zero_point},
    {"NSEvent", 'I', "type", oh_nil},
    {"NSEvent", 'I', "characters", oh_nil},
    {"NSEvent", 'I', "buttonNumber", oh_nil},
    {"NSEvent", 'I', "CGEvent", oh_nil},
    {"NSEvent", 'I', "deltaX", oh_fp0},
    {"NSEvent", 'I', "deltaY", oh_fp0},
    {"NSEvent", 'I', "deltaZ", oh_fp0},
    {"NSString", 'I', "length", oh_str_length},
    {"NSString", 'C', "stringWithFormat:", oh_str_format},
    {"NSString", 'I', "componentsSeparatedByString:", oh_str_components},
    {"NSString", 'I', "pathComponents", oh_str_pathcomps},
    {"NSString", 'C', "pathWithComponents:", oh_str_pathwith},
    {"NSString", 'I', "stringByTrimmingCharactersInSet:", oh_str_trim},
    {"NSMutableArray", 'C', "arrayWithArray:", oh_arr_copyfrom},
    {"NSArray", 'C', "arrayWithArray:", oh_arr_copyfrom},
    {"NSArray", 'I', "addObjectsFromArray:", oh_arr_addall},
    {"NSArray", 'I', "removeObjectAtIndex:", oh_arr_rmat},
    {"NSArray", 'I', "removeObject:", oh_arr_rmobj},
    {"NSArray", 'I', "subarrayWithRange:", oh_arr_sub},
    {"NSArray", 'I', "componentsJoinedByString:", oh_arr_join},
    {"NSArray", 'I', "countByEnumeratingWithState:objects:count:", oh_arr_fastenum},
    {"NSArray", 'C', "array", oh_arr_new},
    {"NSCharacterSet", 'C', "whitespaceCharacterSet", oh_charset},
    {"NSScanner", 'C', "scannerWithString:", oh_scanner_new},
    {"NSScanner", 'I', "setCharactersToBeSkipped:", oh_nil},
    {"NSScanner", 'I', "scanString:intoString:", oh_scan_str},
    {"NSScanner", 'I', "scanUpToString:intoString:", oh_scan_upto},
    {"NSScanner", 'I', "isAtEnd", oh_scan_end},
    {"NSWorkspace", 'C', "sharedWorkspace", NULL},
    {"NSWorkspace", 'I', "runningApplications", oh_ws_running},
    {"NSBundle", 'I', "bundleIdentifier", oh_bundle_id},
    {"NSBundle", 'I', "path", oh_bundle_path},
    {"NSURL", 'I', "path", oh_url_path},
    {0, 0, 0, 0}};
static const char *const SINGLETON_SELS[][2] = {// class-method selectors that return the shared instance
                                                {"NSFileManager", "defaultManager"},
                                                {"NSProcessInfo", "processInfo"},
                                                {"NSRunLoop", "mainRunLoop"},
                                                {"NSApplication", "sharedApplication"},
                                                {0, 0}};

// ---- dispatch ----
static int msg_send(CPU *c, u32 recv, const char *sel, int from_cls, int is_class) {
    static int msglog = -1;
    if (msglog < 0)
        msglog = dbg_env("MSGLOG") ? atoi(dbg_env("MSGLOG")) : 0;
    if (msglog > 0) {
        msglog--;
        fprintf(stderr, "[msg] %c[%s %s]\n", is_class ? '+' : '-',
                from_cls >= 0 ? classes[from_cls].name : "?", sel);
    }
    if (!recv) {
        RET(0);
        c->r[EDX] = 0;
        return 0;
    }
    for (int k = from_cls; k >= 0; k = classes[k].super) {
        Cls *cl = &classes[k];
        if (!is_class)
            for (int i = 0; i < cl->ngm; i++)
                if (!strcmp(cl->gm[i].sel, sel)) {
                    c->eip = cl->gm[i].imp;
                    return 1;
                }
        for (int i = 0; OTAB[i].cls; i++) {
            if (OTAB[i].kind != (is_class ? 'C' : 'I') || strcmp(OTAB[i].cls, cl->name) ||
                strcmp(OTAB[i].sel, sel))
                continue;
            if (!OTAB[i].fn) {
                u32 s = singleton(OTAB[i].cls);
                RET(s);
                if (!strcmp(OTAB[i].cls, "NSApplication"))
                    wr(data_sym("_NSApp"), s, 4);
                return 0;
            }
            return OTAB[i].fn(c, recv);
        }
    }
    int k = from_cls >= 0 ? from_cls : 0;
    if (!strncmp(classes[k].name, "NS",
                 2)) { // host-side Cocoa class: warn once per selector and return nil/0
        static char seen[512][64];
        static int nseen;
        int dup = 0;
        for (int i = 0; i < nseen; i++)
            if (!strcmp(seen[i], sel))
                dup = 1;
        if (!dup && nseen < 512) {
            snprintf(seen[nseen++], 64, "%s", sel);
            fprintf(stderr, "[objc] unhandled %c[%s %s] -> 0\n", is_class ? '+' : '-', classes[k].name, sel);
        }
        RET(0);
        c->r[EDX] = 0;
        return 0;
    }
    die("unhandled ObjC message %c[%s %s]", is_class ? '+' : '-', classes[k].name, sel);
}
static int dispatch(CPU *c, u32 recv, u32 sel, int base) {
    oa_base = base;
    int k = cls_of_addr(recv), is_class = k >= 0;
    if (!is_class) {
        if (!recv) {
            RET(0);
            c->r[EDX] = 0;
            return 0;
        }
        k = class_of_obj(recv);
        if (k < 0)
            die("message to non-object %08x (isa %08x)", recv, rd(recv, 4));
    }
    return msg_send(c, recv, GS(sel), k, is_class);
}
H(objc_msgSend) {
    return dispatch(c, A(0), A(1), 2);
}
H(objc_msgSend_fpret) {
    return dispatch(c, A(0), A(1), 2);
}
H(objc_msgSend_stret) {
    return dispatch(c, A(1), A(2), 3);
}
H(objc_msgSendSuper) { // struct objc_super { id receiver; Class class; }, class = where lookup starts
    u32 sp = A(0), recv = rd(sp, 4), cls = rd(sp + 4, 4);
    int k = cls_of_addr(cls);
    if (k < 0)
        die("objc_msgSendSuper: bad class %08x", cls);
    wr(c->r[ESP] + 4, recv, 4); // guest IMP expects the receiver in the first slot
    oa_base = 2;
    int is_class = cls_of_addr(recv) >= 0;
    return msg_send(c, recv, GS(A(1)), k, is_class);
}

// ---- setup: classes, fixed-up __cls_refs, the game's own class ----
static u32 objc_cls_refs, objc_cls_refs_size, objc_class_sec, objc_class_size;
// Registers the classes an image defines and rewrites its __cls_refs entries to point at host class objects.
static void objc_image(u32 class_sec, u32 class_size, u32 cls_refs, u32 cls_refs_size) {
    for (u32 o = 0; o < class_size; o += 0x30) { // classes defined in the image
        u32 a = class_sec + o, name = rd(a + 8, 4), sup = rd(a + 4, 4), ml = rd(a + 28, 4);
        int k = cls_add(GS(name), 0);
        const char *sn = GS(sup);
        classes[k].super = cls_add(sn, 0);
        if (ml)
            for (u32 i = 0; i < rd(ml + 4, 4) && classes[k].ngm < 32; i++) {
                classes[k].gm[classes[k].ngm].sel = GS(rd(ml + 8 + 12 * i, 4));
                classes[k].gm[classes[k].ngm++].imp = rd(ml + 8 + 12 * i + 8, 4);
            }
        printf("objc: image class %s : %s, %d methods\n", GS(name), sn, classes[k].ngm);
    }
    for (u32 o = 0; o < cls_refs_size; o += 4)
        wr(cls_refs + o, classes[cls_add(GS(rd(cls_refs + o, 4)), 0)].addr, 4);
}
static void objc_init(void) {
    hg_init();
    cls_add("NSObject", 0);
    u32 cfc = data_sym("___CFConstantStringClassReference"); // CFString constants point their isa here
    if (cfc)
        cls_add("NSCFConstantString", cfc);
    // NSString* constants exported by Foundation/AppKit
    wr(data_sym("_NSDefaultRunLoopMode"), nsstr("NSDefaultRunLoopMode"), 4);
    wr(data_sym("_NSFullScreenModeSetting"), nsstr("NSFullScreenModeSetting"), 4);
    {
        u32 yes = obj_of("NSNumber");
        wr(yes + 4, 1, 4);
        wr(data_sym("_kCFBooleanTrue"), yes, 4);
    }
    objc_image(objc_class_sec, objc_class_size, objc_cls_refs, objc_cls_refs_size);
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry objc_imports[] = {
    IMPORT(objc_msgSend), IMPORT(objc_msgSend_fpret), IMPORT(objc_msgSend_stret), IMPORT(objc_msgSendSuper),
    {0, 0}};
// clang-format on
