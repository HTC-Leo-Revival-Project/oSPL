/*
 * uart.h - the console.
 */
#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

/*
 * A character, or -1 if none has arrived.  The bootloader's menu reads a line
 * at a time through this, so it must be able to ask without waiting: the
 * prompt has to stay where it is while nobody is typing at it.
 */
int uart_getc(void);

/* printf with %s %d %u %x %c and %% only; no widths, no floats. */
void uart_printf(const char *fmt, ...);

#endif /* UART_H */
