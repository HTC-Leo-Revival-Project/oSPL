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

#define UART_SR      0x08
#define UART_TF      0x0c
#define UART_CR      0x10
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

/* ---- the little printf ---------------------------------------------- */

/*
 * %s %c %d %u %x %X %% with an optional "0" and a width of 1..8 digits,
 * which is everything the bootloader has ever needed to print.
 */
static void put_hex(u32 v, int digits, int upper)
{
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *hex = upper ? up : lo;

    for (int i = digits - 1; i >= 0; i--) {
        uart_putc(hex[(v >> (i * 4)) & 0xf]);
    }
}

static void put_dec(s32 v)
{
    char buf[12];
    int n = 0;
    u32 u = (v < 0) ? (u32)-v : (u32)v;

    if (v < 0) {
        uart_putc('-');
    }
    do {
        buf[n++] = (char)('0' + (u % 10));
        u /= 10;
    } while (u);
    while (n) {
        uart_putc(buf[--n]);
    }
}

/* Skip the flags and width a conversion carries, returning the width. */
static int scan_width(const char **fmt)
{
    const char *f = *fmt;
    int width = 0;

    while (*f == '0' || (*f >= '1' && *f <= '9')) {
        width = width * 10 + (*f - '0');
        f++;
    }
    *fmt = f;
    return width;
}

void uart_printf(const char *fmt, ...)
{
    __builtin_va_list ap;

    __builtin_va_start(ap, fmt);
    while (*fmt) {
        int width;

        if (*fmt != '%') {
            uart_putc(*fmt++);
            continue;
        }
        fmt++;
        width = scan_width(&fmt);             /* the 0 flag, then the width */
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);

            uart_puts(s ? s : "(null)");
            break;
        }
        case 'c':
            uart_putc((char)__builtin_va_arg(ap, int));
            break;
        case 'd':
            put_dec(__builtin_va_arg(ap, s32));
            break;
        case 'u':
            put_dec((s32)__builtin_va_arg(ap, u32));
            break;
        case 'x':
            put_hex(__builtin_va_arg(ap, u32), width ? width : 8, 0);
            break;
        case 'X':
            put_hex(__builtin_va_arg(ap, u32), width ? width : 8, 1);
            break;
        case '%':
            uart_putc('%');
            break;
        case '\0':
            __builtin_va_end(ap);
            return;
        default:
            uart_putc('%');
            uart_putc(*fmt);
            break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}
