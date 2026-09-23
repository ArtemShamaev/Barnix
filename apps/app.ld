OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS
{
    . = 0x01000000;
    .text : { *(.text*) }
    .rodata ALIGN(4096) : { *(.rodata*) }
    .data ALIGN(4096) : { *(.data*) }
    .bss ALIGN(4096) : { *(COMMON) *(.bss*) }
    ASSERT(. <= 0x013F0000, "Application exceeds load arena")
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
