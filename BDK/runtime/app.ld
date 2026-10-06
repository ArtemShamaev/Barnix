OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS
{
    . = 0x01000000;
    .text : { *(.text*) }
    .rodata ALIGN(4096) : { *(.rodata*) }
    .data ALIGN(4096) : {
        *(.data*)
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array))
        __fini_array_end = .;
    }
    .bss ALIGN(4096) : { *(COMMON) *(.bss*) }
    ASSERT(. <= 0x013F0000, "BDK application exceeds Barnix load arena")
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
