/* x86_64 kernel: identity-mapped at 1 MiB, entered in 32-bit mode by GRUB. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text :
    {
        KEEP(*(.multiboot))
        *(.text.boot)
        *(.text*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata*)
    }

    .data ALIGN(4K) :
    {
        *(.data*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss*)
    }
    ASSERT(. <= 0x01000000, "Kernel overlaps application arena")
    /DISCARD/ : { *(.eh_frame) *(.note.gnu.property) *(.comment) }
}
