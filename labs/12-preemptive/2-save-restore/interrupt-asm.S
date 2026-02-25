#include "rpi-asm.h"

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ part 1: used by <2-simple-save-test.c>
@

@ terminal syscall: print the registers we have.
syscall_reg_save:
    sub sp, sp, #68 @ 17 words
    stm sp, {r0-r12} @r13 is now supervisor mode and so banked registers
    add r1, sp, #52     @ &regs[13]
    stm r1, {sp, lr}^
    str lr, [sp, #60] @ lr == return pc
    mrs r2, spsr
    str r2, [sp, #64]
    mov r0, sp @ pass pointer to register array
    bl syscall_handler
    1:  b 1b

@ use rfe to load <pc> and <cpsr> from the 2 entry
@ array passed in <r0>.
MK_FN(rfe_asm)
    mov sp, r0
    rfe sp
    asm_not_reached();

@ for test 1.
.align 5; 
.globl syscall_reg_save_vec
syscall_reg_save_vec:
    b unhandled_reset
    b unhandled_undefined_instruction
    b syscall_reg_save
    b unhandled_prefetch_abort
    b unhandled_data_abort
    b unhandled_interrupt
    asm_not_reached();

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ used by the rest of tests.

@ don't forget to fix lr!
prefetch_reg_save:
    @pass in r0 as an argument to prefetch_abort_handler, save r0-15 + cpsr
    mov sp, #INT_STACK_ADDR
    sub sp, sp, #68
    stm sp, {r0-r12}
    add r1, sp, #52     @ &regs[13]
    stm r1, {sp, lr}^
    sub r2, lr, #4
    str r2, [sp, #60] @ lr == return pc
    mrs r2, spsr
    str r2, [sp, #64]
    mov r0, sp @ pass pointer to register array
    bl prefetch_abort_handler
    1:  b 1b

.align 5; 
.globl simple_reg_save_restore_vec
simple_reg_save_restore_vec:
    b unhandled_reset
    b unhandled_undefined_instruction
    b unhandled_swi
    b prefetch_reg_save
    b unhandled_data_abort
    b unhandled_interrupt
    asm_not_reached();

@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@ weak symbols used not all tests define all handlers.

@ so we can compile if they don't provide
.weak syscall_handler
MK_FN(syscall_handler)
    asm_not_reached();

@ so we can compile if they don't provide
.weak prefetch_abort_handler
MK_FN(prefetch_abort_handler)
    asm_not_reached();
