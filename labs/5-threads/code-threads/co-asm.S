#include "rpi-asm.h"

.global co_switch_asm
co_switch_asm:
    // r0 = old, r1 = new

    // save r4-r11
    add r2, r0, #(4*4)
    stmia r2, {r4-r11}

    // save sp, lr
    str sp, [r0, #(13*4)]
    str lr, [r0, #(14*4)]

    // restore r4-r11
    add r2, r1, #(4*4)
    ldmia r2, {r4-r11}

    // restore sp, lr
    ldr sp, [r1, #(13*4)]
    ldr lr, [r1, #(14*4)]

    bx lr

.global co_trampoline
co_trampoline:
    // r4=fn, r5=arg, r6=self
    mov r0, r5
    blx r4
    mov r0, r6
    bl  co_done
    b   .
