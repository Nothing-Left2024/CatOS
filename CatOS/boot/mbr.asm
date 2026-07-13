
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                               mbr.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
; 标准 MBR 引导代码
; 功能: 将自身移到 0x0600, 读取活动分区的 VBR 到 0x7C00, 跳转
; 分区表 (4 条目) 在偏移 0x1BE, 由 fat_format 填充
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

org	0x0600
bits	16

start:
	jmp	short move_code
	nop

move_code:
	cli
	xor	ax, ax
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, 0x7C00
	sti

	; 将 512 字节从 0x7C00 移动到 0x0600
	cld
	mov	si, 0x7C00
	mov	di, 0x0600
	mov	cx, 256			; 256 words = 512 bytes
	rep	movsw

	; 跳转到 0x0000:main (org=0x0600, 标签地址匹配物理地址)
	jmp	0x0000:main

main:
	; 保存驱动器号
	mov	[drv_num], dl

	; 复位磁盘
	mov	dl, [drv_num]
	xor	ah, ah
	int	13h

	; 扫描分区表 (偏移 0x1BE, 4 个条目, 每个 16 字节)
	mov	si, partition_table
	mov	cx, 4

.scan:
	cmp	byte [si], 0x80		; 活动分区标志
	je	.found
	add	si, 16
	loop	.scan

	; 没有活动分区, 使用第一个分区
	mov	si, partition_table

.found:
	; 读取分区起始 LBA (分区表项偏移 8, 4 字节)
	mov	eax, [si + 8]
	mov	[dap + 8], eax
	mov	dword [dap + 12], 0

	; 读取 VBR 到 0x0000:0x7C00
	mov	si, dap
	mov	ah, 42h
	mov	dl, [drv_num]
	int	13h
	jc	.error

	; 跳转到 VBR
	mov	dl, [drv_num]
	jmp	0x0000:0x7C00

.error:
	hlt
	jmp	.error

; DAP (Disk Address Packet)
dap:
	db	10h			; 包大小
	db	0			; 保留
	dw	1			; 传输扇区数
	dw	0x7C00			; 目标偏移
	dw	0			; 目标段
	dd	0			; 起始 LBA 低 32 位
	dd	0			; 起始 LBA 高 32 位

drv_num	db	0

; 填充到偏移 0x1BE
	times 0x1BE-($-$$)	db	0

; 分区表 (4 个条目, 每个 16 字节, 由 fat_format 填充)
partition_table:
	times 64	db	0

; 引导签名
	dw	0xAA55
