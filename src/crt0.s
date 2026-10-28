    .section ".crt0","ax"
    .global _start
_start:
    b       _rom_init
    .space  0x9C, 0              @ 0x04..0x9F boot logo area (left zeroed; playtest build for emulator use)
    .space  0x20, 0              @ 0xA0..0xBF header fields (patched by fix_header.py)
_rom_init:
    mov     r0, #0x9F            @ system mode, IRQs masked
    msr     cpsr, r0
    ldr     sp, =0x03007F00      @ user stack top in IWRAM
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    ldr     r0, =__data_lma
    ldr     r1, =__data_start
    ldr     r2, =__data_end
2:  cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     2b
    bl      main
3:  b       3b
