// thunk_table.h: the table of host-implemented imports.
//
// Every symbol the guest imports that is not defined by a loaded image is given a thunk address THUNK_BASE + index. Calling or
// jumping to it traps into the host (runtime/imports.h). tnames[i] is the symbol name of thunk i.

static char *tnames[4096];
static int nthunks;
