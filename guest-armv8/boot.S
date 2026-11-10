.section ".text.boot"

.global _start
.global trigger_svc_trap

_start:

    mrs x0, mpidr_el1
    and x0, x0, #0xFF
    cbz x0, master_core

hang_core:
    wfe
    b   hang_core

master_core:
    ldr     x0, =__stack_top
    mov     sp, x0 

    ldr     x0, =exception_vector_table
    msr     vbar_el1, x0
    isb 

    ldr      x0, =__bss_start
    ldr     x1, =__bss_end
bss_loop:
    cmp     x0, x1
    b.ge    launch_kernel
    str  xzr, [x0], #8
    b    bss_loop

launch_kernel:
    bl      guest_kernel_main 
    bl      hang_core 

trigger_svc_trap:
    svc     #0x42
    ret 

.balign 0x800
.global exception_vector_table
exception_vector_table:
    b       sync_exception_entry
    .balign 0x80
    b       unhandled_irq
    .balign 0x80
    b       unhandled_fiq
    .balign 0x80
    b       unhandled_serror
    .balign 0x80
    b       sync_exception_entry
    .balign 0x80
    b       unhandled_irq 
    .balign 0x80 
    b       unhandled_fiq
    .balign 0x80 
    b       unhandled_serror


sync_exception_entry:
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!

    mrs     x0, esr_el1
    mrs     x1, elr_el1
    bl      handle_sync_exception

    ldp     x29, x30, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16

    eret 

unhandled_irq:
unhandled_fiq:
unhandled_serror:
    wfe
    b       unhandled_irq