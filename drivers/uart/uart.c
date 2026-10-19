/*
 * uart.c - UART1DM at 0xA9A00000, the console.
 *
 * Register set (drivers/serial/msm_serial.h):
 *   0x08  SR read / CSR write      0x14  ISR read / IMR write
 *   0x0c  RF read / TF write       0x1c  TFWR
 * SR_TXRDY (0x04) and SR_TXEMT (0x08) together mean the transmit FIFO has
 * room.  Writing a byte to TF pops it out the front of the line.
 */
#include "spl.h"
#include "board.h"
#include "uart.h"
#include "format.h"

#define UART_SR      0x08
#define UART_TF      0x0c
#define UART_CR      0x10
#define UART_RF      0x0c
#define UART_ISR     0x14
#define UART_TFWR    0x1c

#define SR_RXRDY     0x01
#define SR_TXRDY     0x04
#define SR_TXEMT     0x08

/* CR bits (msm_uart.h) */
#define CR_UART_EN   (1 << 0)
#define CR_RX_EN     (1 << 1)
#define CR_TX_EN     (1 << 2)

void uart_init(void)
{
    /*
     * Stale receive data and the transmitter reset, then out of reset with
     * both directions enabled.  The transmitter FIFO is 4 deep.
     */
    mmio_write32(UART1_BASE, UART_CR, 0x20 /* RESET_TX */);
    mmio_write32(UART1_BASE, UART_CR, 0x10 /* RESET_RX */);
    mmio_write32(UART1_BASE, UART_TFWR, 4);
    mmio_write32(UART1_BASE, UART_CR,
                 CR_UART_EN | CR_RX_EN | CR_TX_EN);
}

void uart_putc(char c)
{
    if (c == '\n') {
        uart_putc('\r');
    }
    while ((mmio_read32(UART1_BASE, UART_SR) & (SR_TXRDY | SR_TXEMT)) !=
           (SR_TXRDY | SR_TXEMT)) {
        /* the FIFO is always empty on this part; spin anyway */
    }
    mmio_write8(UART1_BASE, UART_TF, (u8)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

int uart_getc(void)
{
    if (!(mmio_read32(UART1_BASE, UART_SR) & SR_RXRDY)) {
        return -1;
    }
    return (int)mmio_read8(UART1_BASE, UART_RF);
}

/*
 * The console prints itself; see spl/format.c for the conversions.
 */
static void uart_emit(char c, void *arg)
{
    (void)arg;
    uart_putc(c);
}

void uart_printf(const char *fmt, ...)
{
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    spl_vformat(uart_emit, NULL, fmt, ap);
    __builtin_va_end(ap);
}
