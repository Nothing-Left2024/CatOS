; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                               boot.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                                                     Forrest Yu, 2005
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


;%define	_BOOT_DEBUG_	; 在 Boot Sector 中这一行一定要注释掉! 当写好并打开后, nasm Boot.asm -o Boot.com 生成一个 .COM 文件, 方便在 DOS 下调试

%ifdef	_BOOT_DEBUG_
	org  0100h			; 调试状态, 生成 .COM 文件, 偏移地址可从 100h 开始
%else
	org  07c00h			; Boot 状态, Bios 将 Boot Sector 加载到 0:7C00 处开始执行
%endif

;================================================================================================
%ifdef	_BOOT_DEBUG_
BaseOfStack		equ	0100h	; 调试状态下堆栈地址(栈顶, 必须位于低地址端)
%else
BaseOfStack		equ	07c00h	; Boot状态下堆栈地址(栈顶, 必须位于低地址端)
%endif

%include	"load.inc"
;================================================================================================

	jmp short LABEL_START		; Start to boot.
	nop				; 这个 nop 不可少

; 下面是 FAT12 磁盘的头, 之所以要包括这里是因为下面用到了这里的一些信息
%include	"fat12hdr.inc"

LABEL_START:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, BaseOfStack

	; 清屏
	mov	ax, 0600h		; AH = 6,  AL = 0h
	mov	bx, 0700h		; 黑底白字(BL = 07h)
	mov	cx, 0			; 左上角: (0, 0)
	mov	dx, 0184fh		; 右下角: (80, 50)
	int	10h			; int 10h

	mov	dh, 0			; "Booting  "
	call	DispStr			; 显示字符串

	xor	ah, ah	; ``
	xor	dl, dl	; `` 驱动器号复位 (A 盘: 0)
	int	13h	; `` 重置软盘驱动器

	; ===== 直接加载 Loader =====
	; 从扇区 2 开始连续读取, 目标地址 BaseOfLoader:OffsetOfLoader = 0x9000:0x100
	mov	word [wSectorNo], 2		; 从扇区 2 开始 (紧接 boot sector 之后)
	mov	ax, BaseOfLoader
	mov	es, ax				; es <- BaseOfLoader
	mov	bx, OffsetOfLoader		; bx <- OffsetOfLoader

.LOOP_LOAD:
	cmp	word [wSectorCount], 0
	jz	.LOAD_DONE
	dec	word [wSectorCount]

	push	ax
	push	bx
	; 打印一个 '.' 表示进度
	mov	ah, 0Eh
	mov	al, '.'
	mov	bl, 0Fh
	int	10h
	pop	bx
	pop	ax

	mov	ax, [wSectorNo]		; 要读的起始扇区号
	mov	cl, 1			; 每次读 1 个扇区
	call	ReadSector
	inc	word [wSectorNo]
	add	bx, [BPB_BytsPerSec]
	jmp	.LOOP_LOAD

.LOAD_DONE:
	mov	dh, 1			; "Ready.   "
	call	DispStr			; 显示字符串

; *****************************************************************************************************
	jmp	BaseOfLoader:OffsetOfLoader	; 这一句正式跳转到已加载到内存中的 LOADER.BIN 的开始处
						; 开始执行 LOADER.BIN 的代码
						; Boot Sector 的使命到此结束
; *****************************************************************************************************



;============================================================================
;变量
;----------------------------------------------------------------------------
wSectorNo		dw	0		; 要读取的扇区号
wSectorCount		dw	32		; Loader 占用的扇区数 (32 * 512 = 16384 字节)

;============================================================================
;字符串
;----------------------------------------------------------------------------
; 为简化代码, 下面每个字符串的长度都为 MessageLength
MessageLength		equ	9
BootMessage:		db	"Booting  "; 9字节, 不足补空格. 末尾无 0
Message1		db	"Ready.   "; 9字节, 不足补空格. 末尾无 0
Message2		db	"No LOADER"; 9字节, 不足补空格. 末尾无 0
;============================================================================


;----------------------------------------------------------------------------
; 函数名: DispStr
;----------------------------------------------------------------------------
; 作用:
;	显示一个字符串, 函数刚开始时 dh 的应该是字符串序号(0-based)
DispStr:
	mov	ax, MessageLength
	mul	dh
	add	ax, BootMessage
	mov	bp, ax			; ``
	mov	ax, ds			; `` ES:BP = 串地址
	mov	es, ax			; ``
	mov	cx, MessageLength	; CX = 串长度
	mov	ax, 01301h		; AH = 13,  AL = 01h
	mov	bx, 0007h		; 页号为0(BH = 0) 黑底白字(BL = 07h)
	mov	dl, 0
	int	10h			; int 10h
	ret


;----------------------------------------------------------------------------
; 函数名: ReadSector
;----------------------------------------------------------------------------
; 作用:
;	从第 ax 个 Sector 开始, 将 cl 个 Sector 读入 es:bx 中
ReadSector:
	; -----------------------------------------------------------------------
	; 怎样由扇区号求扇区在磁盘中的位置 (扇区号 -> 柱面, 起始扇区, 磁头号)
	; -----------------------------------------------------------------------
	; 设扇区号为 x
	;                           柱面号 = y >> 1
	;       x        除以 y 得   ->
	; -------------- =>         商 y              起始扇区号 = z + 1
	;  每磁道扇区数             余 z =>
	;                   磁头号 => y & 1
	; -----------------------------------------------------------------------
	push	bp
	mov	bp, sp
	sub	esp, 2			; 分出两个字节的栈空间来保存要读的扇区数: byte [bp-2]

	mov	byte [bp-2], cl
	push	bx			; 保存 bx
	mov	bl, [BPB_SecPerTrk]	; bl: 除数
	div	bl			; y 在 al 中, z 在 ah 中
	inc	ah			; z ++
	mov	cl, ah			; cl <- 起始扇区号
	mov	dh, al			; dh <- y
	shr	al, 1			; y >> 1 (其实 y/BPB_NumHeads, 这里BPB_NumHeads=2)
	mov	ch, al			; ch <- 柱面号
	and	dh, 1			; dh & 1 = 磁头号
	pop	bx			; 恢复 bx
	; 至此, "柱面号, 起始扇区, 磁头号" 全部得到 ^^^^^^^^^^^^^^^^^^^^^^^^
	mov	dl, [BS_DrvNum]		; 驱动器号 (0 表示 A 盘)
.GoOnReading:
	mov	ah, 2			; 读
	mov	al, byte [bp-2]		; 读 al 个扇区
	int	13h
	jc	.GoOnReading		; 如果读取错误 CF 会被置为 1, 这时就不停地读, 直到正确为止

	add	esp, 2
	pop	bp

	ret

;----------------------------------------------------------------------------

times 	510-($-$$)	db	0	; 填充剩下的空间, 使生成的二进制代码恰好为512字节
dw 	0xaa55				; 结束标志
