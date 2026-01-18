#include "rpi.h"

void reboot(void) {
    const int PM_RSTC = 0x2010001c;
    const int PM_WDOG = 0x20100024;
    const int PM_PASSWORD = 0x5a000000;
    const int PM_RSTC_WRCFG_FULL_RESET = 0x00000020;
    int i;
    for(i = 0; i < 100000; i++)
         nop();
    PUT32(PM_WDOG, PM_PASSWORD | 1);
    PUT32(PM_RSTC, PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET);
    while(1);
}


void notmain(void) {
    enum { led = 20 };
    gpio_set_output(led);
    for(int i = 0; i < 5; i++) {
        gpio_set_on(led);
        delay_cycles(1000000);
        gpio_set_off(led);
        delay_cycles(1000000);
    }
    reboot();
}
