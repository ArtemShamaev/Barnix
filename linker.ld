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

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss :
    {
        *(COMMON)
        *(.bss*)
    }
}
