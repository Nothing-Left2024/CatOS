/*
 * CatOS User Program Linker Script
 * 程序链接地址 = 0 (相对于 LDT 段基址 0x100000)
 * 段顺序: .text (含 .rodata) → .data → .bss
 */
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.eh_frame_hdr)
    }
}
