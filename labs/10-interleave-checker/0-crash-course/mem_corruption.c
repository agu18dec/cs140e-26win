// we modify the <0-nop-example.c> example, stripping out comments
// so its more succinct and wrapping up the diffent routines into
// a <single_step_fn> routine that will run a function with an
// argument in single step mode.
#include "rpi.h"
#include "breakpoint.h"
#include "full-except.h"
#include "cpsr-util.h"
#include "single-step-syscalls.h"
#include "fast-hash32.h"

#undef trace

static int verbose_p = 1;
#define trace(msg...) do { if(verbose_p) trace_nofn(msg); } while(0)

void single_step_verbose(int v_p) {
    verbose_p = v_p;
}

static const char *ss_cur_fn = "none";
static volatile unsigned n_inst;

static regs_t scheduler_regs;
static regs_t exit_regs;

static regs_t prev_regs;
static int prev_valid;
static uint32_t run_hash;

static uint32_t *pc_hist;
static uint32_t hist_base;
static uint32_t hist_n;

// Part 2: watch for corruption of a “should never change” location.
static volatile uint32_t *watch_addr;
static uint32_t watch_expected;
static int watch_on;

static void hist_init(uint32_t base, uint32_t n_slots) {
    hist_base = base;
    hist_n = n_slots;
    pc_hist = kmalloc(hist_n * sizeof *pc_hist);
    memset(pc_hist, 0, hist_n * sizeof *pc_hist);
}

static inline void hist_inc(uint32_t pc) {
    if(pc < hist_base) return;
    uint32_t idx = (pc - hist_base) >> 2;
    if(idx < hist_n)
        pc_hist[idx]++;
}

static void hist_print_top(unsigned K) {
    for(unsigned k = 0; k < K; k++) {
        uint32_t best_i = ~0u, best = 0;
        for(unsigned i = 0; i < hist_n; i++) {
            if(pc_hist[i] > best) {
                best = pc_hist[i];
                best_i = i;
            }
        }
        if(best == 0) break;

        uint32_t pc = hist_base + (best_i << 2);
        trace("TOP %d: pc=%x count=%d inst=%x\n", k, pc, best, GET32(pc));
        pc_hist[best_i] = 0;
    }
}

static void single_step_handler(regs_t *regs) {
    if(!brkpt_fault_p())
        panic("impossible: should get no other faults\n");
    assert(mode_get(regs->regs[REGS_CPSR]) == USER_MODE);

    uint32_t pc = regs->regs[REGS_PC];
    n_inst++;

    // Part 2: detect first time watched location changes.
    if(watch_on && watch_addr) {
        uint32_t v = *watch_addr;
        if(v != watch_expected) {
            panic("MEM CORRUPTION: *%x expected=%x got=%x at pc=%x inst=%x\n",
                  (uint32_t)watch_addr, watch_expected, v, pc, GET32(pc));
        }
    }

    trace("fault:\t%X:\t%X  @ %d\n", pc, GET32(pc), n_inst);

    if(prev_valid) {
        for(int i = 0; i < 17; i++) {
            uint32_t oldv = prev_regs.regs[i];
            uint32_t newv = regs->regs[i];
            if(oldv != newv)
                trace("chg r%d: 0x%x -> 0x%x\n", i, oldv, newv);
        }
    }
    prev_regs = *regs;
    prev_valid = 1;

    run_hash = fast_hash_inc32(regs->regs, 17 * sizeof(uint32_t), run_hash);
    hist_inc(pc);

    while(!uart_can_put8())
        ;

    brkpt_mismatch_set(pc);
    switchto(regs);
}

static int syscall_handler(regs_t *r) {
    assert(mode_get(r->regs[REGS_CPSR]) == USER_MODE);

    uint32_t sysno = r->regs[REGS_R0];
    uint32_t arg0  = r->regs[REGS_R1];
    uint32_t pc    = r->regs[REGS_PC];

    switch(sysno) {
    case SS_SYS_EXIT:
        trace("%s: exited with syscall=%d: arg0=%d, pc=%x\n",
            ss_cur_fn, sysno, arg0, pc);
        exit_regs = *r;
        switchto(&scheduler_regs);
        not_reached();
    case SS_SYS_PUTC:
        output("%c", r->regs[REGS_R1]);
        break;
    default:
        panic("illegal system call number: %d\n", sysno);
    }
    switchto(r);
    not_reached();
}

static inline uint32_t cpsr_to_user(void) {
    uint32_t cpsr = cpsr_get();

    cpsr = bits_clr(cpsr, 0, 4) | USER_MODE;
    cpsr = bits_clr(cpsr, 28, 31);
    cpsr = bit_clr(cpsr, 7);

    assert(mode_get(cpsr) == USER_MODE);
    return cpsr;
}

static inline regs_t
regs_init(void (*fp)(), uint32_t arg, void *stack, uint32_t nbytes) {
    assert(fp);
    uint32_t initial_pc = (uint32_t)fp;
    uint32_t cpsr = cpsr_to_user();

    uint32_t sp = 0;
    if(stack) {
        demand(nbytes > 4096, stack seems small);
        sp = (uint32_t)(stack + nbytes);
    }

    void exit_trampoline(void);
    uint32_t exit_tramp = (uint32_t)exit_trampoline;

    return (regs_t) {
        .regs[REGS_PC]   = initial_pc,
        .regs[REGS_R0]   = arg,
        .regs[REGS_SP]   = sp,
        .regs[REGS_CPSR] = cpsr,
        .regs[REGS_LR]   = (uint32_t)exit_tramp,
    };
}

void single_step_on(void) {
    static int init_p = 0;

    if(!init_p) {
        init_p = 1;
        full_except_install(0);
        full_except_set_prefetch(single_step_handler);
        full_except_set_syscall(syscall_handler);
    }
    brkpt_mismatch_start();
    brkpt_mismatch_set(0);
}

void single_step_off(void) {
    brkpt_mismatch_stop();
}

static regs_t single_step_fn(
    const char *fn_name,
    void (*fp)(),
    uint32_t arg,
    void *stack,
    uint32_t nbytes)
{
    regs_t regs = regs_init(fp, arg, stack, nbytes);
    ss_cur_fn = fn_name;

    n_inst = 0;
    prev_valid = 0;
    run_hash = 0;
    memset(&exit_regs, 0, sizeof exit_regs);

    trace("PRE: about to single step <%s>\n", fn_name);
    single_step_on();
    switchto_cswitch(&scheduler_regs, &regs);
    single_step_off();
    trace("POST: done single-stepping %s\n", fn_name);

    trace("HASH(%s)=0x%x\n", fn_name, run_hash);
    return exit_regs;
}

__attribute__((noinline))
void loop_fn(void) {
    volatile unsigned s = 0;
    for(unsigned i = 0; i < 100; i++)
        s += i;
}

// Part 2: deliberately corrupt the watched address.
__attribute__((noinline))
void smash_watch_addr(void) {
    for(unsigned i = 0; i < 50; i++)
        asm volatile("nop");
    *watch_addr = 0x12345678;
}

void notmain(void) {
    kmalloc_init_mb(1);

    enum { stack_size = 64 * 1024 };
    void *stack = kmalloc(stack_size);

    hist_init(0x8000, 8192);

    trace("******************<loop_fn>******************************\n");
    single_step_verbose(0);
    watch_on = 0;
    single_step_fn("loop_fn", loop_fn, 0, stack, stack_size);
    single_step_verbose(1);

    trace("****************** top PCs ******************************\n");
    hist_print_top(12);

    trace("******************<part2: smash_watch_addr>******************************\n");
    watch_addr = kmalloc(sizeof(uint32_t));
    watch_expected = 0xdeadbeef;
    *watch_addr = watch_expected;
    watch_on = 1;

    single_step_verbose(0);
    single_step_fn("smash_watch_addr", smash_watch_addr, 0, stack, stack_size);

    // If you get here, your checker didn't detect the write.
    panic("unexpected: corruption checker did not fire\n");
}
