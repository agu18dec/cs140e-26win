### Implement the routines in `gpio.c` to control GPIO pins.

Files:
- `gpio.c`: the only file you modify. Implement the GPIO functions.
- `rpi.h`: function prototypes and definitions.
- `start.S`: assembly routines to read/write device memory (`GET32`, `PUT32`).
- `libpi.c`: minimal support code (`delay_cycles`, `panic`).

Tests (run in order):

Part 1 (output):
- `1-blink.c`: blink a single LED on pin 20.
- `2-blink.c`: blink two LEDs oppositely (pins 20 and 27).
  Note: its easy to make a mistake in how you read the broadcom
  document!

Part 2 (input):
- `3-loopback.c`: test GPIO input by connecting pins 8 and 9 with a jumper.
