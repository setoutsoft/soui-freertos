/*
 * mps2-an385 memory layout (QEMU, Cortex-M3):
 *   ZBT SSRAM1 @ 0x00000000, 4MB  -- code + vectors (writable on QEMU)
 *   SRAM       @ 0x20000000, 4MB  -- data / bss / heap
 * (FreeRTOS official QEMU demo target; 4MB is needed because the whole
 *  swinx module + cairo/pixman software rasterizer links into this image.)
 */
MEMORY
{
    FLASH ( rx  ) : ORIGIN = 0x00000000, LENGTH = 8M
    RAM   ( rwx ) : ORIGIN = 0x20000000, LENGTH = 4M
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

    /* C++ dynamic initializers: SOUI keeps namespace-level objects such as
     * "static const SStringW KNoneFont = L"none"" whose constructors are
     * registered in .init_array.  __libc_init_array() (called from startup)
     * walks these tables; without them the objects stay zero-filled and the
     * first SWindow ctor HardFaults inside SStringW::InitFromIString. */
    .preinit_array :
    {
        PROVIDE_HIDDEN( __preinit_array_start = . );
        KEEP( *( .preinit_array ) )
        PROVIDE_HIDDEN( __preinit_array_end = . );
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN( __init_array_start = . );
        KEEP( *( SORT_BY_INIT_PRIORITY( .init_array.* ) ) )
        KEEP( *( .init_array ) )
        PROVIDE_HIDDEN( __init_array_end = . );
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN( __fini_array_start = . );
        KEEP( *( SORT_BY_INIT_PRIORITY( .fini_array.* ) ) )
        KEEP( *( .fini_array ) )
        PROVIDE_HIDDEN( __fini_array_end = . );
    } > FLASH

    .data :
    {
        . = ALIGN( 4 );
        _sdata = .;
        *(.data*)
        . = ALIGN( 4 );
        _edata = .;
    } > RAM AT > FLASH

    /* .bss MUST get its own PT_LOAD with LMA in RAM, not merged into the
     * .data segment: QEMU's ELF loader commits the whole segment to
     * p_paddr + p_memsz, and with .bss merged the FLASH-side extent
     * (LMA(.data) + data + bss) overflows the 4MB ssram1 region once text
     * grows past ~3.1MB -- the loader then refuses the blob entirely and
     * boots into zeroed code.  AT> RAM forces the separate segment. */
    .bss ( NOLOAD ) :
    {
        . = ALIGN( 4 );
        _sbss = .;
        *(.bss*)
        *( COMMON )
        . = ALIGN( 4 );
        _ebss = .;
    } > RAM AT > RAM

    . = ALIGN( 8 );
    _end = .;
    _heap_limit = ORIGIN( RAM ) + LENGTH( RAM );
}
