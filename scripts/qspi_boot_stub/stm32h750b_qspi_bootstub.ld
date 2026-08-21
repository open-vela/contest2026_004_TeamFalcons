MEMORY
{
  flash (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  ram   (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
}

_estack = ORIGIN(ram) + LENGTH(ram);

OUTPUT_ARCH(arm)
ENTRY(Reset_Handler)

SECTIONS
{
  .vectors ORIGIN(flash) :
  {
    KEEP(*(.vectors))
  } > flash

  .text :
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
    _etext = .;
  } > flash

  .data :
  {
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } > ram AT > flash

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > ram

  /DISCARD/ :
  {
    *(.comment)
    *(.ARM.attributes)
    *(.note*)
    *(.eh_frame*)
  }
}
