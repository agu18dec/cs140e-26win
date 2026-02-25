#include "rpi-asm.h"

MK_FN(switchto_user_asm)
    mov r14, r0 @ store base ptr
    ldm r14, {r0-r12}
    @ need to restore user bank sp and lr which are at 52, 
    add r14, r14, #52 
    ldm r14, {sp, lr}^
    add r14, r14, #8 @ for pc
    mov sp, r14              @ put pair address into sp
    rfe sp


@ NOTE:  set <cpsr> using the following instructon so GNU
@ assembler will set all the bits.  get mismatches otherwise
@   msr   cpsr_cxsf, <reg>
MK_FN(switchto_priv_asm)
    mov r4, r0
    ldr r1, [r4, #(16*4)]
    msr cpsr_cxsf, r1 
    prefetch_flush(r2)
    ldm r4, {r0-r15}   

    asm_not_reached();

@ void priv_get_sp_lr_asm(uint32_t mode, uint32_t *sp, uint32_t *lr);
@   - assumes caller is at privileged mode [we could check this]
@   - mode in r0
@   - sp pointer in r1
@   - lr pointer in r2
@
MK_FN(priv_get_sp_lr_asm)
    mrs r3, cpsr                
    msr cpsr_cxsf, r0   @ switch to requested privileged mode
    prefetch_flush(r12)
    str sp, [r1]          @ write that mode's sp
    str lr, [r2]        @ write that mode's lr
    msr cpsr_cxsf, r3      @ restore original mode
    prefetch_flush(r12)
    bx lr

@ you'll have to write this, but it should mirror your other save
@ routines.
@
@ void cswitchto_priv_asm(regs_t *old, regs_t *next);
@
@ voluntary context switch: save the current registers in <old>
@ and call the right switchto on <next>
MK_FN(cswitchto_priv_asm)
    @ initially just forward to ours.
    b staff_cswitchto_priv_asm

    @ to implement:
    @ 1. save all the callee regs (just like in your threads library)
    @    into the 17-entry register array we have been using.  note
    @    these have to be at the right offsets.
    @    e.g.:
    @       str r4,  [r0, #(4*4)]  @ r4
    @       str r5,  [r0, #(5*4)]  @ r5
    @       ... etc ...
    @   don't forget the cpsr!


    @ 2. just call the right switch routine: note, we should
    @    *not* reach this location when switching back!
    bl switchto_priv_asm
    asm_not_reached();

@ you'll have to write this, but it should mirror your other save
@ routines.
@
@ void cswitchto_user_asm(regs_t *old, regs_t *next);
@
@ voluntary context switch: save the current registers in <old>
@ and call the right switchto on <next>
MK_FN(cswitchto_user_asm)
    @ initially just forward to ours.
    b staff_cswitchto_user_asm

    asm_todo("save the registers: identical to <switchto_priv_asm>")
    bl switchto_user_asm
    asm_not_reached();
