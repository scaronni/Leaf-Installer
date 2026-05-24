/*
 * Leaf Updater payload startup.
 *
 * Hekate hands us control at __payload_start with no stack set up and DRAM
 * already initialised. We just need to point SP at a sane location, zero
 * .bss, then call main().
 */

.section .text._start
.arm
.globl _start
.type _start, %function
_start:
    /* Stack just above the payload so we have ~16 KB of headroom. The exact
       address is set at link time via __stack_top. */
    LDR SP, =__stack_top

    /* Zero .bss. */
    LDR R0, =__bss_start
    LDR R1, =__bss_end
    MOV R2, #0
1:  CMP R0, R1
    BEQ 2f
    STR R2, [R0], #4
    B   1b
2:

    BL  main
    /* main() never returns under normal operation, but if it does, halt. */
3:  B   3b
