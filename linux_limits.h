#ifndef BARNIX_LINUX_LIMITS_H
#define BARNIX_LINUX_LIMITS_H

/* Large, static Linux toolchain images live above the kernel's identity map.
 * The ordinary 4 MiB i386 window and the legacy Barnix arena are unchanged. */
#define LINUX_LARGE_BASE 0x80000000U
#define LINUX_LARGE_ARENA_SIZE 0x10000000U
#define LINUX_LARGE_BITMAP_SIZE (LINUX_LARGE_ARENA_SIZE >> 15)
#define LINUX_LARGE_STACK_SIZE 0x00800000U

#endif
