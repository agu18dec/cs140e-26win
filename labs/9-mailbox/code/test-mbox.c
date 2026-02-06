#include "rpi.h"
#include "mbox.h"
#include "cycle-count.h"

uint32_t rpi_temp_get(void);

unsigned cyc_per_sec(void) {
    unsigned t0 = timer_get_usec();
    unsigned c0 = cycle_cnt_read();

    while((unsigned)(timer_get_usec() - t0) < 100000)
        ;

    unsigned t1 = timer_get_usec();
    unsigned c1 = cycle_cnt_read();

    unsigned dt = t1 - t0;
    unsigned dc = c1 - c0;

    return (unsigned)((uint64_t)dc * 1000000ull / dt);
}

static void clk_dump(const char *name, uint32_t clk_id) {
    uint32_t cur  = rpi_clock_curhz_get(clk_id);
    uint32_t real = rpi_clock_realhz_get(clk_id);
    uint32_t mn   = rpi_clock_minhz_get(clk_id);
    uint32_t mx   = rpi_clock_maxhz_get(clk_id);

    output("%s clk=%d: cur=%d real=%d min=%d max=%d\n",
           name, clk_id, cur, real, mn, mx);
}

static uint32_t mem_rw_10k_usec(void) {
    static volatile uint32_t a[10000];

    unsigned s = timer_get_usec();

    for(unsigned i = 0; i < 10000; i++)
        a[i] = i * 2654435761u;

    volatile uint32_t sum = 0;
    for(unsigned i = 0; i < 10000; i++)
        sum += a[i];

    unsigned tot = timer_get_usec() - s;

    return tot;
}

static void mem_test_report(const char *label, unsigned cps) {
    uint32_t us = mem_rw_10k_usec();
    uint32_t ns_per_op = (us * 1000u) / 20000u;

    output("%s: mem rw 10k = %d usec  (%d ns/op)\n", label, us, ns_per_op);
    output("%s: cycles/sec = %d\n", label, cps);
}



void notmain(void) {
    output("mailbox serial number = %llx\n", rpi_get_serialnum());
    output("mailbox revision number = %x\n", rpi_get_revision());
    output("mailbox model number = %x\n", rpi_get_model());

    uint32_t size = rpi_get_memsize();
    output("mailbox physical mem: size=%d (%dMB)\n", size, size/(1024*1024));

    unsigned x = rpi_temp_get();
    unsigned C = x / 1000;
    unsigned F = (C * 9) / 5 + 32;
    output("mailbox temp = %x, C=%d F=%d\n", x, C, F);

    output("\n=== BASELINE ===\n");
    clk_dump("ARM",   MBOX_CLK_CPU);
    clk_dump("CORE",  MBOX_CLK_CORE);
    clk_dump("SDRAM", MBOX_CLK_SDRAM);

    unsigned cps0 = cyc_per_sec();
    mem_test_report("baseline", cps0);

    uint32_t arm_max = rpi_clock_maxhz_get(MBOX_CLK_CPU);
    output("\nARM max reported = %d\n", arm_max);
    output("Setting ARM to %d...\n", arm_max);
    uint32_t set_ret = rpi_clock_hz_set(MBOX_CLK_CPU, arm_max);
    output("Set returned = %d\n", set_ret);

    output("\n=== AFTER OVERCLOCK ===\n");
    clk_dump("ARM", MBOX_CLK_CPU);

    unsigned cps1 = cyc_per_sec();
    mem_test_report("after", cps1);

    uint32_t us0 = mem_rw_10k_usec();
    uint32_t us1 = mem_rw_10k_usec();
    output("sanity: two runs after overclock: %d usec, %d usec\n", us0, us1);
}
