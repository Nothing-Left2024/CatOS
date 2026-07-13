
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                               load.inc
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                                                     Forrest Yu, 2005
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


BaseOfLoader		equ	 09000h	; LOADER.BIN 被加载到的位置 ----  段地址
OffsetOfLoader		equ	  0100h	; LOADER.BIN 被加载到的位置 ---- 偏移地址

BaseOfLoaderPhyAddr	equ	BaseOfLoader * 10h	; LOADER.BIN 被加载到的位置 ---- 物理地址 (= BaseOfLoader * 10h)


BaseOfKernelFile	equ	 08000h	; KERNEL.BIN 被加载到的位置 ----  段地址
OffsetOfKernelFile	equ	     0h	; KERNEL.BIN 被加载到的位置 ---- 偏移地址

BaseOfKernelFilePhyAddr	equ	BaseOfKernelFile * 10h
KernelEntryPointPhyAddr	equ	030400h	; 注意：1，须与 MAKEFILE 中的 -Ttext 的值一致!!
					;       2，若给的是物理地址而不是偏移，则 -Ttext 的值为 0x400400，此值也应该为 0x400400。

PageDirBase		equ	200000h	; ҳĿ¼��ʼ��ַ:		2M
PART_START		equ	2048		; FAT32 partition start LBA (1MB aligned, standard MBR layout)

PageTblBase		equ	201000h	; ҳ����ʼ��ַ:			2M + 4K

