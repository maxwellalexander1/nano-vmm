ENTRY(_start)

SECTIONS
{
    . = 0x40000000;
    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }
    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(4096);
    . += 0x10000;
    __stack_top = .;

}