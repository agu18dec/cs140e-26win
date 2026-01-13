// part 2: uses your GPIO code to blink two LEDs where if one is 
// off, the other is on.
//  - if they don't alternate you probably didn't read-modify-write
//    when setting the GPIO pins 20,27 to output.
// Hardware:
//  - connect pin(20) to an external led (same as in 1-blink.c)
//  - pin(27) controls the built-in bright green LED on the Parthiv 
//    board.  
#include "rpi.h"

void notmain(void) {
    enum { led1 = 20, led2 = 27 };

    // Config pins for output.  
    //  - NOTE: Worth removing and seeing what happens.
    gpio_set_output(led1);
    gpio_set_output(led2);

    // blink forever: have to power-cycle for next program.
    //  - NOTE: worth messing with different delays to see what
    //    happens.
    while(1) {
        // will blink oppositely.
        gpio_set_on(led1);
        gpio_set_off(led2);
        delay_cycles(3000000);  // Q: as gets smaller?
        gpio_set_off(led1);
        gpio_set_on(led2);
        delay_cycles(3000000);
    }
}
