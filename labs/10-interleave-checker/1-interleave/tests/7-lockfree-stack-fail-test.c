// Broken lock-free stack: should fail.
//
// Bug: a naive linked-list push reads head, sets node->next,
// then updates head.  These are separate instructions, so
// another thread can push between the read and the update,
// causing a "lost update" --- one node becomes unreachable.
//
// Concrete failure:
//   1. A reads head (NULL), stores it in nodeA.next
//   2. --- checker switches to B ---
//   3. B reads head (still NULL), sets nodeB.next=NULL,
//      sets head=&nodeB.  B finishes.
//   4. A resumes: sets head=&nodeA (with nodeA.next=NULL).
//   Result: head -> nodeA -> NULL.  nodeB is lost.
//   Only 1 node reachable, not 2.
#include "check-interleave.h"

typedef struct node {
    int val;
    struct node *next;
} node_t;

static node_t *head;
static node_t nodeA, nodeB;

// naive push: read head, link, update --- NOT atomic.
static void stack_A(checker_t *c) {
    nodeA.val = 1;
    nodeA.next = head;      // step 1: read head
    head = &nodeA;          // step 2: update head
}

static int stack_B(checker_t *c) {
    nodeB.val = 2;
    nodeB.next = head;      // step 1: read head
    head = &nodeB;          // step 2: update head
    return 1;               // always runs
}

static void stack_init(checker_t *c) {
    head = 0;
    nodeA.val = 0; nodeA.next = 0;
    nodeB.val = 0; nodeB.next = 0;
}

// after both push, 2 nodes should be reachable from head.
static int stack_check(checker_t *c) {
    int count = 0;
    for(node_t *n = head; n; n = n->next)
        count++;
    return count == 2;
}

checker_t stack_mk_checker(void) {
    return (struct checker) { 
        .state = 0,
        .A = stack_A,
        .B = stack_B,
        .init = stack_init,
        .check = stack_check
    };
}

void notmain(void) {
    enable_cache();

    struct checker c = stack_mk_checker();
    if(check(&c))
        panic("check should have failed!\n");
    else 
        exit_success("lock-free stack failed as expected, ntrials=[%d], nerrors=[%d]\n", 
                c.ntrials, c.nerrors);
}
