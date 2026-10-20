/*
 * uart_mps2.c -- minimal polled UART0 driver for the ARM MPS2+ AN385 on QEMU.
 *
 * The AN385's UART0 is a CMSDK APB UART at 0x40004000; QEMU wires it to the
 * first -serial chardev.  Only TX is used for validation output.
 * Register layout (verified on QEMU): DATA 0x0, STATE 0x4 (TXBF = bit 0),
 * CTRL 0x8 (UARTEN = bit 0, TXEN = bit 1), BAUDDIV 0x10.
 */
#include <stdint.h>
#include "uart.h"

#define UART0_BASE   0x40004000u

#define UART_DATA    0x000u
#define UART_STATE   0x004u      /* STATE: bit0 TXBF, bit1 RXBF */
#define UART_CTRL    0x008u      /* CTRL:  bit0 UARTEN, bit1 TXEN, bit2 RXEN */
#define UART_BAUDDIV 0x010u

static volatile uint32_t * reg( uint32_t off )
{
    return ( volatile uint32_t * ) ( UART0_BASE + off );
}

void uart_init( void )
{
    *reg( UART_BAUDDIV ) = 217;             /* 25MHz / (115200 + 1) approx */
    *reg( UART_CTRL ) = ( 1u << 0 )         /* UARTEN */
                      | ( 1u << 1 );        /* TXEN */
}

void uart_putc( char c )
{
    /* Wait until the TX buffer is not full (TXBF == 0). */
    while ( *reg( UART_STATE ) & ( 1u << 0 ) )
    {
    }
    *reg( UART_DATA ) = ( uint32_t ) ( unsigned char ) c;
}

void uart_puts( const char * s )
{
    while ( *s )
    {
        if ( *s == '\n' )
        {
            uart_putc( '\r' );
        }
        uart_putc( *s++ );
    }
}
