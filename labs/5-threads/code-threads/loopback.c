// connect GPIO 21 (sender) to GPIO 20 (receiver) with a jumper wire
#include "test-header.h"
#include "co.h"

static const int receiver_pin = 20;
static const int sender_pin   = 21;

static const int num_bits     = 1000;
static const int new_delay_us     = 10;

static co_th_t main_co, sender_co, receiver_co;
static uint32_t sender_stack[2048];
static uint32_t receiver_stack[2048];

static volatile int expected_bit = 0;
static volatile unsigned errors = 0;

static inline int time_before(unsigned a, unsigned b) {
    return (int)(a - b) < 0;
}

static inline void wait_until(unsigned deadline) {
    while(time_before(timer_get_usec(), deadline))
        ;   // no yields: coroutine handoff is the yield
}

static void receiver_fn(uint32_t arg) {
    (void)arg;
    gpio_set_input(receiver_pin);

    for(int i = 0; i < num_bits; i++) {
        unsigned deadline = timer_get_usec() + new_delay_us;
        wait_until(deadline);

        int b = gpio_read(receiver_pin);
        if(b != expected_bit)
            errors++;

        co_transfer(&sender_co);
    }

    trace("receiver: errors=%d / %d\n", errors, num_bits);
    return;
}

static void sender_fn(uint32_t arg) {
    (void)arg;
    gpio_set_output(sender_pin);

    int bit = 0;
    for(int i = 0; i < num_bits; i++) {
        expected_bit = bit;
        gpio_write(sender_pin, bit);
        bit ^= 1;

        co_transfer(&receiver_co);
    }

    trace("sender: done\n");
    return; // -> trampoline -> co_done -> receiver (because on_done)
}

void notmain(void) {
    test_init();

    co_set_main(&main_co);
    co_set_current(&main_co);

    co_init(&sender_co, sender_fn, 0, &sender_stack[2048]);
    co_init(&receiver_co, receiver_fn, 0, &receiver_stack[2048]);

    // finish chain:
    // sender returns -> co_done -> resume receiver (so it can return)
    // receiver returns -> co_done -> back to main
    sender_co.on_done = &receiver_co;
    receiver_co.on_done = &main_co;

    unsigned start = timer_get_usec();
    trace("starting sender\n");
    co_transfer(&sender_co);   // runs until both coroutines finish and return to main
    unsigned end = timer_get_usec();

    unsigned dt = end - start;
    unsigned us_per_bit_x100 = (dt * 100) / num_bits;   // scaled by 100
    
    trace("Completed %d bits in %d usec (%d usec/bit x100)\n", num_bits, dt, us_per_bit_x100);

    test_done();
}
