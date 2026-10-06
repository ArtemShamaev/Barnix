#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOT_MAGIC 0x2BADB002U
#define MULTIBOOT_CMDLINE (1U << 2)
#define MULTIBOOT_MODULES (1U << 3)

typedef struct {
    uint32_t start, end, command_line, reserved;
} MultibootModule;

/* Multiboot v1 information through RGB framebuffer fields.
 * Color-union alignment follows GRUB include/multiboot.h. */
typedef struct {
    uint32_t flags, mem_lower, mem_upper, boot_device;
    uint32_t command_line, module_count, modules;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr, drives_length, drives_addr;
    uint32_t config_table, boot_loader_name, apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint32_t framebuffer_low, framebuffer_high, framebuffer_pitch;
    uint32_t framebuffer_width, framebuffer_height;
    uint8_t framebuffer_bpp, framebuffer_type;
    uint16_t framebuffer_reserved; /* color union is aligned to four bytes */
    uint8_t red_position, red_size, green_position, green_size, blue_position, blue_size;
} __attribute__((packed)) MultibootInfo;
_Static_assert(__builtin_offsetof(MultibootInfo, framebuffer_low) == 88, "Multiboot framebuffer offset");

_Static_assert(__builtin_offsetof(MultibootInfo, red_position) == 112, "Multiboot color offset");
#endif
