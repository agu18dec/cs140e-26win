// tests that your gpio_input works.
//
// Hardware config:
//   1. connect pin 8 and 9 using a jumper ("loopback").
//   2. leave your LEDs connected as in 2-blink.c
//   3. if the test succeeds they should blink in unison: either
//      both on or both off.
// 
// we do "true" embedded system debugging without any printk
// by using LEDs to show what the system is doing:
//  iterate, alternating the value of <v> \in {0,1}
//  1. set pin(20) = <v>, which sets its LED on/off (ground truth)
//  2. set pin(9)  = <v> (the loopback output)
//  3. set <x> = pin(8) (the loopback input)
//  4. set(27) = <x>, which sets its LED on/off (compare)
//
//  - if both 20 and 27's LEDs match (both on at the same time, 
//    both off at the same time) your GPIO code worked.
//  - if not: 
//      - first try the staff binary to make sure your 
//        pi is wired correctly.
//      - then debug your gpio: common is to not read-modify-write 
//        the FSEL register.
#include "rpi.h"

void notmain(void) {
    // the different pins.
    enum { 
        led0    = 20,
        led1    = 27,
        output  = 9,
        input   = 8
    };

    // configure.  can be in any order, just do before
    // use.
    gpio_set_output(led0);
    gpio_set_input(input);
    gpio_set_output(led1);
    gpio_set_output(output);

    // initial = off.
    unsigned v = 0; 

    for(int i = 0; i < 20; i++) {
        // 1. set pin(<led0>) = <v> so we have something 
        //    to compare to.
        gpio_write(led0, v);

        // 2. write <v> to the loopback, read it, then
        //    set <led1> to the value.  visually: <led1>
        //    should be the same as <led0> (either on or off).
        gpio_write(output, v);
        gpio_write(led1, gpio_read(input));

        // hold so can see visually (humans are slow)
        delay_cycles(1500000);

        // flip the value of v from 0->1 or 1->0
        // (could also do  <v> = 1-<v>)
        v = !v; 
    }
}
