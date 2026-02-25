#include "rpi.h"
#include "rpi-interrupts.h"
#include "asm-helpers.h"
#include "bit-support.h"
#include "breakpoint.h"

cp_asm_get(cp15_ifsr, p15, 0, c5, c0, 1)
cp_asm(cp14_dscr, p14, 0, c0, c1, 0)   // DSCR
// CP14: breakpoint regs (match uses #1, mismatch uses #0)
cp_asm(cp14_bvr0, p14, 0, c0, c0, 4)
cp_asm(cp14_bcr0, p14, 0, c0, c0, 5)
cp_asm(cp14_bvr1, p14, 0, c0, c1, 4)
cp_asm(cp14_bcr1, p14, 0, c0, c1, 5)

static inline int ifsr_debug_event(uint32_t ifsr) {m
    if(bit_is_on(ifsr, 10))
        return 0;
    return bits_eq(ifsr, 0, 3, 0b0010);   // debug event
}

static inline void enable_monitor_debug(void) {
    uint32_t dscr = cp14_dscr_get();
    dscr = bit_set(dscr, 15);
    dscr = bit_clr(dscr, 14);
    cp14_dscr_set(dscr);
}


// make sure that cp14 (DSCR) is enabled (p13-7)
void brkpt_match_init(void) {
    enable_monitor_debug();
}

// set match on <addr>
//
// for simplicity: for matching we use breakpoint 1 
// (bcr1 p13-17 and bvr1 p13-16) so dont't conflict 
// with single-stepping.   
void brkpt_match_set(uint32_t addr) {
   enable_monitor_debug();
   cp14_bvr1_set(addr);
   uint32_t bcr = 0;
   bcr = bits_set(bcr, 20, 22, 0b000);
   bcr = bits_set(bcr, 14, 15, 0b00);
   bcr = bits_set(bcr, 5, 8, 0b1111);
   bcr = bits_set(bcr, 1, 2, 0b11);
   bcr = bit_set(bcr, 0);
   cp14_bcr1_set(bcr);
}

// turn off match faults (clear bcr1)
void brkpt_match_stop(void) {
    uint32_t bcr = cp14_bcr1_get();
    bcr = bit_clr(bcr, 0);
    cp14_bcr1_set(bcr);
}

// return the match addr (bvr1)
uint32_t brkpt_match_get(void) {
    if(bit_is_off(cp14_bcr1_get(), 0)) {return 0;}
    return cp14_bvr1_get();
}


// set mismatch on <addr>
//
// for simplicity: for matching we use breakpoint 0. 
// so set bcr0 and bvr0.
void brkpt_mismatch_set(uint32_t addr) {
   enable_monitor_debug();
   cp14_bvr0_set(addr);
   uint32_t bcr = 0;
   // bits[22:21] = 10 --> mismatch
   bcr = bits_set(bcr, 20, 22, 0b100);
   bcr = bits_set(bcr, 14, 15, 0b00);
   bcr = bits_set(bcr, 5, 8, 0b1111);
   bcr = bits_set(bcr, 1, 2, 0b11);
   bcr = bit_set(bcr, 0);
   cp14_bcr0_set(bcr);
}

// this will mismatch on the first instruction at user level.
void brkpt_mismatch_start(void) {
    // 1. check DSCR: if not enabled, enable it.
    // 2. set brkpt_mismatch_set(0)
    enable_monitor_debug();
    brkpt_mismatch_set(0);
}

// turn off mismatching: clear bcr0
void brkpt_mismatch_stop(void) {
    uint32_t bcr = cp14_bcr0_get();
    bcr = bit_clr(bcr, 0);
    cp14_bcr0_set(bcr);
}

// was this a breakpoint fault? (either mismatch or match)
// check IFSR bits (p 3-66) to see it was a debug fault.
// check DSCR bits (13-11) to see if it was a breakpoint
int brkpt_fault_p(void) {
    uint32_t ifsr = cp15_ifsr_get();
    if(!ifsr_debug_event(ifsr))
        return 0;

    uint32_t dscr = cp14_dscr_get();
    return bits_eq(dscr, 2, 5, 0b0001);
}
