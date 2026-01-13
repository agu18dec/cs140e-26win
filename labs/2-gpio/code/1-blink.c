// part 1: uses your GPIO code to blink a single LED connected to 
// pin 20.
//   - when run should keep blinking.
//   - to restart: you have to power-cycle the pi.
#include "rpi.h"

void notmain(void) {
    enum { led = 20 };

    gpio_set_output(led);
    while(1) {
        gpio_set_on(led);
        delay_cycles(1000000);
        gpio_set_off(led);
        delay_cycles(1000000);
    }
}
