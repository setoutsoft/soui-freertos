/*
 * startup.c -- lm3s6965 (Cortex-M3) reset/vector table + C runtime bring-up.
 *
 * The FreeRTOS ARM_CM3 port manipulates the NVIC/SysTick registers directly, so
 * no CMSIS core_cm3.h is required. The three kernel handlers (SVC, PendSV,
 * SysTick) must sit at vector indices 11 / 14 / 15; this file wires them here.
 */
#include <stdint.h>

/* Linker-provided symbols. */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

/* FreeRTOS port exception handlers (defined in portable/GCC/ARM_CM3/port.c). */
void vPortSVCHandler( void );
void xPortPendSVHandler( void );
void xPortSysTickHandler( void );

/* Eager newlib heap-lock creation (defined in syscalls.c; must run before
 * the scheduler starts -- see heap_lock_early_init in syscalls.c). */
void heap_lock_early_init( void );

/* C++ dynamic initializers (newlib).  With -nostartfiles crt0 is not linked,
 * so Reset_Handler must run .init_array itself -- SOUI relies on namespace-
 * level objects (e.g. "static const SStringW KNoneFont") being constructed. */
extern void __libc_init_array( void );

/* newlib's __libc_init_array calls _init(); normally provided by crtbegin.o
 * which is excluded by -nostartfiles -- give it an empty body. */
void _init( void )
{
}

/* Application entry point (defined in main.cpp). */
int entry( void );

void Reset_Handler( void );
void Default_Handler( void ) __attribute__( ( noreturn ) );

__attribute__( ( section( ".isr_vector" ) ) )
const uint32_t vector_table[] =
{
    ( uint32_t ) &_estack,          /* 0  initial stack pointer          */
    ( uint32_t ) Reset_Handler,      /* 1  reset                          */
    ( uint32_t ) Default_Handler,    /* 2  NMI                            */
    ( uint32_t ) Default_Handler,    /* 3  hard fault                     */
    ( uint32_t ) Default_Handler,    /* 4  MPU / mem manage fault         */
    ( uint32_t ) Default_Handler,    /* 5  bus fault                      */
    ( uint32_t ) Default_Handler,    /* 6  usage fault                    */
    0, 0, 0, 0,                      /* 7..10 reserved                    */
    ( uint32_t ) vPortSVCHandler,    /* 11 SVC                            */
    ( uint32_t ) Default_Handler,    /* 12 debug monitor                  */
    0,                               /* 13 reserved                       */
    ( uint32_t ) xPortPendSVHandler, /* 14 PendSV                         */
    ( uint32_t ) xPortSysTickHandler /* 15 SysTick                        */
};

void Default_Handler( void )
{
    while ( 1 )
    {
    }
}

void Reset_Handler( void )
{
    /* Load the main stack pointer from the top-of-RAM symbol. On real silicon
     * the Cortex-M fetches MSP from vector[0] on reset; setting it explicitly
     * here keeps us correct even if the loader (e.g. some QEMU paths) did not. */
    __asm volatile ( "msr msp, %0" : : "r" ( &_estack ) );

    /* Copy the .data section from flash to SRAM. */
    uint32_t * src = &_sidata;
    uint32_t * dst = &_sdata;
    while ( dst < &_edata )
    {
        *dst++ = *src++;
    }

    /* Zero the .bss section. */
    dst = &_sbss;
    while ( dst < &_ebss )
    {
        *dst++ = 0;
    }

    /* newlib heap lock must exist before any task mallocs (heap_3). */
    heap_lock_early_init();

    /* Run C++ .init_array constructors (namespace-level globals).  Must come
     * after the heap lock is ready because several ctors malloc. */
    __libc_init_array();

    /* Call the C++/C application entry; does not return. */
    entry();

    while ( 1 )
    {
    }
}
