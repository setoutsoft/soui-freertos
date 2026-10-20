/*
 * uart.c -- minimal polled UART0 driver for the TI/luminary lm3s6965 on QEMU.
 *
 * Only TX is used. QEMU's -serial connects UART0 to the host chardev, so this
 * is enough to emit validation output. Clock gating and pin mux writes are
 * harmless and match the real silicon; QEMU ignores what it does not model.
 */
#include <stdint.h>
#include "uart.h"

#define UART0_BASE   0x4000C000u
#define RCGCUART     0x400FE618u   /* UART run-mode clock gate */
#define RCGCGPIO     0x400FE608u   /* GPIO run-mode clock gate */
#define GPIOA_BASE   0x40004000u

static volatile uint32_t * reg( uint32_t base, uint32_t off )
{
    return ( volatile uint32_t * ) ( base + off );
}

void uart_init( void )
{
    /* Enable UART0 and GPIOA clocks. */
    *reg( RCGCUART, 0 ) |= ( 1u << 0 );
    *reg( RCGCGPIO, 0 ) |= ( 1u << 0 );

    /* Configure PA0 (U0RX) and PA1 (U0TX) as UART alternate function. */
    volatile uint32_t * gpioa = reg( GPIOA_BASE, 0 );
    gpioa[ 0x420u / 4u ] |= 0x3u;   /* AFSEL  for PA0, PA1 */
    gpioa[ 0x51Cu / 4u ] |= 0x3u;   /* DEN    for PA0, PA1 */

    /* Disable the UART while configuring. */
    *reg( UART0_BASE, 0x030u ) = 0;
    /* Baud rate is irrelevant to QEMU; write nominal divisors. */
    *reg( UART0_BASE, 0x024u ) = 8;    /* IBRD */
    *reg( UART0_BASE, 0x028u ) = 44;   /* FBRD */
    *reg( UART0_BASE, 0x02Cu ) = 0x60; /* LCRH: 8-bit, FEN */
    /* Enable UART, TX and RX. */
    *reg( UART0_BASE, 0x030u ) = ( 1u << 0 ) | ( 1u << 8 ) | ( 1u << 9 );
}

void uart_putc( char c )
{
    /* Wait until the TX FIFO is not full. */
    while ( *reg( UART0_BASE, 0x018u ) & ( 1u << 5 ) )
    {
    }
    *reg( UART0_BASE, 0x000u ) = ( uint32_t ) ( unsigned char ) c;
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
