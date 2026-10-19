#ifndef HOST_UART_H
#define HOST_UART_H
#include "spl.h"
void uart_puts(const char *s);
void uart_printf(const char *fmt, ...);
#endif
