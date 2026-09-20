#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOT_MAGIC 0x2BADB002U
#define MULTIBOOT_CMDLINE (1U << 2)
#define MULTIBOOT_MODULES (1U << 3)

typedef struct {
    uint32_t start, end, command_line, reserved;
} MultibootModule;

/* Prefix of the Multiboot v1 information structure supplied by GRUB. */
typedef struct {
    uint32_t flags, mem_lower, mem_upper, boot_device;
    uint32_t command_line, module_count, modules;
} MultibootInfo;

#endif
