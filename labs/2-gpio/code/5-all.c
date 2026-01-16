// part 5: blinks all three LEDs in unison (all on, all off).
// - pin 20: external LED (active high)
// - pin 27: on-board green LED (active high)
// - pin 47: ACT LED (active low)
#include "rpi.h"

void notmain(void) {
    enum { led1 = 20, led2 = 27, led_act = 47 };

    gpio_set_output(led1);
    gpio_set_output(led2);
    gpio_set_output(led_act);

    while(1) {
        gpio_set_on(led1);
        gpio_set_on(led2);
        gpio_set_off(led_act);
        delay_cycles(1000000);

        gpio_set_off(led1);
        gpio_set_off(led2);
        gpio_set_on(led_act); 
        delay_cycles(1000000);
    }
}