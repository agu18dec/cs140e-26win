// trivial: we do a single read of pin 20.
// should be easy to debug!
#include "rpi.h"
void notmain(void) {
    output("TRACE: about to read 20=%d\n", gpio_read(20));
}
