#include "rpi.h"

// trivial delay routine: <nop()> is in start.S so it doesn't
// get optimized away.  Worth removing the <nop> call and seeing
// what happens as you change optimization levels.
void delay_cycles(unsigned ticks) {
  while (ticks-- > 0)
    nop();
}

// weird panic: we just infinite loop since we don't have 
// printk in 2-gpio.
void gpio_panic(const char *msg, ...) {
    while(1);
}
