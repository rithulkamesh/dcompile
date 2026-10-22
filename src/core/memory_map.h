// memory_map.h: the layout of the guest's 4 GiB address space.
//
//   0x00000000 - 0x00000fff   guard page: reads/writes here are guest bugs (null pointers) and stop the run
//   0x00001000 - ...          the main executable, at its link addresses
//   0x10000000 - 0x7fffffff   guest heap (runtime/heap.h)
//   0x88000000 - ...          stacks of guest threads 1.. (4 MiB each, runtime/sched.h)
//   0xa0000000 - ...          further images loaded with dlopen (32 MiB slots, loader/dyld.h)
//   0xc0000000                top of the main thread's stack (grows down)
//   0xe0000000 - 0xe0ffffff   host-managed data the guest holds pointers to: imported variables and vtables (DATA_BASE),
//                             fake FILE objects, Objective-C class objects, directory and image handles, pthread_t values
//   0xeffe0000, 0xefff0000    magic return addresses: "a thread's start routine returned" / "a host->guest call returned"
//   0xf0000000 - ...          thunks: one address per imported symbol that the host implements (core/thunk_table.h)
//
// Wait-object keys used by the scheduler (0xfd000000 thread-join, 0xfe000000 semaphores) are not addresses and live in
// runtime/threads.h.

#define HEAP_BASE 0x10000000u
#define HEAP_LIMIT 0x80000000u
#define THREAD_STACK_BASE 0x88000000u
#define THREAD_STACK_SIZE 0x400000u
#define DYLIB_BASE 0xA0000000u
#define DYLIB_STRIDE 0x02000000u
#define STACK_TOP 0xC0000000u

#define DATA_BASE 0xE0000000u
#define FILE_BASE 0xE0010000u         // fake FILE objects, 0x100 apart
#define CLASS_BASE 0xE0100000u        // Objective-C class objects
#define DIR_HANDLE_BASE 0xE0200000u   // DIR* values, 16 apart
#define IMAGE_HANDLE_BASE 0xE0300000u // dlopen handles, 16 apart
#define PTHREAD_BASE 0xE0400000u      // pthread_t = PTHREAD_BASE + 0x100 * thread index

#define THREAD_RET 0xEFFE0000u
#define RET_MAGIC 0xEFFF0000u
#define THUNK_BASE 0xF0000000u
