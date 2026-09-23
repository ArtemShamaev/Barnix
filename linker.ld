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
    .app_arena 0x01000000 (NOLOAD) :
    {
        . += 0x00400000;
    }
    /* Three process snapshots, separate from the running 16..20 MiB arena. */
    .task_arena 0x01400000 (NOLOAD) :
    {
        . += 0x00c00000;
    }
}
