// engler, cs140 put your gpio-int implementations in here.
#include "rpi.h"

// in libpi/include: has useful enums.
#include "rpi-interrupts.h"

enum { GPIO_BASE = 0x20200000 };

enum {
    GPEDS0 = GPIO_BASE + 0x40,  // p96
    GPREN0 = GPIO_BASE + 0x4C,  // p97
    GPFEN0 = GPIO_BASE + 0x58,  // p98
};


static inline uint32_t rd32(uint32_t addr) {
    dev_barrier();
    uint32_t v = GET32(addr);
    dev_barrier();
    return v;
}


// returns 1 if there is currently a GPIO_INT0 interrupt, 
// 0 otherwise.
//
// note: we can only get interrupts for <GPIO_INT0> since the
// (the other pins are inaccessible for external devices).
int gpio_has_interrupt(void) {
    uint32_t bank_2_pending =  rd32(IRQ_pending_2);
    return (bank_2_pending >> (GPIO_INT0 - 32)) & 1u; // get lowest bit
}

// p97 set to detect rising edge (0->1) on <pin>.
// as the broadcom doc states, it  detects by sampling based on the clock.
// it looks for "011" (low, hi, hi) to suppress noise.  i.e., its triggered only
// *after* a 1 reading has been sampled twice, so there will be delay.
// if you want lower latency, you should us async rising edge (p99)
//
// also have to enable GPIO interrupts at all in <IRQ_Enable_2>
void gpio_int_rising_edge(unsigned pin) {
    if(pin>=32)
        return;
    dev_barrier();
    or32((volatile void*)GPREN0, 1u << pin);
    dev_barrier();
    // direct write, not rmw
    PUT32(IRQ_Enable_2, 1u << (GPIO_INT0 - 32)); // need to tell IRQ controller to enable this interrupt
    dev_barrier();
}

// p98: detect falling edge (1->0).  sampled using the system clock.  
// similarly to rising edge detection, it suppresses noise by looking for
// "100" --- i.e., is triggered after two readings of "0" and so the 
// interrupt is delayed two clock cycles.   if you want  lower latency,
// you should use async falling edge. (p99)
//
// also have to enable GPIO interrupts at all in <IRQ_Enable_2>
void gpio_int_falling_edge(unsigned pin) {
    if(pin>=32)
        return;
    dev_barrier();
    or32((volatile void *)GPFEN0, 1u << pin);
    dev_barrier();
    // direct write, not rmw
    PUT32(IRQ_Enable_2, 1u << (GPIO_INT0 - 32)); // need to tell IRQ controller to enable this interrupt
    dev_barrier();
}

// p96: a 1<<pin is set in EVENT_DETECT if <pin> triggered an interrupt.
// if you configure multiple events to lead to interrupts, you will have to 
// read the pin to determine which caused it.
int gpio_event_detected(unsigned pin) {
    if(pin>=32)
        return 0;
    uint32_t v = rd32(GPEDS0);
    return (v >> pin) & 1u;
}

// p96: have to write a 1 to the pin to clear the event.
void gpio_event_clear(unsigned pin) {
    if(pin>=32)
        return;
    dev_barrier();
    PUT32(GPEDS0, 1u << pin); // clear the event by writing a 1 to the pin
    dev_barrier();
}
