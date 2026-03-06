// Ping-pong SERVER side
#include "nrf-test.h"
#include "nrf-hw-support.h"

enum { ntrial = 100, timeout_usec = 5000, nbytes = 4 };

static int net_get32(nrf_t *nic, uint32_t *out) {
    int ret = nrf_read_exact_timeout(nic, out, 4, timeout_usec);
    if(ret != 4) {
        debug("server receive failed: ret=%d\n", ret);
        return 0;
    }
    return 1;
}

static void net_put32(nrf_t *nic, uint32_t txaddr, uint32_t x) {
    int ret = nrf_send_ack(nic, txaddr, &x, 4);
    if(ret != 4)
        panic("server send ret=%d, expected 4\n", ret);
}

void notmain(void) {
    kmalloc_init_mb(1);

    trace("Ping-pong SERVER\n");
    trace("listening on %x, sending to %x\n", server_addr, client_addr);

    nrf_t *s = server_mk_ack(server_addr, nbytes);
    nrf_dump("server config:\n", s);

    nrf_stat_start(s);

    unsigned npackets = 0, ntimeout = 0;
    uint32_t exp = 0, got = 0;

    for (unsigned i = 0; i < ntrial; i++) {

        if (i && i % 20 == 0)
            trace("server progress: success=%d timeouts=%d\n",
                  npackets, ntimeout);

        uint32_t val = ++exp;

        // send packet
        net_put32(s, client_addr, val);

        // small gap helps radio turnaround
        delay_us(200);

        // wait for echo
        int ret = net_get32(s, &got);
        if (!ret) {
            ntimeout++;
            continue;
        }

        if (got != exp) {
            nrf_output("server: received %u (expected %u)\n", got, exp);
            continue;
        }

        npackets++;
    }

    trace("SERVER DONE: success=%d timeouts=%d\n", npackets, ntimeout);

    nrf_stat_print(s, "server");
}