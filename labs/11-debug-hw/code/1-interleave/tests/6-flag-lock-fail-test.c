// Broken flag-based "lock": should fail.
//
// Bug: using a plain flag variable as a lock is broken because
// the check (while(flag)) and the set (flag=1) are two separate
// instructions.  The checker can interleave B between them so
// both A and B enter the "critical section" concurrently.
//
// On ARM, cnt++ compiles to three instructions:
//      ldr r0, [cnt]       @ load
//      add r0, r0, #1      @ increment
//      str r0, [cnt]       @ store
//
// If A loads cnt (gets 0), then B runs entirely (cnt becomes 1),
// then A stores its stale value+1 (stores 1), final cnt=1 not 2.
#include "check-interleave.h"

static volatile int flag = 0;
static volatile int cnt = 0;

static void flag_A(checker_t *c) {
    while(flag);    // spin until flag == 0
    flag = 1;       // "acquire" -- NOT atomic with the check!
    cnt++;
    flag = 0;       // "release"
}

static int flag_B(checker_t *c) {
    if(flag) return 0;  // can't enter, "lock" held
    flag = 1;
    cnt++;
    flag = 0;
    return 1;
}

static void flag_init(checker_t *c) { flag = 0; cnt = 0; }
static int  flag_check(checker_t *c) { return cnt == 2; }

checker_t flag_mk_checker(void) {
    return (struct checker) { 
        .state = 0,
        .A = flag_A,
        .B = flag_B,
        .init = flag_init,
        .check = flag_check
    };
}

void notmain(void) {
    enable_cache();

    struct checker c = flag_mk_checker();
    if(check(&c))
        panic("check should have failed!\n");
    else 
        exit_success("flag lock failed as expected, ntrials=[%d], nerrors=[%d]\n", 
                c.ntrials, c.nerrors);
}
