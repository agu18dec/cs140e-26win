// very dumb, simple interface to wrap up watchpoints better.
// only handles a single watchpoint.
#include "rpi.h"
#include "watchpoint.h"
#include "bit-support.h"
#include "asm-helpers.h"

// keep track of what we are watching.
static uint32_t watch_addr;

//   cp15 DFSR: Data Fault Status Register
cp_asm_get(cp15_dfsr, p15, 0, c5, c0, 0)
//   cp15 FAR:  Fault Address Register (data abort)
cp_asm_get(cp15_far,  p15, 0, c6, c0, 0)
cp_asm(cp14_dscr, p14, 0, c0, c1, 0)      // DSCR
cp_asm(cp14_wfar, p14, 0, c0, c6, 0)      // WFAR
cp_asm_set(cp14_wvr0, p14, 0, c0, c0, 6)  // WVR0
cp_asm(cp14_wcr0,  p14, 0, c0, c0, 7)     // WCR0

static inline int dfsr_debug_event(uint32_t dfsr) {
    if(bit_is_on(dfsr, 10))
        return 0;
    // bit[10]==0, the status code is in bits[3:0].
    return bits_eq(dfsr, 0, 3, 0b0010);
}


static inline int dfsr_write(uint32_t dfsr) {
    return bit_is_on(dfsr, 11);
}


static inline void enable_monitor_debug(void) {
    uint32_t dscr = cp14_dscr_get();
    dscr = bit_set(dscr, 15);   // enable monitor debug
    dscr = bit_clr(dscr, 14);   // select monitor debug-mode (per lab note)
    cp14_dscr_set(dscr);        // macro includes prefetch_flush()
}

// was it a watchpoint fault?
//  1. use dfsr 3-64  to make sure it was a debug event.
//  2. and dscr 13-11: to make sure it was a watchpoint
int watchpt_fault_p(void) {
    // check if abort came from a debug event and if debug event was a watchpoint (data abort)
    uint32_t dfsr = cp15_dfsr_get();
    if(!dfsr_debug_event(dfsr))
        return 0;

    // check watchpoint vs breakpoint
    uint32_t dscr = cp14_dscr_get();
    if(bits_eq(dscr, 2, 5, 0b0010))
        return 1;
    return 0;
}

// is it a load fault?
//  - use dfsr 3-64
int watchpt_load_fault_p(void) {
    if(!watchpt_fault_p())
        return 0;
    uint32_t dfsr = cp15_dfsr_get();
    return !dfsr_write(dfsr);
}

// get the pc of the fault.
//   - p13-34: use <wfar> (see 3-12) to get the fault pc 
// important:
//   - pay attention to the comment on 13-12 to see how to adjust!
uint32_t watchpt_fault_pc(void) {
    //  WFAR = faulting instruction address + 0x8 so pc is WFAR - 0x8
    return cp14_wfar_get() - 0x8;
}

// get the data address that caused the fault.
// use <far> 3-68 to get the fault addr.
uint32_t watchpt_fault_addr(void) {
    return cp15_far_get();
}

// set a watch-point on <addr>: 
//  1. enable cp14 if not enabled.  
//     - MAKE SURE TO DO THIS FIRST.
//  2. set wcr0 (13-21), wvr0 (13-20)
//     - don't rmw -- just set it directly.
// Important: 
//  - make sure you handle subword accesses! 
int watchpt_on(uint32_t addr) {
    watch_addr = addr;
    // enable monitor debug
   enable_monitor_debug();
   uint32_t wcr = 0;
   wcr = bits_set(wcr, 14, 15, 0b00);
   wcr = bits_set(wcr, 5, 8, 0b1111); // BAS
   wcr = bits_set(wcr, 3, 4, 0b11);    // loads+stores
   wcr = bits_set(wcr, 1, 2, 0b11);     // supervisor access
   wcr = bit_set(wcr, 0);       // enable

   cp14_wcr0_set(wcr);    // includes prefetch_flush()
   cp14_wvr0_set(addr);
   return 0;

}

// turn off watchpoint:
//   - check that <addr> is what we were watching.
//   - clear wcr0
int watchpt_off(uint32_t addr) {
    if(addr != watch_addr)
        panic("disabling invalid watchpoint %x, tracking %x\n", 
            addr, watch_addr);
    cp14_wcr0_set(0);  // disable
    return 0;
}
