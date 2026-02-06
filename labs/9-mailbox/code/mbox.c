#include "rpi.h"
#include "mbox.h"

// dump out the entire messaage.  useful for debug.
void msg_dump(const char *msg, volatile uint32_t *u, unsigned nwords) {
    printk("%s\n", msg);
    for(int i = 0; i < nwords; i++)
        output("u[%d]=%x\n", i,u[i]);
}

static inline void mbox_ok_or_die(volatile uint32_t *msg) {
    if(msg[1] != 0x80000000)
        panic("invalid response: got %x\n", msg[1]);
    if((msg[4] & 0x80000000u) == 0)
        panic("tag missing response bit: msg[4]=%x\n", msg[4]);
}

/*
  This is given.

  Get board serial
    Tag: 0x00010004
    Request: Length: 0
    Response: Length: 8
    Value: u64: board serial
*/
uint64_t rpi_get_serialnum(void) {
    // 16-byte aligned 32-bit array
    volatile uint32_t msg[8] __attribute__((aligned(16)));

    // make sure aligned
    assert((unsigned)msg%16 == 0);

    msg[0] = 8*4;         // total size in bytes.
    msg[1] = 0;           // sender: always 0.
    msg[2] = 0x00010004;  // serial tag
    msg[3] = 8;           // total bytes avail for reply
    msg[4] = 0;           // request code [0].
    msg[5] = 0;           // space for 1st word of reply 
    msg[6] = 0;           // space for 2nd word of reply 
    msg[7] = 0;   // end tag

    // send and receive message
    mbox_send(MBOX_CH, msg);

#if 0
    // if you want to debug.
    output("got:\n");
    for(int i = 0; i < 8; i++)
        output("msg[%d]=%x\n", i, msg[i]);
#endif

    // should have value for success: 1<<31
    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    // high bit should be set and reply size
    assert(msg[4] == ((1<<31) | 8));

    // for me the upper 32 bits were never non-zero.  
    // not sure if always true?
    assert(msg[6] == 0);
    return msg[5];
}

uint32_t rpi_get_memsize(void) {
    volatile uint32_t msg[8] __attribute__((aligned(16)));
    assert(((unsigned)msg % 16) == 0);

    msg[0] = 8 * 4;
    msg[1] = 0;

    msg[2] = 0x00010005; // Get ARM memory
    msg[3] = 8;       
    msg[4] = 0;          
    msg[5] = 0; 
    msg[6] = 0;          

    msg[7] = 0;    
    mbox_send(MBOX_CH, msg);      

    if(msg[1] != 0x80000000)
		panic("invalid response: got %x\n", msg[1]);

    assert(msg[4] == ((1u<<31) | 8u));
    return msg[6];
}


uint32_t rpi_get_model(void) {
    volatile uint32_t msg[7] __attribute__((aligned(16)));
    assert(((unsigned)msg % 16) == 0);

    msg[0] = 7 * 4;
    msg[1] = 0;

    msg[2] = 0x00010001;
    msg[3] = 4;
    msg[4] = 0;

    msg[5] = 0;
    msg[6] = 0;

    mbox_send(MBOX_CH, msg);

    msg_dump("rpi_get_model: after mbox_send", msg, 7);

    if(msg[1] != 0x80000000)
        panic("invalid response: got %x\n", msg[1]);

    assert(msg[4] == ((1u<<31) | 4u));
    return msg[5];
}


// https://www.raspberrypi-spy.co.uk/2012/09/checking-your-raspberry-pi-board-version/
uint32_t rpi_get_revision(void) {
    volatile uint32_t msg[7] __attribute__((aligned(16)));
    assert(((unsigned)msg % 16) == 0);

    msg[0] = 7 * 4;
    msg[1] = 0;

    msg[2] = 0x00010002; // Get board revision
    msg[3] = 4;          // one u32
    msg[4] = 0;          

    msg[5] = 0;         
    msg[6] = 0;          

    mbox_send(MBOX_CH, msg);

    if(msg[1] != 0x80000000)
        panic("invalid response: got %x\n", msg[1]);

    assert(msg[4] == ((1u<<31) | 4u));
    return msg[5];
}

uint32_t rpi_temp_get(void) {
    volatile uint32_t msg[8] __attribute__((aligned(16)));
    assert(((unsigned)msg % 16) == 0);

    msg[0] = 8 * 4;
    msg[1] = 0;

    msg[2] = 0x00030006; // Get temperature
    msg[3] = 8;         
    msg[4] = 0;          

    msg[5] = 0;
    msg[6] = 0;          
    msg[7] = 0;       

    mbox_send(MBOX_CH, msg);

    if(msg[1] != 0x80000000)
        panic("invalid response: got %x\n", msg[1]);
    
    assert(msg[4] == ((1u<<31) | 8u));
    return msg[6];
}


static uint32_t mbox_call_u32_u32(uint32_t tag, uint32_t a, uint32_t *out_a) {
    volatile uint32_t msg[8] __attribute__((aligned(16)));
    assert(((unsigned)msg % 16) == 0);

    msg[0] = 8 * 4;
    msg[1] = 0;

    msg[2] = tag;
    msg[3] = 8;
    msg[4] = 0;      // request

    msg[5] = a;      // input
    msg[6] = 0;      // output
    msg[7] = 0;      // end tag

    mbox_send(MBOX_CH, msg);
    mbox_ok_or_die(msg);

    // msg[4] low bits are returned value length
    uint32_t len = msg[4] & 0x7fffffff;
    if(len < 8)
        panic("tag %x returned short len=%d (msg[4]=%x)\n", tag, len, msg[4]);

    if(out_a) *out_a = msg[5];   // firmware echoes id
    return msg[6];
}

static uint32_t mbox_call_u32_u32_u32(uint32_t tag, uint32_t a, uint32_t b, uint32_t c, uint32_t *out_a) {
    volatile uint32_t msg[9] __attribute__((aligned(16)));
    assert(((unsigned)msg % 16) == 0);

    msg[0] = 9 * 4;
    msg[1] = 0;

    msg[2] = tag;
    msg[3] = 12;
    msg[4] = 0;

    msg[5] = a;
    msg[6] = b;
    msg[7] = c;
    msg[8] = 0;

    mbox_send(MBOX_CH, msg);
    mbox_ok_or_die(msg);

    uint32_t len = msg[4] & 0x7fffffff;
    if(len < 8) 
        panic("tag %x returned short len=%d (msg[4]=%x)\n", tag, len, msg[4]);

    if(out_a) *out_a = msg[5];
    return msg[6];
}


uint32_t rpi_clock_curhz_get(uint32_t clock) {
    uint32_t echoed = 0;
    uint32_t hz = mbox_call_u32_u32(0x00030002, clock, &echoed);
    return hz;
}

uint32_t rpi_clock_realhz_get(uint32_t clock) {
    uint32_t echoed = 0;
    uint32_t hz = mbox_call_u32_u32(0x00030047, clock, &echoed);
    return hz;
}

uint32_t rpi_clock_maxhz_get(uint32_t clock) {
    uint32_t echoed = 0;
    uint32_t hz = mbox_call_u32_u32(0x00030004, clock, &echoed);
    return hz;
}


uint32_t rpi_clock_minhz_get(uint32_t clock) {
    uint32_t echoed = 0;
    uint32_t hz = mbox_call_u32_u32(0x00030007, clock, &echoed);
    return hz;
}

uint32_t rpi_clock_hz_set(uint32_t clock, uint32_t hz) {
    uint32_t echoed = 0;
    // Set clock rate: (id, hz, skip_turbo). skip_turbo=0 enables turbo bundle.
    uint32_t new_hz = mbox_call_u32_u32_u32(0x00038002, clock, hz, 0, &echoed);
    return new_hz;
}