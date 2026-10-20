/* lm3s6965evb memory layout (QEMU): flash @ 0x00000000, SRAM @ 0x20000000. */
MEMORY
{
    FLASH ( rx  ) : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   ( rwx ) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN( RAM ) + LENGTH( RAM );

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN( 256 );
        KEEP( *( .isr_vector ) )
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        *(.glue_7*)
        *(.glue_7t*)
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    _sidata = LOADADDR( .data );

    .data :
    {
        . = ALIGN( 4 );
        _sdata = .;
        *(.data*)
        . = ALIGN( 4 );
        _edata = .;
    } > RAM AT > FLASH

    .bss :
    {
        . = ALIGN( 4 );
        _sbss = .;
        *(.bss*)
        *( COMMON )
        . = ALIGN( 4 );
        _ebss = .;
    } > RAM

    . = ALIGN( 8 );
    _end = .;
    _heap_limit = ORIGIN( RAM ) + LENGTH( RAM );
}
