// write code in C to check if stack grows up or down.
// suggestion:
//   - local variables are on the stack.
//   - so take the address of a local, call another routine, and
//     compare addresses of one of its local variables to the 
//     original.
//   - make sure you check the machine code make sure the
//     compiler didn't optimize the calls away!
//
//   - bonus: also use inline assembly or a gcc intrinsic to get the
//     stack pointer and compare.
#include "rpi.h"

static volatile uintptr_t last_helper_addr;

static void helper(void) {
    volatile unsigned x = 0;
    last_helper_addr = (uintptr_t)&x;
}

static int stack_grows_down(void) {
    volatile unsigned a = 0;
    uintptr_t a_addr = (uintptr_t)&a;

    helper();
    uintptr_t x_addr = last_helper_addr;

    return x_addr < a_addr;
}

void notmain(void) {
    if(stack_grows_down())
        trace("stack grows down\n");
    else
        trace("stack grows up\n");
}
