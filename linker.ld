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
}
