/*
 * uart.h - the console.
 */
#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);

/* printf with %s %d %u %x %c and %% only; no widths, no floats. */
void uart_printf(const char *fmt, ...);

#endif /* UART_H */
