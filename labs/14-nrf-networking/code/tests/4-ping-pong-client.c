// Part 5: Ping-pong test - CLIENT side (run on PARTNER's Pi).
// You run 4-ping-pong-server.c on your Pi.
//
// Address setup: Partner listens on client_addr, sends to server_addr (your RX).
// You listen on server_addr, send to client_addr (partner's RX).
#include "nrf-test.h"

enum { ntrial = 100, timeout_usec = 1000, nbytes = 4 };

void notmain(void) {
    kmalloc_init_mb(1);

    trace("Ping-pong CLIENT: listening on %x, sending to %x\n",
          client_addr, server_addr);

    nrf_t *c = client_mk_ack(client_addr, nbytes);
    nrf_dump("client config:\n", c);

    nrf_stat_start(c);

    unsigned npackets = 0, ntimeout = 0;
    uint32_t got = 0;

    for (unsigned i = 0; i < ntrial; i++) {
        if (i && i % 20 == 0)
            trace("client: got %d [timeouts=%d]\n", npackets, ntimeout);

        // Receive from partner (they send to client_addr = our RX)
        int ret = nrf_read_exact_timeout(c, &got, 4, timeout_usec);
        if (ret != 4) {
            ntimeout++;
        } else {
            npackets++;
            // Echo back to partner (server_addr = their RX address)
            nrf_send_ack(c, server_addr, &got, 4);
        }
    }

    trace("client done: %d packets, %d timeouts\n", npackets, ntimeout);
    nrf_stat_print(c, "client");
}
