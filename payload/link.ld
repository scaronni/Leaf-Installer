/*
 * Linker script for the Leaf Updater payload.
 *
 * Hekate stages payloads into DRAM at EXT_PAYLOAD_ADDR (0xC0000000) but its
 * relocator stub then copies them to IRAM at PATCHED_RELOC_ENTRY
 * (0x40010000) and jumps there. We link for the IRAM run address — using
 * the DRAM staging address would leave every absolute pointer in the binary
 * dangling.
 */

ENTRY(_start)

SECTIONS {
    PROVIDE(__payload_start = LDR_LOAD_ADDR);
    . = __payload_start;

    .text : {
        *(.text._start);
        *(.text*);
    }

    .rodata : {
        *(.rodata*);
    }

    .data : {
        *(.data*);
    }

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*);
        *(COMMON);
        __bss_end = .;
    }

    . = ALIGN(16);
    __stack_bottom = .;
    . = . + 0x4000; /* 16 KB descending stack */
    __stack_top = .;

    . = ALIGN(0x10);
    __payload_end = .;
}
