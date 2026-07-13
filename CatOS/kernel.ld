/*======================================================================
  kernel.ld — CatOS 内核链接脚本

  作用:
    1. 将 .text 段放置在 0x30400 (内核加载地址)
    2. 按 .text → .data → .rdata → .bss 顺序排列所有段
    3. 定义 __bss_start__ / __bss_end__ 符号, 供 kernel.asm 中
       _start 清零 BSS 段使用 (objcopy 不输出 .bss, 必须运行时清零)

  编译流程:
    gcc -m32 用此脚本链接 → kernel.elf (PE)
    objcopy -O binary -j .text -j .data -j .rdata → kernel.bin (不含 BSS)
    loader 将 kernel.bin 加载到 0x30400, BSS 区域为内存垃圾
    _start 中 rep stosd 清零 [__bss_start__, __bss_end__]
======================================================================*/

ENTRY(cstart)

SECTIONS
{
    . = 0x30400;

    .text : {
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
    }

    .data : {
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
    }

    .rdata : {
        *(.rdata)
        *(.rdata.*)
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
    }

    . = ALIGN(4);
    __bss_start__ = .;
    .bss : {
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
    }
    __bss_end__ = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.drectve)
        *(.ctors)
        *(.dtors)
    }
}
