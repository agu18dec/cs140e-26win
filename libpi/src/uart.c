// simple mini-uart driver: implement every routine 
// with a <todo>.
//
// NOTE: 
//  - from broadcom: if you are writing to different 
//    devices you MUST use a dev_barrier().   
//  - its not always clear when X and Y are different
//    devices.
//  - pay attenton for errata!   there are some serious
//    ones here.  if you have a week free you'd learn 
//    alot figuring out what these are (esp hard given
//    the lack of printing) but you'd learn alot, and
//    definitely have new-found respect to the pioneers
//    that worked out the bcm eratta.
//
// historically a problem with writing UART code for
// this class (and for human history) is that when 
// things go wrong you can't print since doing so uses
// uart.  thus, debugging is very old school circa
// 1950s, which modern brains arne't built for out of
// the box.   you have two options:
//  1. think hard.  we recommend this.
//  2. use the included bit-banging sw uart routine
//     to print.   this makes things much easier.
//     but if you do make sure you delete it at the 
//     end, otherwise your GPIO will be in a bad state.
//
// in either case, in the next part of the lab you'll
// implement bit-banged UART yourself.
#include "rpi.h"

// change "1" to "0" if you want to comment out
// the entire block.
#if 1
//*****************************************************
// We provide a bit-banged version of UART for debugging
// your UART code.  delete when done!
//
// NOTE: if you call <emergency_printk>, it takes 
// over the UART GPIO pins (14,15). Thus, your UART 
// GPIO initialization will get destroyed.  Do not 
// forget!   

// header in <libpi/include/sw-uart.h>
#include "sw-uart.h"
static sw_uart_t sw_uart;

// a sw-uart putc implementation.
static int sw_uart_putc(int chr) {
    sw_uart_put8(&sw_uart,chr);
    return chr;
}

#ifndef AUX_BASE
#   define AUX_BASE            0x20215000u
#   define AUX_ENABLES         (AUX_BASE + 0x04u)

#   define AUX_MU_IO_REG       (AUX_BASE + 0x40u)
#   define AUX_MU_IER_REG      (AUX_BASE + 0x44u)
#   define AUX_MU_IIR_REG      (AUX_BASE + 0x48u)
#   define AUX_MU_LCR_REG      (AUX_BASE + 0x4Cu)
#   define AUX_MU_MCR_REG      (AUX_BASE + 0x50u)
#   define AUX_MU_LSR_REG      (AUX_BASE + 0x54u)
#   define AUX_MU_CNTL_REG     (AUX_BASE + 0x60u)
#   define AUX_MU_BAUD_REG     (AUX_BASE + 0x68u)
#endif

enum {
    AUXENB_MINIUART = 1u << 0,

    // LSR bits
    LSR_DATA_READY  = 1u << 0,   // RX has at least 1 byte
    LSR_TX_CAN_ACCEPT = 1u << 5, // TX FIFO has space for at least 1 byte
    LSR_TX_EMPTY    = 1u << 6,   // TX empty AND transmitter idle

    // CNTL bits
    CNTL_RX_EN      = 1u << 0,
    CNTL_TX_EN      = 1u << 1,
};

// call this routine to print stuff. 
//
// note the function pointer hack: after you call it 
// once can call the regular printk etc.
__attribute__((noreturn)) 
static void emergency_printk(const char *fmt, ...)  {
    // we forcibly initialize in case the 
    // GPIO got reset. this will setup 
    // gpio 14,15 for sw-uart.
    sw_uart = sw_uart_default();

    // all libpi output is via a <putc>
    // function pointer: this installs ours
    // instead of the default
    rpi_putchar_set(sw_uart_putc);

    // do print
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);

    // at this point UART is all messed up b/c we took it over
    // so just reboot.   we've set the putchar so this will work
    clean_reboot();
}

#undef todo
#define todo(msg) do {                          \
    emergency_printk("%s:%d:%s\nDONE!!!\n",     \
            __FUNCTION__,__LINE__,msg);         \
} while(0)

// END of the bit bang code.
#endif

void uart_div(uint32_t baud_reg) {
    dev_barrier();
    PUT32(AUX_MU_BAUD_REG, baud_reg); // pg 19 for baud rate register
    dev_barrier();
}

//*****************************************************
// the rest you should implement.

// called first to setup uart to 8n1 115200  baud,
// no interrupts.
//  - you will need memory barriers, use <dev_barrier()>
//
//  later: should add an init that takes a baud rate.
void uart_init(void) {
    dev_barrier();
    gpio_set_function(14, GPIO_FUNC_ALT5);
    gpio_set_function(15, GPIO_FUNC_ALT5);
    dev_barrier();

    // rmw to enable the AUX device
    uint32_t enable = GET32(AUX_ENABLES); // pg 8 auxenb
    dev_barrier();
    enable |= AUXENB_MINIUART;
    PUT32(AUX_ENABLES, enable);
    dev_barrier();

    //disable tx/rx
    PUT32(AUX_MU_CNTL_REG, 0); // pg 17
    dev_barrier();
    PUT32(AUX_MU_IER_REG, 0); // pg 12 for interrupts disable
    dev_barrier();
    PUT32(AUX_MU_IIR_REG, 0b110); // need to clear fifos bits 1 and 2
    dev_barrier();
    PUT32(AUX_MU_LCR_REG, 0b11); // pg 14 for data size (8 bits) and 8n1 (no parity, 1 stop bit)
    dev_barrier();
    uart_div(270); // 115200 baud
    dev_barrier();
    PUT32(AUX_MU_CNTL_REG, CNTL_RX_EN | CNTL_TX_EN); // pg 17 for tx/rx enable
    dev_barrier();
    // dev_barrier();
    // NOTE: for cross-checking: make sure write UART 
    // addresses in order
}

// disable the uart: make sure all bytes have been
// 
void uart_disable(void) {
    // TODO: implement this!
    dev_barrier();

    uart_flush_tx();

    // Disable TX/RX and interrupts; clear FIFOs to leave clean.
    dev_barrier();
    PUT32(AUX_MU_CNTL_REG, 0);
    dev_barrier();
    PUT32(AUX_MU_IER_REG, 0);
    dev_barrier();
    PUT32(AUX_MU_IIR_REG, 0xC6); // need to clear fifos bits 1 and 2
    dev_barrier();
}

// returns one byte from the RX (input) hardware
// FIFO.  if FIFO is empty, blocks until there is 
// at least one byte.
int uart_get8(void) {
    dev_barrier();
    // we want to poll the queue to see if there is data available
    while(!uart_has_data()) {}
    // data is available now so we can read from FIFo
    uint32_t data = GET32(AUX_MU_IO_REG); // pg 11 for getting data from the RX FIFO
    dev_barrier();
    return (int)(data & 0xFFu); // get lower 8 bits
}

// returns 1 if the hardware TX (output) FIFO has room
// for at least one byte.  returns 0 otherwise.
int uart_can_put8(void) {
    dev_barrier();
    int can_put = (GET32(AUX_MU_LSR_REG) & LSR_TX_CAN_ACCEPT) ? 1 : 0; // pg 15 for LSR register bit 5 checking if TX FIFO has space for at least 1 byte
    dev_barrier();
    return can_put;
}

// put one byte on the TX FIFO, if necessary, waits
// until the FIFO has space.
int uart_put8(uint8_t c) {
    dev_barrier();
    while(!uart_can_put8()) {}
    PUT32(AUX_MU_IO_REG, (uint32_t)c); // pg 11 for putting data into the TX FIFO
    dev_barrier();
    return 1;
}

// returns:
//  - 1 if at least one byte on the hardware RX FIFO.
//  - 0 otherwise
int uart_has_data(void) {
    dev_barrier();
    int has_data = (GET32(AUX_MU_LSR_REG) & LSR_DATA_READY) ? 1 : 0; // pg 15 for LSR register bit 0 checking if data has at least 1 byte
    dev_barrier();
    return has_data;
}

// returns:
//  -1 if no data on the RX FIFO.
//  otherwise reads a byte and returns it.
int uart_get8_async(void) { 
    if(!uart_has_data())
        return -1;
    return uart_get8();
}

// returns:
//  - 1 if TX FIFO empty AND idle.
//  - 0 if not empty.
int uart_tx_is_empty(void) {
    dev_barrier();
    int is_empty = (GET32(AUX_MU_LSR_REG) & LSR_TX_EMPTY) ? 1 : 0; // pg 15 for LSR register bit 6 checking if TX FIFO is empty and idle
    dev_barrier();
    return is_empty;
}

// return only when the TX FIFO is empty AND the
// TX transmitter is idle.  
//
// used when rebooting or turning off the UART to
// make sure that any output has been completely 
// transmitted.  otherwise can get truncated 
// if reboot happens before all bytes have been
// received.
void uart_flush_tx(void) {
    while(!uart_tx_is_empty())
        rpi_wait();
}
