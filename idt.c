#include "idt.h"
#include "barnix.h"

#define IDT_ENTRIES 256
#define KERNEL_CODE_SEGMENT 0x08
#define IDT_INTERRUPT_GATE 0x8E

typedef struct
{
    unsigned short offset_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char type_attr;
    unsigned short offset_high;
} __attribute__((packed)) IdtEntry;

typedef struct
{
    unsigned short limit;
    unsigned int base;
} __attribute__((packed)) IdtPointer;

extern void *isr_stub_table[];

static IdtEntry idt[IDT_ENTRIES];
static IdtPointer idt_pointer;

static const char *exception_messages[] =
{
    "division by zero",
    "debug exception",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid TSS",
    "segment not present",
    "stack-segment fault",
    "general protection fault",
    "page fault",
    "reserved exception",
    "x87 floating-point exception",
    "alignment check",
    "machine check",
    "SIMD floating-point exception",
    "virtualization exception",
    "control protection exception",
    "reserved exception",
    "reserved exception",
    "reserved exception",
    "reserved exception",
    "reserved exception",
    "reserved exception",
    "hypervisor injection exception",
    "VMM communication exception",
    "security exception",
    "reserved exception"
};

static void idt_set_gate(int vector, unsigned int handler)
{
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = KERNEL_CODE_SEGMENT;
    idt[vector].zero = 0;
    idt[vector].type_attr = IDT_INTERRUPT_GATE;
    idt[vector].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));

    for (int i = 0; i < 32; i++)
        idt_set_gate(i, (unsigned int)isr_stub_table[i]);

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = (unsigned int)idt;

    __asm__ volatile("lidt %0" : : "m"(idt_pointer));
}

void exception_handler(unsigned int vector)
{
    if (vector < 32)
        panic(exception_messages[vector]);

    panic("unknown CPU exception");
}
