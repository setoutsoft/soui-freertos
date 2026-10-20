#ifndef UART_H
#define UART_H

#ifdef __cplusplus
extern "C" {
#endif

void uart_init( void );
void uart_putc( char c );
void uart_puts( const char * s );

#ifdef __cplusplus
}
#endif

#endif /* UART_H */
