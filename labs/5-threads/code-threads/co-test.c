#include "rpi.h"
#include "co.h"

static co_th_t main_co, a_co, b_co;

static uint32_t a_stack[1024];
static uint32_t b_stack[1024];

void fn_a(uint32_t arg) {
    for (int i = 0; i < 5; i++) {
        trace("A: arg=%d i=%d\n", arg, i);
        co_transfer(&b_co);
    }
    trace("A returning\n");
}

void fn_b(uint32_t arg) {
    for (int i = 0; i < 5; i++) {
        trace("B: arg=%d i=%d\n", arg, i);
        co_transfer(&a_co);
    }
    trace("B returning\n");
}

void notmain(void) {
    // If your co.c has these:
    co_set_main(&main_co);
    co_set_current(&main_co);

    co_init(&a_co, fn_a, 111, &a_stack[1024]);
    co_init(&b_co, fn_b, 222, &b_stack[1024]);

    // optional: define who to run when one returns
    // (otherwise co_done goes back to main immediately)
    a_co.on_done = &main_co;
    b_co.on_done = &main_co;

    trace("starting A\n");
    co_transfer(&a_co);

    trace("back in main after coroutines done\n");
}
