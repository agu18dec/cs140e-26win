// trivial: we do a single read of pin 7.
// should be easy to debug!
#include "rpi.h"
void notmain(void) {
    output("TRACE: about to read 7: %d\n", gpio_read(7));
}
