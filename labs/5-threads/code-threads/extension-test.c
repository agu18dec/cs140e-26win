// Extension for Lab 5: how fast can you make a loopback bit-bang protocol?
// To run, connect GPIOs 20 and 21 with a jumper wire and then "make run"
#include "test-header.h"

const int receiver_pin = 20;
const int sender_pin = 21;
const int num_bits = 1000;
const int delay_amt_us = 10;

// taken from 7-test-realtime-yield.c
static void wait_usec(unsigned n) {
    demand(n < 100000, "unlikely large delay = %dusec!\n", n);
    unsigned start = timer_get_usec();
    while (1) {
        if ((timer_get_usec() - start) >= n)
            return;
        rpi_yield();
    }
}

void receiver(void* arg) {
    // Receiver thread reads receiver_pin
    gpio_set_input(receiver_pin);

    for (int i = 0; i < num_bits; i++) {
        int bit = gpio_read(receiver_pin);
        wait_usec(delay_amt_us);
    }

    rpi_exit(0);
}

void sender(void* arg) {
    // Sender thread writes to sender_pin
    gpio_set_output(sender_pin);
    int iter = 0;

    for (int i = 0; i < num_bits; i++) {
        gpio_write(sender_pin, iter);
        iter = !iter;
        wait_usec(delay_amt_us);
    }

    rpi_exit(0);
}

void notmain(void) {
    test_init();
    rpi_fork(sender, (void*)1);
    rpi_fork(receiver, (void*)2);

    unsigned start = timer_get_usec();
    rpi_thread_start();
    unsigned end = timer_get_usec();

    trace("Completed %d bit-bangs in %d microseconds", num_bits, end-start);
    test_done();
}