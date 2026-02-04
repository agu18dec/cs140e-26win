#include "rpi.h"
#include "co.h"

static co_th_t *co_main;
static co_th_t *co_current;

void co_set_main(co_th_t *main) { co_main = main; }
void co_set_current(co_th_t *cur) { co_current = cur; }

void co_init(co_th_t *co, co_fn_t fn, uint32_t arg, void *stack_top) {
    for(int i = 0; i < 17; i++) co->regs[i] = 0;
    co->done = 0;
    co->on_done = 0;

    co->regs[4]  = (uint32_t)(uintptr_t)fn;            // r4 = fn
    co->regs[5]  = (uint32_t)arg;                      // r5 = arg
    co->regs[6]  = (uint32_t)(uintptr_t)co;            // r6 = self
    co->regs[13] = (uint32_t)(uintptr_t)stack_top;     // sp
    co->regs[14] = (uint32_t)(uintptr_t)co_trampoline; // lr = trampoline
}

void co_transfer(co_th_t *to) {
    if(!to)
        panic("co_transfer: to is null\n");
    if(!co_current)
        panic("co_transfer: co_current is null (did you call co_set_current?)\n");

    co_th_t *from = co_current;

    //trace("co_transfer: %p -> %p\n", from, to);

    co_current = to;
    co_switch_asm(from, to);
}

void co_done(co_th_t *co) {
    //trace("co_done: co=%p on_done=%p main=%p\n", co, co ? co->on_done : 0, co_main);

    if(!co)
        panic("co_done: co is null\n");

    co->done = 1;

    if(co->on_done) {
        co_transfer(co->on_done);
        not_reached();
    }

    if(!co_main)
        panic("co_done: no main coroutine set\n");

    co_transfer(co_main);
    not_reached();
}
