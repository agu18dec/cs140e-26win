// Non-atomic multi-word update: should fail.
//
// Bug: A updates two variables (x,y) that should always be
// seen as a consistent pair.  Without atomicity, B can read
// them between the two stores and observe a "torn" state
// where x and y are from different updates.
//
// Concrete failure:
//   init: x=0, y=0.
//   1. A stores x=1.
//   2. --- checker switches to B ---
//   3. B reads x (gets 1) and y (gets 0), saves the snapshot.
//      B finishes.
//   4. A resumes: stores y=1.
//   Result: B observed (x=1, y=0) which violates the invariant
//   that x and y should always be equal.
#include "check-interleave.h"

static volatile int x, y;
// B's snapshot of the two values.
static int b_x, b_y;

// A sets both to 1 (simulating a coordinated update).
static void torn_A(checker_t *c) {
    x = 1;
    y = 1;
}

// B takes a snapshot of both values.
static int torn_B(checker_t *c) {
    b_x = x;
    b_y = y;
    return 1;   // always runs
}

static void torn_init(checker_t *c) {
    x = 0; y = 0;
    b_x = 0; b_y = 0;
}

// invariant: B's snapshot should be consistent --- either
// (0,0) before A runs, or (1,1) after A runs, never (1,0).
static int torn_check(checker_t *c) {
    return b_x == b_y;
}

checker_t torn_mk_checker(void) {
    return (struct checker) { 
        .state = 0,
        .A = torn_A,
        .B = torn_B,
        .init = torn_init,
        .check = torn_check
    };
}

void notmain(void) {
    enable_cache();

    struct checker c = torn_mk_checker();
    if(check(&c))
        panic("check should have failed!\n");
    else 
        exit_success("torn write detected as expected, ntrials=[%d], nerrors=[%d]\n", 
                c.ntrials, c.nerrors);
}
