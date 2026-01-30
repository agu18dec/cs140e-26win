#pragma once
#include <stdint.h>

typedef void (*co_fn_t)(uint32_t arg);

typedef struct co_th {
    uint32_t regs[17];          // regs[4..11], regs[13]=sp, regs[14]=lr used
    struct co_th *on_done;      // who to run when this coroutine returns
    volatile int done;
} co_th_t;

void co_init(co_th_t *co, co_fn_t fn, uint32_t arg, void *stack_top);
void co_set_main(co_th_t *main);
void co_set_current(co_th_t *cur);
void co_transfer(co_th_t *to);
void co_done(co_th_t *co);

// asm
void co_switch_asm(co_th_t *old, const co_th_t *new);
void co_trampoline(void);
