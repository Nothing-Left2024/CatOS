; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                               hdboot.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
; FAT32 VBR 引导代码 (superfloppy 方式)
;
; 偏移布局:
;   0~2   : JMP short + NOP (跳到偏移 90)
;   3~10  : OEM Name
;   11~89 : BPB (由 fat_format 填充)
;   90+   : 引导代码
;   440~443: Kernel 起始 LBA (由 installer 写入, loader 读取)
;   444~445: Kernel 扇区数   (由 installer 写入, loader 读取)
;   510~511: 0xAA55
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

org	07c00h
bits	16

%include	"load.inc"

; FAT32 VBR 头
	jmp	short LABEL_START
	nop

; BPB 占位符 (偏移 3~89, 共 87 字节)
	times	87	db	0

; ===== 引导代码 (偏移 90) =====
LABEL_START:
	jmp	0000h:LABEL_NORMALIZED

LABEL_NORMALIZED:
	mov	ax, 0000h
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, 07c00h

	cmp	dl, 80h
	jae	.dl_ok
	mov	dl, 80h
.dl_ok:
	mov	[drv_num], dl

	mov	dl, [drv_num]
	xor	ah, ah
	int	13h
	jc	.read_error

	; ===== 逐扇区读取 loader.bin: 扇区 2~33 (32 扇区) → 0x9000:0x100 =====
	mov	bx, OffsetOfLoader
	mov	ax, BaseOfLoader
	mov	es, ax
	mov	si, 2
	mov	cx, 32
.read_loop:
	mov	word [dap_loader + 8], si
	mov	word [dap_loader + 10], 0
	mov	word [dap_loader + 2], 1
	mov	[dap_loader + 4], bx
	mov	[dap_loader + 6], es

	push	ds
	push	si
	push	ax
	push	dx

	mov	ax, 0
	mov	ds, ax
	mov	dl, [drv_num]
	mov	si, dap_loader
	mov	ah, 42h
	int	13h

	pop	dx
	pop	ax
	pop	si
	pop	ds

	jc	.read_error

	inc	si
	add	bx, 512
	test	bx, bx
	jnz	.next_sector
	push	ax
	mov	ax, es
	add	ax, 1000h
	mov	es, ax
	pop	ax
.next_sector:
	dec	cx
	jnz	.read_loop

	mov	dl, [drv_num]
	jmp	BaseOfLoader:OffsetOfLoader

.read_error:
.halt:
	hlt
	jmp	.halt

; ===== DAP: 读取 loader (运行时设置字段) =====
dap_loader:
	db	10h
	db	0
	dw	1
	dw	0
	dw	BaseOfLoader
	dd	0
	dd	0

drv_num	db	0

times 440-($-$$)	db	0
kernel_lba	dd	0

times 510-($-$$)	db	0
dw	0xaa55
