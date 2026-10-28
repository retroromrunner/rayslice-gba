ENTRY(_start)
MEMORY {
  ROM (rx) : ORIGIN = 0x08000000, LENGTH = 0x02000000
  IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 0x8000
}
SECTIONS {
  .crt0 0x08000000 : { *(.crt0) } > ROM
  .text : { *(.text) *(.text.*) } > ROM
  .rodata : { *(.rodata) *(.rodata.*) } > ROM
  .data : { __data_start = .; *(.data) *(.data.*) ; __data_end = .; } > IWRAM AT > ROM
  __data_lma = LOADADDR(.data);
  .bss : { __bss_start = .; *(.bss) *(.bss.*) *(COMMON) ; __bss_end = .; } > IWRAM
  /DISCARD/ : { *(.comment) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) }
}
