// patches.h: binary patches for the supported game build.
//
//
// The game talks to Ubisoft's online service through a client object that it polls every frame. Offline, that connection
// can never come up, and the game reacts by pausing with a "connection lost" menu. Two small patches make the offline stand-in
// (online/orbit.h) look like a healthy connection:
//
//   1. Request function: the game stores "asking" in the client state and relies on a later task to evaluate the reply. Our
//      reply is already in by the time the call returns, so the store is redirected to a stub that stores the state, runs the
//      evaluator on the same client and continues. Without it a frame that polls in between concludes "connection lost".
//   2. Tick function: a `je` guarding the "connection closed" teardown becomes a `jmp`, so the (never connected) socket object
//      is never treated as lost.
//
// Every patch verifies the bytes it replaces and silently does nothing on other builds. Disable all patches with DC_NOPATCH=1.
// The addresses below are specific to the build whose SHA-256 is listed in known_builds[].

// Tiny x86 emitter for the stub below.
typedef struct {
    u32 at;
} Emit;
static void emit_bytes(Emit *e, const u8 *b, int n) {
    memcpy(P(e->at), b, n);
    e->at += n;
}
static void emit_rel(Emit *e, u8 opcode, u32 target) { // call/jmp rel32
    *P(e->at) = opcode;
    wr(e->at + 1, target - (e->at + 5), 4);
    e->at += 5;
}

// Redirects `site` (a 7-byte `movl $1, 0x44(%edi)`) to a stub: state = 1; evaluator(edi); jmp back to `resume`.
static void patch_request_site(u32 site, u32 resume, u32 evaluator) {
    if (*P(site) != 0xc7 || *P(site + 3) != 1)
        return; // not the code we expect: leave it alone
    Emit e = {data_alloc(32)};
    u32 stub = e.at;
    static const u8 set_state[] = {0xc7, 0x47, 0x44, 0x01, 0, 0, 0};  // movl $1, 0x44(%edi)
    static const u8 push_edi = 0x57, drop_arg[] = {0x83, 0xc4, 0x04}; // push edi ... add esp, 4
    emit_bytes(&e, set_state, 7);
    emit_bytes(&e, &push_edi, 1);
    emit_rel(&e, 0xe8, evaluator);
    emit_bytes(&e, drop_arg, 3);
    emit_rel(&e, 0xe9, resume);
    Emit s = {site};
    emit_rel(&s, 0xe9, stub);
    *P(site + 5) = 0x90; // pad the original 7 bytes with nops
    *P(site + 6) = 0x90;
}

static void apply_binary_patches(void) {
    if (DBG_FLAG("NOPATCH"))
        return;
    enum { REQUEST_EVALUATOR = 0x9bdac0, TICK_LOST_CHECK = 0x9bdfb6 };
    patch_request_site(0x9bd911, 0x9bd918, REQUEST_EVALUATOR);
    patch_request_site(0x9bd94c, 0x9bd953, REQUEST_EVALUATOR);
    if (*P(TICK_LOST_CHECK) == 0x74) // je -> jmp
        *P(TICK_LOST_CHECK) = 0xeb;
}

// The protector overwrites the first entry of the static-initialiser table. Every translation unit's initialiser is the stub
// `push ebp; mov edx,0xffff; mov ebp,esp; mov eax,1; leave; jmp ...`; the first such stub in __text is the one that was lost.
static u32 recover_first_initializer(void) {
    static const u8 stub[] = {0x55, 0xba, 0xff, 0xff, 0x00, 0x00, 0x89, 0xe5,
                              0xb8, 0x01, 0x00, 0x00, 0x00, 0xc9, 0xe9};
    for (u32 a = 0x28c0; a < 0x28c0 + 0x1000; a++)
        if (!memcmp(P(a), stub, sizeof stub))
            return a;
    return 0;
}
