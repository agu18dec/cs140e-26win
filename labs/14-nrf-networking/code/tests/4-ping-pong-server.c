// Part 5: Ping-pong test - SERVER side (run on YOUR Pi).
// Your partner runs 4-ping-pong-client.c on their Pi.
//
// Address setup: You listen on server_addr, send to client_addr (partner's RX).
// Partner listens on client_addr, send to server_addr (your RX).
#include "nrf-test.h"

enum { ntrial = 100, timeout_usec = 1000, nbytes = 4 };

void notmain(void) {
    kmalloc_init_mb(1);

    trace("Ping-pong SERVER: listening on %x, sending to %x\n",
          server_addr, client_addr);

    nrf_t *s = server_mk_ack(server_addr, nbytes);
    nrf_dump("server config:\n", s);

    nrf_stat_start(s);

    unsigned npackets = 0, ntimeout = 0;
    uint32_t exp = 0, got = 0;

    for (unsigned i = 0; i < ntrial; i++) {
        if (i && i % 20 == 0)
            trace("server: sent %d, got %d [timeouts=%d]\n",
                  npackets, ntimeout, ntimeout);

        // Send to partner (client_addr = their RX address)
        uint32_t val = ++exp;
        nrf_send_ack(s, client_addr, &val, 4);

        // Receive partner's reply (they send to server_addr = our RX)
        int ret = nrf_read_exact_timeout(s, &got, 4, timeout_usec);
        if (ret != 4) {
            ntimeout++;
        } else if (got != exp) {
            nrf_output("server: received %u (expected %u)\n", got, exp);
        } else {
            npackets++;
        }
    }

    trace("server done: %d packets, %d timeouts\n", npackets, ntimeout);
    nrf_stat_print(s, "server");
}
