// platform.h: host system headers shared by every module.
//
// Everything is compiled as one translation unit (see docs/architecture.md); this is its first include.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <setjmp.h>

// Guest imports are served inside setjmp() frames at a very high rate, so use the variants that do not touch the signal mask.
#define setjmp(e) _setjmp(e)
#define longjmp(e, v) _longjmp(e, v)

#define GL_SILENCE_DEPRECATION 1
#define GL_GLEXT_PROTOTYPES 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenGL/glext.h>

#include <sys/mman.h>
#include <sys/time.h>
#include <mach/mach_time.h>
#include <mach/machine.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "host/hostgfx.h"
