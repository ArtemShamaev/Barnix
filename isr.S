.section .text

.macro ISR_NOERR num
.global isr\num
.type isr\num, @function
isr\num:
    cli
    push $\num
    call exception_handler
.hang\num:
    hlt
    jmp .hang\num
.endm

.macro ISR_ERR num
.global isr\num
.type isr\num, @function
isr\num:
    cli
    push $\num
    call exception_handler
.hang\num:
    hlt
    jmp .hang\num
.endm

.extern exception_handler

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

.global isr_stub_table
isr_stub_table:
.long isr0
.long isr1
.long isr2
.long isr3
.long isr4
.long isr5
.long isr6
.long isr7
.long isr8
.long isr9
.long isr10
.long isr11
.long isr12
.long isr13
.long isr14
.long isr15
.long isr16
.long isr17
.long isr18
.long isr19
.long isr20
.long isr21
.long isr22
.long isr23
.long isr24
.long isr25
.long isr26
.long isr27
.long isr28
.long isr29
.long isr30
.long isr31

.section .note.GNU-stack,"",@progbits
