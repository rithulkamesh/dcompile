// gl.h: the game's OpenGL calls forwarded to the host's OpenGL.
//
// The game uses legacy fixed-function GL plus ARB vertex/fragment assembly programs, FBOs, VBOs and S3TC (103 entry points).
// Apple's legacy GL 2.1 driver sits on top of Metal and supports all of it, so no shader translation is needed. Guest pointers
// are host pointers (guest memory is one flat mapping) unless a buffer object is bound, in which case the "pointer" is an offset.
//
// Debugging: DC_GLLOG logs every call, DC_GLERR checks glGetError, and the capture hooks in video/capture.h grab a frame.

static u32 gl_bound[8]; // 0 array, 1 element, 2 pixel unpack, 3 pixel pack
static int gl_slot(GLenum t) {
    return t == GL_ARRAY_BUFFER           ? 0
           : t == GL_ELEMENT_ARRAY_BUFFER ? 1
           : t == GL_PIXEL_UNPACK_BUFFER  ? 2
           : t == GL_PIXEL_PACK_BUFFER    ? 3
                                          : -1;
}
static const void *gp_slot(u32 a, int slot) {
    return gl_bound[slot] ? (const void *)(uintptr_t)a : gp(a);
}
static u32 gl_trace;

static u64 gl_ncalls;
static int gl_log; // gl_logf: one-frame trace written by video/capture.h
#define GLH(name, body)                                                                                      \
    H(name) {                                                                                                \
        gl_ncalls++;                                                                                         \
        if (gl_logf)                                                                                         \
            fprintf(gl_logf, #name " %x %x %x %x\n", A(0), A(1), A(2), A(3));                                \
        else if (gl_log)                                                                                     \
            fprintf(stderr, "[gl] " #name "\n");                                                             \
        body;                                                                                                \
        return 0;                                                                                            \
    }
GLH(glActiveTexture, glActiveTexture(A(0)))
GLH(glAlphaFunc, glAlphaFunc(A(0), Af(c, 1)))
GLH(glBegin, glBegin(A(0)))
GLH(glBeginQuery, glBeginQuery(A(0), A(1)))
static void bind_buf(u32 t, u32 id) {
    int s = gl_slot(t);
    if (s >= 0)
        gl_bound[s] = id;
    glBindBuffer(t, id);
}
GLH(glBindBuffer, bind_buf(A(0), A(1)))
GLH(glBindBufferARB, bind_buf(A(0), A(1)))
GLH(glBindFramebufferEXT, glBindFramebufferEXT(A(0), A(1)))
GLH(glBindProgramARB, glBindProgramARB(A(0), A(1)))
GLH(glBindRenderbufferEXT, glBindRenderbufferEXT(A(0), A(1)))
GLH(glBindTexture, glBindTexture(A(0), A(1)))
GLH(glBlendColor, glBlendColor(Af(c, 0), Af(c, 1), Af(c, 2), Af(c, 3)))
GLH(glBlendEquation, glBlendEquation(A(0)))
GLH(glBlendFunc, glBlendFunc(A(0), A(1)))
GLH(glBlitFramebufferEXT,
    glBlitFramebufferEXT((GLint)A(0), (GLint)A(1), (GLint)A(2), (GLint)A(3), (GLint)A(4), (GLint)A(5),
                         (GLint)A(6), (GLint)A(7), A(8), A(9)))
GLH(glBufferDataARB, glBufferData(A(0), A(1), gp(A(2)), A(3)))
GLH(glBufferSubData, glBufferSubData(A(0), A(1), A(2), gp(A(3))))
H(glCheckFramebufferStatusEXT) {
    RET(glCheckFramebufferStatusEXT(A(0)));
    return 0;
}
GLH(glClear, glClear(A(0)))
GLH(glClearColor, glClearColor(Af(c, 0), Af(c, 1), Af(c, 2), Af(c, 3)))
GLH(glClearDepth, glClearDepth(Ad(c, 0)))
GLH(glClearStencil, glClearStencil((GLint)A(0)))
GLH(glColorMask, glColorMask(A(0) & 0xff, A(1) & 0xff, A(2) & 0xff, A(3) & 0xff))
GLH(glCompressedTexImage1DARB,
    glCompressedTexImage1D(A(0), (GLint)A(1), A(2), A(3), (GLint)A(4), A(5), gp_slot(A(6), 2)))
GLH(glCompressedTexImage2DARB,
    glCompressedTexImage2D(A(0), (GLint)A(1), A(2), A(3), A(4), (GLint)A(5), A(6), gp_slot(A(7), 2)))
GLH(glCompressedTexImage3DARB,
    glCompressedTexImage3D(A(0), (GLint)A(1), A(2), A(3), A(4), A(5), (GLint)A(6), A(7), gp_slot(A(8), 2)))
GLH(glCompressedTexSubImage1DARB,
    glCompressedTexSubImage1D(A(0), (GLint)A(1), (GLint)A(2), A(3), A(4), A(5), gp_slot(A(6), 2)))
GLH(glCompressedTexSubImage2DARB, glCompressedTexSubImage2D(A(0), (GLint)A(1), (GLint)A(2), (GLint)A(3), A(4),
                                                            A(5), A(6), A(7), gp_slot(A(8), 2)))
GLH(glCompressedTexSubImage3DARB,
    glCompressedTexSubImage3D(A(0), (GLint)A(1), (GLint)A(2), (GLint)A(3), (GLint)A(4), A(5), A(6), A(7),
                              A(8), A(9), gp_slot(A(10), 2)))
GLH(glCullFace, glCullFace(A(0)))
GLH(glDeleteBuffersARB, glDeleteBuffers(A(0), (const GLuint *)gp(A(1))))
GLH(glDeleteFramebuffersEXT, glDeleteFramebuffersEXT(A(0), (const GLuint *)gp(A(1))))
GLH(glDeleteProgramsARB, glDeleteProgramsARB(A(0), (const GLuint *)gp(A(1))))
GLH(glDeleteQueries, glDeleteQueries(A(0), (const GLuint *)gp(A(1))))
GLH(glDeleteRenderbuffersEXT, glDeleteRenderbuffersEXT(A(0), (const GLuint *)gp(A(1))))
GLH(glDeleteTextures, glDeleteTextures(A(0), (const GLuint *)gp(A(1))))
GLH(glDepthFunc, glDepthFunc(A(0)))
GLH(glDepthMask, glDepthMask(A(0) & 0xff))
GLH(glDepthRange, glDepthRange(Ad(c, 0), Ad(c, 2)))
GLH(glDisable, glDisable(A(0)))
GLH(glDisableVertexAttribArray, glDisableVertexAttribArray(A(0)))
GLH(glDrawArrays, glDrawArrays(A(0), (GLint)A(1), (GLsizei)A(2)))
GLH(glDrawElements, glDrawElements(A(0), (GLsizei)A(1), A(2), gp_slot(A(3), 1)))
GLH(glEnable, glEnable(A(0)))
GLH(glEnableVertexAttribArray, glEnableVertexAttribArray(A(0)))
GLH(glEnd, glEnd())
GLH(glEndQuery, glEndQuery(A(0)))
GLH(glFlush, glFlush())
GLH(glFramebufferRenderbufferEXT, glFramebufferRenderbufferEXT(A(0), A(1), A(2), A(3)))
GLH(glFramebufferTexture2DEXT, glFramebufferTexture2DEXT(A(0), A(1), A(2), A(3), (GLint)A(4)))
GLH(glFrontFace, glFrontFace(A(0)))
// glGen*(n, ids *): generate on the host, copy the names into guest memory
#define GEN(name, fn)                                                                                        \
    H(name) {                                                                                                \
        u32 n = A(0);                                                                                        \
        GLuint *t = malloc(4 * (n + 1));                                                                     \
        fn(n, t);                                                                                            \
        memcpy(P(A(1)), t, 4 * n);                                                                           \
        free(t);                                                                                             \
        return 0;                                                                                            \
    }
GEN(glGenBuffersARB, glGenBuffers)
GEN(glGenFramebuffersEXT, glGenFramebuffersEXT)
GEN(glGenProgramsARB, glGenProgramsARB)
GEN(glGenQueries, glGenQueries)
GEN(glGenRenderbuffersEXT, glGenRenderbuffersEXT)
GEN(glGenTextures, glGenTextures) static int get_count(GLenum p) {
    switch (p) {
    case GL_VIEWPORT:
    case GL_SCISSOR_BOX:
    case GL_COLOR_CLEAR_VALUE:
    case GL_COLOR_WRITEMASK:
    case GL_CURRENT_COLOR:
    case GL_BLEND_COLOR:
        return 4;
    case GL_MODELVIEW_MATRIX:
    case GL_PROJECTION_MATRIX:
    case GL_TEXTURE_MATRIX:
        return 16;
    case GL_MAX_VIEWPORT_DIMS:
    case GL_DEPTH_RANGE:
    case GL_POINT_SIZE_RANGE:
    case GL_LINE_WIDTH_RANGE:
        return 2;
    default:
        return 1;
    }
}
H(glGetBooleanv) {
    GLboolean t[16] = {0};
    glGetBooleanv(A(0), t);
    memcpy(P(A(1)), t, get_count(A(0)));
    return 0;
}
H(glGetFloatv) {
    GLfloat t[16] = {0};
    glGetFloatv(A(0), t);
    memcpy(P(A(1)), t, 4 * get_count(A(0)));
    return 0;
}
H(glGetIntegerv) {
    GLint t[16] = {0};
    glGetIntegerv(A(0), t);
    memcpy(P(A(1)), t, 4 * get_count(A(0)));
    return 0;
}
H(glGetError) {
    u32 e = glGetError();
    if (e && DBG_FLAG("GLERR"))
        fprintf(stderr, "[gl] glGetError -> 0x%x\n", e);
    RET(e);
    return 0;
}
H(glGetQueryObjectiv) {
    GLint v = 0;
    glGetQueryObjectiv(A(0), A(1), &v);
    wr(A(2), (u32)v, 4);
    return 0;
}
GLH(glLoadIdentity, glLoadIdentity())
// glMapBufferARB: the host mapping is a 64-bit pointer, so map a guest shadow copy and write it back in glUnmapBufferARB
static struct {
    u32 block, size, access;
} gl_map[4];
H(glMapBufferARB) {
    int s = gl_slot(A(0));
    GLint size = 0;
    glGetBufferParameteriv(A(0), GL_BUFFER_SIZE, &size);
    if (s < 0 || !size) {
        RET(0);
        return 0;
    }
    gl_map[s].block = hmalloc(size);
    gl_map[s].size = size;
    gl_map[s].access = A(1);
    if (A(1) != GL_WRITE_ONLY)
        glGetBufferSubData(A(0), 0, size, P(gl_map[s].block));
    else
        memset(P(gl_map[s].block), 0, size);
    RET(gl_map[s].block);
    return 0;
}
H(glUnmapBufferARB) {
    int s = gl_slot(A(0));
    if (s < 0 || !gl_map[s].block) {
        RET(0);
        return 0;
    }
    if (gl_map[s].access != GL_READ_ONLY)
        glBufferSubData(A(0), 0, gl_map[s].size, P(gl_map[s].block));
    free_guest(gl_map[s].block);
    gl_map[s].block = 0;
    RET(1);
    return 0;
}
GLH(glMatrixMode, glMatrixMode(A(0)))
GLH(glPixelStorei, glPixelStorei(A(0), (GLint)A(1)))
GLH(glPolygonMode, glPolygonMode(A(0), A(1)))
GLH(glPolygonOffset, glPolygonOffset(Af(c, 0), Af(c, 1)))
GLH(glPopAttrib, glPopAttrib())
GLH(glPopMatrix, glPopMatrix())
GLH(glProgramEnvParameters4fvEXT,
    glProgramEnvParameters4fvEXT(A(0), A(1), (GLsizei)A(2), (const GLfloat *)gp(A(3))))
GLH(glProgramLocalParameters4fvEXT,
    glProgramLocalParameters4fvEXT(A(0), A(1), (GLsizei)A(2), (const GLfloat *)gp(A(3))))
H(glProgramStringARB) {
    glProgramStringARB(A(0), A(1), (GLsizei)A(2), gp(A(3)));
    if (glGetError() == GL_INVALID_OPERATION) { // ARB program rejected: show why
        GLint pos = -1;
        glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
        fprintf(stderr, "[gl] ARB program error at %d: %s\n", pos,
                (const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB));
    }
    return 0;
}
GLH(glPushAttrib, glPushAttrib(A(0)))
GLH(glPushMatrix, glPushMatrix())
GLH(glReadBuffer, glReadBuffer(A(0)))
GLH(glReadPixels, glReadPixels((GLint)A(0), (GLint)A(1), A(2), A(3), A(4), A(5), (void *)gp_slot(A(6), 3)))
GLH(glRenderbufferStorageEXT, glRenderbufferStorageEXT(A(0), A(1), A(2), A(3)))
GLH(glRenderbufferStorageMultisampleEXT, glRenderbufferStorageMultisampleEXT(A(0), A(1), A(2), A(3), A(4)))
GLH(glRotatef, glRotatef(Af(c, 0), Af(c, 1), Af(c, 2), Af(c, 3)))
GLH(glStencilFunc, glStencilFunc(A(0), (GLint)A(1), A(2)))
GLH(glStencilMask, glStencilMask(A(0)))
GLH(glStencilOp, glStencilOp(A(0), A(1), A(2)))
GLH(glTexCoord2f, glTexCoord2f(Af(c, 0), Af(c, 1)))
GLH(glTexEnvf, glTexEnvf(A(0), A(1), Af(c, 2)))
GLH(glTexImage1D,
    glTexImage1D(A(0), (GLint)A(1), (GLint)A(2), A(3), (GLint)A(4), A(5), A(6), gp_slot(A(7), 2)))
GLH(glTexImage2D,
    glTexImage2D(A(0), (GLint)A(1), (GLint)A(2), A(3), A(4), (GLint)A(5), A(6), A(7), gp_slot(A(8), 2)))
GLH(glTexImage3D,
    glTexImage3D(A(0), (GLint)A(1), (GLint)A(2), A(3), A(4), A(5), (GLint)A(6), A(7), A(8), gp_slot(A(9), 2)))
GLH(glTexParameterfv, glTexParameterfv(A(0), A(1), (const GLfloat *)gp(A(2))))
GLH(glTexParameteri, glTexParameteri(A(0), A(1), (GLint)A(2)))
GLH(glTexSubImage1D, glTexSubImage1D(A(0), (GLint)A(1), (GLint)A(2), A(3), A(4), A(5), gp_slot(A(6), 2)))
GLH(glTexSubImage2D,
    glTexSubImage2D(A(0), (GLint)A(1), (GLint)A(2), (GLint)A(3), A(4), A(5), A(6), A(7), gp_slot(A(8), 2)))
GLH(glTexSubImage3D, glTexSubImage3D(A(0), (GLint)A(1), (GLint)A(2), (GLint)A(3), (GLint)A(4), A(5), A(6),
                                     A(7), A(8), A(9), gp_slot(A(10), 2)))
GLH(glTranslatef, glTranslatef(Af(c, 0), Af(c, 1), Af(c, 2)))
GLH(glVertex3f, glVertex3f(Af(c, 0), Af(c, 1), Af(c, 2)))
GLH(glVertexAttribPointer,
    glVertexAttribPointer(A(0), (GLint)A(1), A(2), A(3) & 0xff, (GLsizei)A(4), gp_slot(A(5), 0)))
static int vplog;
H(glViewport) {
    gl_ncalls++;
    if (vplog++ < 12 && DBG_FLAG("WINLOG"))
        fprintf(stderr, "[gl] glViewport %d %d %u %u\n", (int)A(0), (int)A(1), A(2), A(3));
    glViewport((GLint)A(0), (GLint)A(1), (GLsizei)A(2), (GLsizei)A(3));
    return 0;
}
H(gluErrorString) {
    RET(gstr((const char *)gluErrorString(A(0))));
    return 0;
}
GLH(gluPerspective, gluPerspective(Ad(c, 0), Ad(c, 2), Ad(c, 4), Ad(c, 6)))

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry gl_imports[] = {
    IMPORT(glActiveTexture), IMPORT(glAlphaFunc), IMPORT(glBegin), IMPORT(glBeginQuery),
    IMPORT(glBindBuffer), IMPORT(glBindBufferARB), IMPORT(glBindFramebufferEXT), IMPORT(glBindProgramARB),
    IMPORT(glBindRenderbufferEXT), IMPORT(glBindTexture), IMPORT(glBlendColor), IMPORT(glBlendEquation),
    IMPORT(glBlendFunc), IMPORT(glBlitFramebufferEXT), IMPORT(glBufferDataARB), IMPORT(glBufferSubData),
    IMPORT(glCheckFramebufferStatusEXT), IMPORT(glClear), IMPORT(glClearColor), IMPORT(glClearDepth),
    IMPORT(glClearStencil), IMPORT(glColorMask), IMPORT(glCompressedTexImage1DARB),
    IMPORT(glCompressedTexImage2DARB), IMPORT(glCompressedTexImage3DARB),
    IMPORT(glCompressedTexSubImage1DARB), IMPORT(glCompressedTexSubImage2DARB),
    IMPORT(glCompressedTexSubImage3DARB), IMPORT(glCullFace), IMPORT(glDeleteBuffersARB),
    IMPORT(glDeleteFramebuffersEXT), IMPORT(glDeleteProgramsARB), IMPORT(glDeleteQueries),
    IMPORT(glDeleteRenderbuffersEXT), IMPORT(glDeleteTextures), IMPORT(glDepthFunc), IMPORT(glDepthMask),
    IMPORT(glDepthRange), IMPORT(glDisable), IMPORT(glDisableVertexAttribArray), IMPORT(glDrawArrays),
    IMPORT(glDrawElements), IMPORT(glEnable), IMPORT(glEnableVertexAttribArray), IMPORT(glEnd),
    IMPORT(glEndQuery), IMPORT(glFlush), IMPORT(glFramebufferRenderbufferEXT),
    IMPORT(glFramebufferTexture2DEXT), IMPORT(glFrontFace), IMPORT(glGenBuffersARB),
    IMPORT(glGenFramebuffersEXT), IMPORT(glGenProgramsARB), IMPORT(glGenQueries),
    IMPORT(glGenRenderbuffersEXT), IMPORT(glGenTextures), IMPORT(glGetBooleanv), IMPORT(glGetError),
    IMPORT(glGetFloatv), IMPORT(glGetIntegerv), IMPORT(glGetQueryObjectiv), IMPORT(glLoadIdentity),
    IMPORT(glMapBufferARB), IMPORT(glMatrixMode), IMPORT(glPixelStorei), IMPORT(glPolygonMode),
    IMPORT(glPolygonOffset), IMPORT(glPopAttrib), IMPORT(glPopMatrix), IMPORT(glProgramEnvParameters4fvEXT),
    IMPORT(glProgramLocalParameters4fvEXT), IMPORT(glProgramStringARB), IMPORT(glPushAttrib),
    IMPORT(glPushMatrix), IMPORT(glReadBuffer), IMPORT(glReadPixels), IMPORT(glRenderbufferStorageEXT),
    IMPORT(glRenderbufferStorageMultisampleEXT), IMPORT(glRotatef), IMPORT(glStencilFunc),
    IMPORT(glStencilMask), IMPORT(glStencilOp), IMPORT(glTexCoord2f), IMPORT(glTexEnvf),
    IMPORT(glTexImage1D), IMPORT(glTexImage2D), IMPORT(glTexImage3D), IMPORT(glTexParameterfv),
    IMPORT(glTexParameteri), IMPORT(glTexSubImage1D), IMPORT(glTexSubImage2D), IMPORT(glTexSubImage3D),
    IMPORT(glTranslatef), IMPORT(glUnmapBufferARB), IMPORT(glVertex3f), IMPORT(glVertexAttribPointer),
    IMPORT(glViewport), IMPORT(gluErrorString), IMPORT(gluPerspective),
    {0, 0}};
// clang-format on
