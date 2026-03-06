#include "nrf-test.h"
#include "nrf-hw-support.h"

enum {
    ntrial = 200,
    nbytes = 4,
    timeout_usec = 200000
};

#ifndef MY_RX_ADDR
# define MY_RX_ADDR   0xa1a1a1
#endif

#ifndef PEER_RX_ADDR
# define PEER_RX_ADDR 0xb2b2b2
#endif

#ifndef I_AM_INITIATOR
# define I_AM_INITIATOR 1
#endif

#ifndef USE_CLIENT_NIC
# define USE_CLIENT_NIC 0
#endif

static uint32_t mk_reply(uint32_t x) {
    return x ^ 0xffffffffu;
}

static int wait_for_expected_reply(nrf_t *nic, uint32_t expected, uint32_t *got) {
    uint32_t start = timer_get_usec();
    while(timer_get_usec() - start < timeout_usec) {
        int ret = nrf_read_exact_timeout(nic, got, nbytes, 3000);
        if(ret != 4)
            continue;
        if(*got == expected)
            return 1;
    }
    return 0;
}

static nrf_t *partner_nic_mk(void) {
    nrf_conf_t c = USE_CLIENT_NIC ? client_conf(nbytes) : server_conf(nbytes);
    return nrf_init_acked(c, MY_RX_ADDR);
}

void notmain(void) {
    kmalloc_init_mb(1);

    trace("partner ping-pong: my_rx=%x peer_rx=%x initiator=%d use_client_nic=%d\n",
        MY_RX_ADDR, PEER_RX_ADDR, I_AM_INITIATOR, USE_CLIENT_NIC);

    nrf_t *nic = partner_nic_mk();

    // Don't manually override RX_ADDR_P0 here.
    // nrf_send_ack() will set TX_ADDR and RX_ADDR_P0 to PEER_RX_ADDR per send.

    nrf_dump("partner nic config", nic);
    nrf_stat_start(nic);

    if(I_AM_INITIATOR)
        delay_ms(700);

    unsigned ok = 0, timeout = 0;

    for(unsigned i = 0; i < ntrial; i++) {
        uint32_t req = 0x140e0000u | i;
        uint32_t rsp = 0;

        if(I_AM_INITIATOR) {
            printk("initiator sending req=%x\n", req);

            if(nrf_send_ack(nic, PEER_RX_ADDR, &req, nbytes) != 4)
                panic("initiator send failed: req=%x\n", req);

            if(!wait_for_expected_reply(nic, mk_reply(req), &rsp)) {
                printk("initiator timeout waiting for rsp to req=%x\n", req);
                timeout++;
                continue;
            }

            printk("initiator received rsp=%x\n", rsp);
            ok++;
        } else {
            printk("responder waiting for req\n");

            uint32_t got = 0;
            if(nrf_read_exact_timeout(nic, &got, nbytes, timeout_usec) != 4) {
                printk("responder timeout waiting for req\n");
                timeout++;
                continue;
            }

            printk("responder received req=%x\n", got);

            rsp = mk_reply(got);
            printk("responder sending rsp=%x\n", rsp);

            if(nrf_send_ack(nic, PEER_RX_ADDR, &rsp, nbytes) != 4)
                panic("responder send failed: rsp=%x\n", rsp);

            ok++;
        }
    }

    trace("partner ping-pong done: ok=%d timeout=%d of %d rounds\n",
        ok, timeout, ntrial);
    nrf_stat_print(nic, "partner ping-pong stats");
}