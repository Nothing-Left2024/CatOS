
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                               loader.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                                                     Forrest Yu, 2005
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


org  0100h

	jmp	LABEL_START		; Start

; 下面是 FAT12 磁盘的头, 之所以要包含这里是因为下面用到了这里的一些信息
%include	"fat12hdr.inc"
%include	"load.inc"
%include	"pm.inc"


; GDT ------------------------------------------------------------------------------------------------------------------------------------------------------------
;                                                基地址            段界限     , 属性
LABEL_GDT:			Descriptor             0,                    0, 0						; 空描述符
LABEL_DESC_FLAT_C:		Descriptor             0,              0fffffh, DA_CR  | DA_32 | DA_LIMIT_4K			; 0 ~ 4G
LABEL_DESC_FLAT_RW:		Descriptor             0,              0fffffh, DA_DRW | DA_32 | DA_LIMIT_4K			; 0 ~ 4G
LABEL_DESC_VIDEO:		Descriptor	 0B8000h,               0ffffh, DA_DRW                         | DA_DPL3	; 显存首地址
; GDT ------------------------------------------------------------------------------------------------------------------------------------------------------------

GdtLen		equ	$ - LABEL_GDT
GdtPtr		dw	GdtLen
		dd	BaseOfLoaderPhyAddr + LABEL_GDT

; ===== IDT (built at runtime in PM code) =====
IdtPtr		dw	2048				; 256 * 8
		dd	BaseOfLoaderPhyAddr + LABEL_IDT

LABEL_IDT:						; 2048 bytes of space
	times	256*8	db	0

; GDT 选择子 ----------------------------------------------------------------------------------
SelectorFlatC		equ	LABEL_DESC_FLAT_C	- LABEL_GDT
SelectorFlatRW		equ	LABEL_DESC_FLAT_RW	- LABEL_GDT
SelectorVideo		equ	LABEL_DESC_VIDEO	- LABEL_GDT + SA_RPL3
; GDT 选择子 ----------------------------------------------------------------------------------


BaseOfStack	equ	0100h


LABEL_START:			; <--- 从这里开始 *************
	mov	ax, cs
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, BaseOfStack

	; 保存 BIOS 传入的驱动器号 (DL: 0=软盘, 80h=硬盘)
	mov	[BS_DrvNum], dl

	; 同时存储到固定物理地址 0x500, 供内核检测启动介质
	push	ds
	push	ax
	xor	ax, ax
	mov	ds, ax
	mov	[0500h], dl
	pop	ax
	pop	ds

	; ===== VBE 640x400x256 LFB 模式设置 (实模式, BIOS INT 10h 可用) =====
	; 初始化图形模式标志 (物理地址 0x504, 0=进入图形模式, 非0=文本模式)
	; 默认值: 0 = 进入图形模式
	push	ds
	xor	ax, ax
	mov	ds, ax
	mov	byte [0504h], 0		; 默认为 0 (图形模式)
	mov	al, [0504h]		; 读取图形模式标志
	pop	ds
	
	; ★ al=0 进入图形模式（默认），al!=0 保持文本模式
	test	al, al
	jnz	.SkipGraphicsMode

.SetGraphicsMode:
	; 先尝试 VBE 640x480x256 LFB 模式 (更高分辨率)
	; 第 0 步: 检查 VBE 是否存在 (功能 0x4F00)
	push	es
	push	di
	mov	ax, 04F00h		; VBE: Return Controller Info
	push	ds
	pop	es
	mov	di, _VBE_ModeInfo	; 临时借用 ModeInfo 缓冲区存 VBE 信息
	int	10h
	pop	di
	pop	es
	cmp	ax, 004Fh		; 成功?
	jnz	.TryVGA13		; 失败 → VBE 不存在, 直接用 VGA13

	; 尝试多个 VBE 模式 (按优先级从高到低)
	; 优先级 1: 通过 VBE 模式枚举查找 1920x1080x256 (非标准模式号, 需枚举)
	call	FindVBE1920
	jnc	.SkipGraphicsMode	; 成功则跳过后续回退

	; 优先级 2: 固定模式号 0x107 = 1280x1024x256
	mov	cx, 0107h
	call	TryVBEMode
	jnc	.SkipGraphicsMode	; 成功则跳过后续回退

	; 优先级 3: 固定模式号 0x105 = 1024x768x256
	mov	cx, 0105h
	call	TryVBEMode
	jnc	.SkipGraphicsMode	; 成功则跳过后续回退

	; 优先级 4: 固定模式号 0x103 = 800x600x256
	mov	cx, 0103h
	call	TryVBEMode
	jnc	.SkipGraphicsMode	; 成功则跳过后续回退

	; 优先级 5: 固定模式号 0x101 = 640x480x256 (标准 VBE, 兼容性好)
	mov	cx, 0101h
	call	TryVBEMode
	jnc	.SkipGraphicsMode	; 成功则跳过 VGA13 回退

.TryVGA13:
	; VBE 失败，回退到 VGA Mode 13h (320x200x256)
	mov	ax, 0013h
	int	10h

	; ★ 写入图形模式参数到 0x500 参数区
	push	ds
	push	ax
	xor	ax, ax
	mov	ds, ax
	; [0x504] = video_mode (1=VGA13)
	mov	byte [0504h], 1
	; [0x506] = vmem_base (32-bit, 0xA0000)
	mov	word [0506h], 0000h		; low word = 0x0000
	mov	word [0508h], 000Ah		; high word = 0x000A
	; [0x50A] = screen_width (320)
	mov	word [050Ah], 320
	; [0x50C] = screen_height (200)
	mov	word [050Ch], 200
	; [0x50E] = bytes_per_pixel (1)
	mov	byte [050Eh], 1
	; [0x510] = bytes_per_scan_line (320 for mode 13h)
	mov	word [0510h], 320
	pop	ax
	pop	ds

	; 设置 VGA 兼容调色板 (前16色)
	call	SetVGA13Palette

.SkipGraphicsMode:

	; 得到内存信息 (INT 15h E820h)
	xor	bx, bx			; bx = 初始值, 第一次调用时为 0
	mov	di, _MemChkBuf		; es:di 指向一个地址范围描述符结构(Address Range Descriptor Structure)
.MemChkLoop:
	mov	eax, 0E820h		; eax = 0000E820h
	mov	ecx, 20			; ecx = 地址范围描述符结构的大小
	; 用内存方式设置 SMAP 签名, 避免实模式下立即数超过 16 位的问题
	mov	dword [_SMAP_SIGNATURE], 0534D4150h
	mov	edx, [_SMAP_SIGNATURE]
	int	15h			; int 15h
	jc	.MemChkFail
	add	di, 20
	inc	dword [_dwMCRNumber]	; dwMCRNumber = ARDS 的个数
	cmp	bx, 0
	jne	.MemChkLoop
	jmp	.MemChkOK

.MemChkFail:
	; E820h 失败, 使用 CMOS 方法回退
	; CMOS 0x30 = 低字节, 0x31 = 高字节 (KB单位, 16-bit, 最大64MB)
	mov	al, 0x30
	out	0x70, al
	in	al, 0x71
	mov	ah, al
	mov	al, 0x31
	out	0x70, al
	in	al, 0x71
	mov	bx, ax			; bx = 内存大小 (KB)

	; CMOS 0x34/0x35 = 扩展内存 (>16MB, 64KB单位)
	mov	al, 0x34
	out	0x70, al
	in	al, 0x71
	mov	ch, al
	mov	al, 0x35
	out	0x70, al
	in	al, 0x71
	mov	cl, al			; cx = 扩展内存 (64KB单位)

	; 计算总内存大小 (字节) - 使用纯16位运算
	cmp	cx, 0
	jz	.CMOS_NoExt
	; 有扩展内存: 总内存 = 16MB + cx * 64KB
	; 16MB = 0x1000000, 64KB = 0x10000
	mov	ax, cx			; ax = 扩展内存 (64KB单位)
	mov	dx, 0			; dx:ax = cx
	mov	bx, 0x10000		; bx = 64KB
	mul	bx			; dx:ax = cx * 64KB
	; 加上 16MB = 0x1000000
	add	ax, 0x0000
	adc	dx, 0x0100		; dx:ax = 16MB + cx * 64KB
	jmp	.CMOS_Save
.CMOS_NoExt:
	; 无扩展内存: 总内存 = 1MB + bx * 1KB
	; 1MB = 0x100000, 1KB = 0x400
	mov	ax, bx			; ax = 内存大小 (KB)
	mov	dx, 0			; dx:ax = bx
	mov	bx, 0x0400		; bx = 1KB
	mul	bx			; dx:ax = bx * 1KB
	; 加上 1MB = 0x100000
	add	ax, 0x0000
	adc	dx, 0x0010		; dx:ax = 1MB + bx * 1KB
.CMOS_Save:
	; 保存到 dwMemSize (dword)
	mov	[_dwMemSize], ax
	mov	[_dwMemSize + 2], dx
	; 保底: 至少 16MB (0x1000000)
	cmp	dx, 0x0100
	jb	.CMOS_Fallback
	jmp	.CMOS_Valid
.CMOS_Fallback:
	mov	word [_dwMemSize], 0x0000
	mov	word [_dwMemSize + 2], 0x0100	; 强制 16MB
.CMOS_Valid:
	mov	dword [_dwMCRNumber], 0	; 标记未使用 E820h
	jmp	.MemChkOK

.MemChkOK:

	; ============================================================
	; 加载 KERNEL.BIN 到临时缓冲区 0x10000 (80KB)
	; 之后在保护模式下由 InitKernel 复制到 0x30400 (执行地址)
	; 0x10000 处保留 kernel.bin 原始副本供 setup 命令使用
	; ============================================================
	cmp	byte [BS_DrvNum], 80h
	jb	.floppy_load

	; --- 硬盘: LBA 读取 ---
	; 复位硬盘
	mov	dl, [BS_DrvNum]
	xor	ah, ah
	int	13h			; 复位硬盘
	jc	.hd_read_err

	; ===== 读取 VBR 扇区 (LBA=PART_START), 从偏移 440 获取 kernel LBA =====
	mov	ax, cs
	mov	ds, ax			; 确保 ds = cs
	mov	dl, [BS_DrvNum]		; 修复：显式设置驱动器号
	mov	si, dap_vbr
	mov	ah, 42h
	int	13h
	jc	.hd_read_err

	; 从 VBR 缓冲区偏移 440 (0x1B8) 读取 kernel 起始 LBA (4字节)
	mov	ax, 06000h
	mov	es, ax
	mov	ax, [es:01B8h]		; 低16位
	mov	dx, [es:01BAh]		; 高16位
	mov	word [_kernel_lba_temp], ax
	mov	word [_kernel_lba_temp + 2], dx

	; 从 VBR 偏移 444 (0x1BC) 读取 kernel 扇区数 (2字节)
	mov	cx, [es:01BCh]
	test	cx, cx
	jnz	.have_sect_count
	mov	cx, 512			; 默认值 (兼容旧 VBR)
.have_sect_count:
	mov	word [_hd_sect_left], cx

	; 检查 kernel_lba 是否为 0 (setup 未正确写入)
	mov	ax, [_kernel_lba_temp]
	or	ax, [_kernel_lba_temp + 2]
	jz	.halt

	; 恢复 ds = cs
	mov	ax, cs
	mov	ds, ax

	; ===== 循环逐扇区读取 kernel =====
	; 使用内存变量跟踪状态, 避免BIOS破坏寄存器导致问题
	mov	ax, [_kernel_lba_temp]
	mov	[_hd_lba], ax
	mov	ax, [_kernel_lba_temp + 2]
	mov	[_hd_lba + 2], ax
	mov	word [_hd_dest_off], 0
	mov	word [_hd_dest_seg], 1000h
.hd_loop:
	; 确保 ds = cs (BIOS INT 13h可能破坏ds)
	mov	ax, cs
	mov	ds, ax

	; 设置 DAP
	mov	ax, [_hd_lba]
	mov	[dap_k1 + 8], ax
	mov	ax, [_hd_lba + 2]
	mov	[dap_k1 + 10], ax
	mov	word [dap_k1 + 12], 0
	mov	word [dap_k1 + 14], 0
	mov	word [dap_k1 + 2], 1
	mov	ax, [_hd_dest_off]
	mov	[dap_k1 + 4], ax
	mov	ax, [_hd_dest_seg]
	mov	[dap_k1 + 6], ax

	; 准备INT 13h
	mov	ax, cs
	mov	ds, ax
	mov	dl, [BS_DrvNum]
	mov	si, dap_k1
	mov	ah, 42h
	int	13h
	jc	.hd_read_err_diag

	; 递增LBA (32位)
	add	word [_hd_lba], 1
	adc	word [_hd_lba + 2], 0

	; 递增目标偏移
	add	word [_hd_dest_off], 512
	jnz	.hd_no_wrap
	add	word [_hd_dest_seg], 1000h
.hd_no_wrap:
	dec	word [_hd_sect_left]
	jnz	.hd_loop

	; 设置es:bx指向加载完成的缓冲区(兼容后续代码)
	mov	ax, [_hd_dest_seg]
	mov	es, ax
	mov	bx, [_hd_dest_off]

	jmp	.KERNEL_LOADED

; 硬盘读取错误: 显示 'E' 后跟 AH 错误码 (十六进制)
.hd_read_err_diag:
	mov	bl, ah		; 保存 AH 错误码
	mov	ah, 0Eh
	mov	al, 'E'
	int	10h
	mov	al, bl
	; 显示 AH 高4位
	and	ax, 0FFh
	mov	cl, 4
	shr	al, cl
	call	.hex_digit
	mov	ah, 0Eh
	int	10h
	; 显示 AH 低4位
	mov	al, bl
	and	al, 0Fh
	call	.hex_digit
	mov	ah, 0Eh
	int	10h
	jmp	.halt
.hex_digit:
	cmp	al, 10
	jl	.is_digit
	add	al, 'A' - 10
	ret
.is_digit:
	add	al, '0'
	ret

.hd_read_err:
	mov	ah, 0Eh
	mov	al, 'E'
	int	10h
.halt:
	hlt
	jmp	.halt

.floppy_load:
	; --- 软盘: CHS 读取 ---
	mov	dl, [BS_DrvNum]
	xor	ah, ah
	int	13h

	mov	word [wSectorNo], 34	; KERNEL.BIN 起始扇区号 (boot:0, loader:2~33, kernel:34+)
	mov	ax, 01000h		; 0x1000 段 → 物理地址 0x10000 (临时缓冲区)
	mov	es, ax			; es <- 0x1000
	xor	bx, bx			; bx <- 0
					; es:bx = 0x1000:0000 = 物理地址 0x10000

.LOOP_LOAD_KERNEL:
	cmp	word [wKernelSectorCount], 0
	jz	.KERNEL_LOADED
	dec	word [wKernelSectorCount]

	mov	ax, [wSectorNo]	; 要读的扇区号
	mov	cl, 1		; 读 1 个扇区
	call	ReadSector
	inc	word [wSectorNo]
	add	bx, [BPB_BytsPerSec]
	; 处理 BX 溢出: BX+=512 后如果 BX=0, 说明段内偏移回绕, 需要增加 ES
	jnz	.LOOP_LOAD_KERNEL
	mov	ax, es
	add	ah, 10h		; ES += 0x1000 (增加 64KB 段)
	mov	es, ax
	jmp	.LOOP_LOAD_KERNEL

.KERNEL_LOADED:
	call	KillMotor		; 关闭软驱马达

	mov	dh, 1			; "Ready."
	call	DispStrRealMode		; 显示字符串
	
; 准备进入保护模式 -------------------------------------------

; ===== 1. 加载 GDTR =====
	lgdt	[GdtPtr]

; ===== 2. 开启地址线A20 (快速A20门) =====
	call	EnableA20_Simple

; ===== 3. 关中断 =====
	cli

; ===== 4. 切换到保护模式 =====
	mov	eax, cr0
	or	eax, 1
	mov	cr0, eax

; ===== 5. 远跳转刷新CS =====
	jmp	dword SelectorFlatC:(BaseOfLoaderPhyAddr+LABEL_PM_START)


;----------------------------------------------------------------------------
; EnableA20_Simple - 精简版A20开启 (兼容QEMU/VMware/Bochs)
;----------------------------------------------------------------------------
EnableA20_Simple:
	; --- 快速A20门 (端口92h, System Control Port A) ---
	in	al, 92h
	test	al, 02h		; A20已经开了?
	jnz	.A20_Done	; 是 → 跳过
	or	al, 00000010b	; 设置 A20 位 (bit 1 = 1)
	out	92h, al

	; 延迟等待A20稳定
	times	8	nop

.A20_Done:
	ret


;============================================================================
;变量
;----------------------------------------------------------------------------
wRootDirSizeForLoop	dw	RootDirSectors	; Root Directory 占用的扇区数
wSectorNo		dw	0		; 要读取的扇区号
bOdd			db	0		; 奇数还是偶数
dwKernelSize		dd	0		; KERNEL.BIN 文件大小
wKernelSectorCount	dw	512	; KERNEL.BIN 最大扇区数 (512*512=262144 字节)

; DAP: kernel 读取用
dap_k1:
	db	10h			; 包大小
	db	0			; 保留
	dw	1			; 传输扇区数 (运行时设置为1)
	dw	0			; 目标偏移 (运行时设置为bx)
	dw	01000h			; 目标段 (运行时设置为es)
	dd	0			; 起始 LBA 低 32 位 (运行时设置)
	dd	0			; 起始 LBA 高 32 位

; DAP: 读取 VBR 扇区 (LBA=PART_START, 1 扇区 → 0x6000:0000)
dap_vbr:
	db	10h			; 包大小
	db	0			; 保留
	dw	1			; 传输扇区数
	dw	0			; 目标偏移
	dw	06000h			; 目标段
	dd	PART_START		; 起始 LBA 低 32 位 (分区起始扇区)
	dd	0			; 起始 LBA 高 32 位

; 硬盘读取状态变量 (实模式用)
_hd_lba:		dd	0	; 当前读取 LBA
_hd_dest_off:	dw	0	; 目标偏移 (bx)
_hd_dest_seg:	dw	0	; 目标段 (es)
_hd_sect_left:	dw	0	; 剩余扇区数

;============================================================================
;字符串
;----------------------------------------------------------------------------
; 为简化代码, 下面每个字符串的长度都为 MessageLength
MessageLength		equ	9
LoadMessage:		db	"Loading  "
Message1		db	"Ready.   "
Message2		db	"No KERNEL"
;============================================================================

;----------------------------------------------------------------------------
; 函数名: DispStrRealMode
;----------------------------------------------------------------------------
; 作用:
;	显示一个字符串, 函数刚开始时 dh 应该是字符串序号(0-based)
DispStrRealMode:
	mov	ax, MessageLength
	mul	dh
	add	ax, LoadMessage
	mov	bp, ax			; ┓
	mov	ax, ds			; ┣ ES:BP = 串地址
	mov	es, ax			; ┛
	mov	cx, MessageLength	; CX = 串长度
	mov	ax, 01301h		; AH = 13,  AL = 01h
	mov	bx, 0007h		; 页号为0(BH = 0) 黑底白字(BL = 07h)
	mov	dl, 0
	add	dh, 3			; 从第 3 行开始显示
	int	10h			; int 10h
	ret
;----------------------------------------------------------------------------
; 函数名: ReadSector
;----------------------------------------------------------------------------
; 作用:
;	从第 ax 个 Sector 的扇区号(Directory Entry 中的 Sector 号)开始, 将 cl 个 Sector 读入 es:bx 中
ReadSector:
	; -----------------------------------------------------------------------
	; 怎样由扇区号求扇区在磁盘中的位置 (扇区号 -> 柱面号, 起始扇区, 磁头号)
	; -----------------------------------------------------------------------
	push	bp
	mov	bp, sp
	sub	esp, 2			; 辟出两个字节的栈空间保存要读的扇区数: byte [bp-2]

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
; 函数名: GetFATEntry
;----------------------------------------------------------------------------
GetFATEntry:
	push	es
	push	bx
	push	ax
	mov	ax, BaseOfKernelFile	; ┓
	sub	ax, 0100h		; ┃ 在 BaseOfKernelFile 后面留出 4K 空间来存放 FAT
	mov	es, ax			; ┛
	pop	ax
	mov	byte [bOdd], 0
	mov	bx, 3
	mul	bx			; dx:ax = ax * 3
	mov	bx, 2
	div	bx			; dx:ax / 2  ==>  ax <- 商, dx <- 余数
	cmp	dx, 0
	jz	LABEL_EVEN
	mov	byte [bOdd], 1
LABEL_EVEN:;偶数
	xor	dx, dx			; 清除 ax 对应的 FATEntry 在 FAT 中的偏移量
	mov	bx, [BPB_BytsPerSec]
	div	bx			; dx:ax / BPB_BytsPerSec
	push	dx
	mov	bx, 0			; bx <- 0
	add	ax, SectorNoOfFAT1	; ax = FATEntry 所在的扇区号
	mov	cl, 2
	call	ReadSector		; 读取 FATEntry 所在的扇区
	pop	dx
	add	bx, dx
	mov	ax, [es:bx]
	cmp	byte [bOdd], 1
	jnz	LABEL_EVEN_2
	shr	ax, 4
LABEL_EVEN_2:
	and	ax, 0FFFh

LABEL_GET_FAT_ENRY_OK:

	pop	bx
	pop	es
	ret
;----------------------------------------------------------------------------


;----------------------------------------------------------------------------
; 函数名: FindVBE1920
;----------------------------------------------------------------------------
; 作用:
;	通过 VBE 模式枚举查找 1920x1080x256 LFB 模式
;	1920x1080 没有标准 VBE 模式号, 需通过 Controller Info 的模式列表枚举
; 流程:
;	1. INT 10h AX=4F00h 获取 VBE Controller Info (含模式列表指针)
;	2. 遍历模式列表, 对每个模式号调用 4F01h 获取 ModeInfo
;	3. 检查 XResolution=1920, YResolution=1080, BitsPerPixel=8, 支持 LFB
;	4. 找到则调用 TryVBEMode 设置该模式
; 输出:
;	CF = 0 成功 (已设置模式), CF = 1 失败 (未找到或不支持)
;----------------------------------------------------------------------------
FindVBE1920:
	push	ax
	push	bx
	push	cx
	push	dx
	push	si
	push	es
	push	ds
	push	di

	; 第一步: 获取 VBE Controller Info Block (es:di = ds:_VBE_CtrlInfo)
	push	ds
	pop	es
	mov	di, _VBE_CtrlInfo
	mov	ax, 04F00h		; VBE: Return Controller Info
	int	10h
	cmp	ax, 004Fh
	jnz	.F1920_Fail

	; 第二步: 读取 VideoModePtr (偏移 0x0E, far ptr: offset+segment)
	mov	si, [es:di + 0x0E]	; si = 模式列表偏移
	mov	bx, [es:di + 0x10]	; bx = 模式列表段

	; 第三步: 设置 es = 模式列表段, 遍历模式列表
	mov	es, bx
	; si 已设置为列表偏移

.F1920_Loop:
	mov	cx, [es:si]		; cx = 当前模式号
	cmp	cx, 0FFFFh		; 列表结束?
	jz	.F1920_Fail		; 未找到 1920x1080

	; 获取该模式的 ModeInfo
	; 保存列表段 es 和偏移 si (INT 10h 会破坏 es)
	push	es
	push	si

	; es:di = ds:_VBE_ModeInfo
	push	ds
	pop	es
	mov	di, _VBE_ModeInfo
	mov	ax, 04F01h		; VBE: Return Mode Info
	int	10h

	; 恢复列表偏移 si (es 暂不恢复, 先用 ds 段检查 ModeInfo)
	pop	si

	cmp	ax, 004Fh
	jnz	.F1920_Next

	; 检查 ModeInfo (此时 es = ds, di = _VBE_ModeInfo)
	; ModeAttributes (offset 0): bit0=可用, bit7=LFB
	mov	ax, [es:di + 0]
	test	ax, 0001h
	jz	.F1920_Next
	test	ax, 0080h
	jz	.F1920_Next

	; XResolution (offset 0x12) = 1920
	mov	ax, [es:di + 12h]
	cmp	ax, 1920
	jne	.F1920_Next

	; YResolution (offset 0x14) = 1080
	mov	ax, [es:di + 14h]
	cmp	ax, 1080
	jne	.F1920_Next

	; BitsPerPixel (offset 0x19) = 8
	mov	al, [es:di + 19h]
	cmp	al, 8
	jne	.F1920_Next

	; 找到 1920x1080x256 LFB 模式!
	; 恢复列表段 es (弹出之前 push 的 es, 保持栈平衡)
	pop	es

	; cx = 模式号 (在循环开头读取, 检查过程中未破坏)
	; 调用 TryVBEMode (它会自己设置 es:di, 保存 cx)
	call	TryVBEMode
	jc	.F1920_Fail		; 设置失败

	; 成功!
	jmp	.F1920_Done

.F1920_Next:
	; 恢复列表段 es (弹出之前 push 的 es)
	pop	es
	; si 已恢复, 移动到下一个模式号
	add	si, 2
	jmp	.F1920_Loop

.F1920_Fail:
	stc

.F1920_Done:
	pop	di
	pop	ds
	pop	es
	pop	si
	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret
;----------------------------------------------------------------------------

;----------------------------------------------------------------------------
; 函数名: TryVBEMode
;----------------------------------------------------------------------------
; 作用:
;	尝试设置指定的 VBE LFB 模式
; 输入:
;	CX = 模式号 (如 0x101 = 640x480x256)
; 输出:
;	CF = 0 成功, CF = 1 失败
;	成功时 0x500 参数区已填充, 调色板已设置
;----------------------------------------------------------------------------
TryVBEMode:
	push	ax
	push	bx
	push	cx
	push	dx
	push	es
	push	di
	push	ds

	; 第一步: 获取 Mode Info Block
	mov	ax, 04F01h		; VBE: Return Mode Info
	push	ds
	pop	es
	mov	di, _VBE_ModeInfo	; es:di = ModeInfoBlock 地址
	int	10h
	cmp	ax, 004Fh		; 成功?
	jnz	.VBE_Fail		; 失败

	; 第二步: 检查模式属性
	mov	ax, [es:di + 0]	; ModeAttributes
	test	ax, 0001h		; bit0: 模式可用?
	jz	.VBE_Fail
	test	ax, 0080h		; bit7: 支持 LFB?
	jz	.VBE_Fail

	; 第三步: 设置 VBE 模式 (LFB)
	mov	ax, 04F02h		; VBE: Set Mode
	mov	bx, cx			; 模式号
	or	bx, 4000h		; 加上 LFB 位 (bit14)
	int	10h
	cmp	ax, 004Fh
	jnz	.VBE_Fail		; 失败回退

	; ★ VBE 模式设置成功，写入参数到 0x500 参数区
	push	ax
	push	cx
	push	ds
	push	es	; ★ 保存 es (BIOS 可能在 INT 10h 期间修改)
	; 重新加载 es = cs (确保 es:di 指向 ModeInfoBlock)
	push	cs
	pop	es
	mov	di, _VBE_ModeInfo
	; ds = 0 (写入物理地址 0x500 区域)
	xor	ax, ax
	mov	ds, ax
	; [0x504] = video_mode (2=VBE_LFB)
	mov	byte [0504h], 2
	; [0x506] = vmem_base (32-bit LFB 物理地址)
	mov	eax, [es:di + 28h]	; PhysBasePtr (offset 0x28)
	mov	[0506h], eax
	; [0x50A] = screen_width
	mov	ax, [es:di + 12h]	; XResolution (offset 0x12)
	mov	[050Ah], ax
	; [0x50C] = screen_height
	mov	ax, [es:di + 14h]	; YResolution (offset 0x14)
	mov	[050Ch], ax
	; [0x50E] = bytes_per_pixel
	mov	al, [es:di + 19h]	; BitsPerPixel (offset 0x19)
	shr	al, 3			; /8 = 字节数
	mov	[050Eh], al
	; [0x510] = bytes_per_scan_line (pitch/stride)
	mov	ax, [es:di + 10h]	; LinBytesPerScanLine (offset 0x10)
	test	ax, ax			; 如果为0则使用width
	jnz	.store_pitch
	mov	ax, [es:di + 12h]	; 回退到 XResolution
.store_pitch:
	mov	[0510h], ax
	pop	es
	pop	ds
	pop	cx
	pop	ax

	; 设置 VGA 兼容调色板
	call	SetVGA13Palette

	; 成功: CF = 0
	clc
	jmp	.VBE_Done

.VBE_Fail:
	; 失败: CF = 1
	stc

.VBE_Done:
	pop	ds
	pop	di
	pop	es
	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret
;----------------------------------------------------------------------------

;----------------------------------------------------------------------------
; 函数名: SetVGA13Palette
;----------------------------------------------------------------------------
; 作用:
;	配置 VGA Mode 13h 调色板 (DAC 0x3C8/0x3C9)
;	前16色映射为文本模式标准 CGA/EGA 色, 便于兼容现有代码
;	后续可扩展更多颜色
;----------------------------------------------------------------------------
SetVGA13Palette:
	push	ax
	push	bx
	push	cx
	push	dx
	push	si
	push	ds

	; 切换 ds 到 cs 段 (调色板表在代码段中)
	mov	ax, cs
	mov	ds, ax

	; VGA DAC 寄存器: 0x3C8写索引, 0x3C9写RGB (每个6位)
	mov	dx, 03C8h		; DAC 索引端口
	mov	al, 0			; 从索引0开始
	out	dx, al

	inc	dx			; dx = 03C9h (DAC 数据端口)

	; 设置前16色 (标准CGA/EGA palette, 6位RGB)
	mov	si, CGA_Palette_Table	; si = 调色板表地址
	mov	cx, 16			; 设置16色

.palette_loop:
	; 写入 DAC: R, G, B 各一个字节 (6位)
	lodsb				; al = R (ds:si++)
	out	dx, al
	lodsb				; al = G
	out	dx, al
	lodsb				; al = B
	out	dx, al
	loop	.palette_loop

	pop	ds
	pop	si
	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret

;----------------------------------------------------------------------------
; 辅助函数: GetCGAColor
; 输入: al = 色号 (0~15)
; 输出: al = R (6位), ah = G (6位), bl = B (6位)
;----------------------------------------------------------------------------
GetCGAColor:
	push	ds
	push	cx
	and	al, 0Fh			; 确保色号在0~15范围

	; 查表获取RGB值 (每色3字节: R,G,B)
	mov	cx, ax			; cx = 色号
	mov	ax, cs
	mov	ds, ax
	mov	bx, CGA_Palette_Table	; 表基址
	add	bx, cx			; bx = 表基址 + 色号
	add	bx, cx
	add	bx, cx			; bx = 表基址 + 色号*3

	mov	al, [bx]		; R
	mov	ah, [bx+1]		; G
	mov	bl, [bx+2]		; B
	pop	cx
	pop	ds
	ret

; CGA/EGA 标准16色表 (RGB, 每值6位 = 0~63)
; 黑/蓝/绿/青/红/紫/棕/浅灰/深灰/浅蓝/浅绿/浅青/浅红/浅紫/黄/白
CGA_Palette_Table:
	db	0, 0, 0		; 0: 黑
	db	0, 0, 42	; 1: 蓝
	db	0, 42, 0	; 2: 绿
	db	0, 42, 42	; 3: 青
	db	42, 0, 0	; 4: 红
	db	42, 0, 42	; 5: 紫
	db	42, 21, 0	; 6: 棕
	db	42, 42, 42	; 7: 浅灰
	db	21, 21, 21	; 8: 深灰
	db	21, 21, 63	; 9: 浅蓝
	db	21, 63, 21	; 10: 浅绿
	db	21, 63, 63	; 11: 浅青
	db	63, 21, 21	; 12: 浅红
	db	63, 21, 63	; 13: 浅紫
	db	63, 63, 21	; 14: 黄
	db	63, 63, 63	; 15: 白


;----------------------------------------------------------------------------
; 函数名: KillMotor
;----------------------------------------------------------------------------
; 作用:
;	关闭软驱马达
KillMotor:
	push	dx
	mov	dx, 03F2h
	mov	al, 0
	out	dx, al
	pop	dx
	ret
;----------------------------------------------------------------------------


; 从此处以后的内容在保护模式下执行 ----------------------------------------------------
; 32 位代码段. 实模式跳入 ---------------------------------------------------------
[SECTION .s32]

ALIGN	32

[BITS	32]

; ---- Dummy interrupt/exception handler ----
; All IDT entries point here - prevents triple fault
DummyHandler:
	; 异常发生: 把屏幕填成红色 (VGA13 320x200 模式)
	; 这样我们就能知道异常触发了
	push	eax
	push	ecx
	push	edi
	push	gs
	mov	ax, SelectorVideo
	mov	gs, ax
	xor	edi, edi
	mov	al, 40h		; 红色
	mov	ecx, 320*200
	cld
.red_fill_loop:
	mov	[gs:edi], al
	inc	edi
	loop	.red_fill_loop
	pop	gs
	pop	edi
	pop	ecx
	pop	eax
.halt:
	cli
	jmp	.halt

ALIGN	32

LABEL_PM_START:
	; ============================================================
	; CRITICAL: Build and load IDT FIRST!
	; Without IDT, any interrupt = triple fault = CPU reset
	; ============================================================

	; ---- Load segments first (need DS for memory writes) ----
	mov	ax, SelectorFlatRW
	mov	ds, ax
	mov	es, ax
	mov	fs, ax
	mov	ss, ax
	mov	esp, 0x7E00

	; ---- Build minimal IDT (all 256 entries → DummyHandler) ----
	mov	edi, BaseOfLoaderPhyAddr + LABEL_IDT
	mov	ecx, 256
	mov	eax, BaseOfLoaderPhyAddr + DummyHandler
.FillIDT:
	push	ecx
	push	eax
	mov	word [edi+0], ax
	mov	word [edi+2], SelectorFlatC
	mov	word [edi+4], 08E00h
	shr	eax, 16
	mov	word [edi+6], ax
	pop	eax
	add	edi, 8
	pop	ecx
	loop	.FillIDT

	; ---- Load IDT ----
	lidt	[BaseOfLoaderPhyAddr + IdtPtr]

	; ---- 动态修改视频段描述符基址 ----
	; 读取 0x504 的 video_mode 标志: 0=文本, 1=VGA13, 2=VBE_LFB
	mov	ax, SelectorFlatRW
	mov	ds, ax
	mov	al, [0504h]		; video_mode
	test	al, al
	jz	.LoadVideoSeg		; al=0 (文本模式) → 保持默认 GDT (0xB8000)

	; 图形模式: 从参数区读取显存基址
	mov	eax, [0506h]		; vmem_base (32-bit)
	mov	edi, BaseOfLoaderPhyAddr + LABEL_DESC_VIDEO

	; 设置基址
	mov	word [edi+2], ax	; base_low (bits 0-15)
	shr	eax, 16
	mov	byte [edi+4], al	; base_mid (bits 16-23)
	mov	byte [edi+7], ah	; base_high (bits 24-31)

	; 设置段界限和属性
	cmp	byte [0504h], 2
	jz	.VBE_LFB_Limit
	; VGA13 模式: 64KB, 字节粒度
	mov	word [edi+0], 0FFFFh	; limit_low = 0xFFFF
	and	byte [edi+6], 00Fh	; limit_high = 0, 清除 G 位(字节粒度)
	jmp	.LoadVideoSeg

.VBE_LFB_Limit:
	; VBE LFB 模式: 大段, 4KB 粒度, 段界限 = 0xFFFFF (4GB 地址空间)
	mov	word [edi+0], 0FFFFh	; limit_low = 0xFFFF
	mov	byte [edi+6], 0CFh	; limit_high=0xF, G=1(4KB粒度), D/B=1(32位)

.LoadVideoSeg:
	; 重新加载 GDT (因为修改了描述符)
	lgdt	[BaseOfLoaderPhyAddr + GdtPtr]

	; ---- Load video segment ----
	mov	ax, SelectorVideo
	mov	gs, ax

	; ============================================================
	; 图形模式下跳过文本输出 (DispStr 会往图形显存写垃圾像素)
	; ============================================================
	mov	al, [0504h]
	test	al, al
	jnz	.SkipDispOutput		; 非文本模式 → 跳过输出

	; ============================================================
	; 调用DispStr / DispMemInfo / SetupPaging / InitKernel
	; ============================================================
	push	szBootOK
	call	DispStr
	add	esp, 4

	call	DispMemInfo

.SkipDispOutput:
	call	SetupPaging
	call	InitKernel

	; 文本模式下才显示内核加载信息
	mov	al, [0504h]
	test	al, al
	jnz	.SkipDispOutput2
	push	szKernelLoaded
	call	DispStr
	add	esp, 4
.SkipDispOutput2:

	; ============================================================
	; InitKernel 已将 kernel 从临时缓冲区 (0x10000) 复制到 0x30400
	; 0x10000 处保留 kernel.bin 原始副本, 供 setup 命令写入硬盘
	; ============================================================
	cli
	jmp	SelectorFlatC:KernelEntryPointPhyAddr


	; 内存看上去是这个样子的
	;              ┃                                    ┃
	;              ┃                 .                  ┃
	;              ┃                 .                  ┃
	;              ┃                 .                  ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;              ┃          Page  Tables              ┃
	;              ┃        (由LOADER设置)              ┃
	;    00101000h ┃                                    ┃ PageTblBase
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;    00100000h ┃    Page Directory Table            ┃ PageDirBase  <- 1M
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;       F0000h ┃            System ROM              ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;       E0000h ┃   Expansion of system ROM          ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;       C0000h ┃  Reserved for ROM expansion        ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓ B8000h ← gs
	;       A0000h ┃  Display adapter reserved          ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;       9FC00h ┃  extended BIOS data area (EBDA)    ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;       90000h ┃          LOADER.BIN                ┃ somewhere in LOADER ← esp
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;       10000h ┃        KERNEL.BIN (temp)           ┃ temporary load buffer
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;       30000h ┃            KERNEL                  ┃ 30400h ← KERNEL 入口
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┃                                    ┃
	;        7E00h ┃              F  R  E  E            ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
	;              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
	;        7C00h ┃         BOOT  SECTOR               ┃
	;              ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛


; ------------------------------------------------------------------------
; 显示 AL 中的数字
; ------------------------------------------------------------------------
DispAL:
	push	ecx
	push	edx
	push	edi

	mov	edi, [dwDispPos]

	mov	ah, 0Fh
	mov	dl, al
	shr	al, 4
	mov	ecx, 2
.begin:
	and	al, 01111b
	cmp	al, 9
	ja	.1
	add	al, '0'
	jmp	.2
.1:
	sub	al, 0Ah
	add	al, 'A'
.2:
	mov	[gs:edi], ax
	add	edi, 2

	mov	al, dl
	loop	.begin

	mov	[dwDispPos], edi

	pop	edi
	pop	edx
	pop	ecx

	ret
; DispAL 结束-------------------------------------------------------------


; ------------------------------------------------------------------------
; 显示一个整数
; ------------------------------------------------------------------------
DispInt:
	mov	eax, [esp + 4]
	shr	eax, 24
	call	DispAL

	mov	eax, [esp + 4]
	shr	eax, 16
	call	DispAL

	mov	eax, [esp + 4]
	shr	eax, 8
	call	DispAL

	mov	eax, [esp + 4]
	call	DispAL

	mov	ah, 07h
	mov	al, 'h'
	push	edi
	mov	edi, [dwDispPos]
	mov	[gs:edi], ax
	add	edi, 4
	mov	[dwDispPos], edi
	pop	edi

	ret
; DispInt 结束------------------------------------------------------------

; ------------------------------------------------------------------------
; 显示一个字符串
; ------------------------------------------------------------------------
DispStr:
	push	ebp
	mov	ebp, esp
	push	ebx
	push	esi
	push	edi

	mov	esi, [ebp + 8]	; pszInfo
	mov	edi, [dwDispPos]
	mov	ah, 0Fh
.1:
	lodsb
	test	al, al
	jz	.2
	cmp	al, 0Ah
	jnz	.3
	push	eax
	mov	eax, edi
	mov	bl, 160
	div	bl
	and	eax, 0FFh
	inc	eax
	mov	bl, 160
	mul	bl
	mov	edi, eax
	pop	eax
	jmp	.1
.3:
	mov	[gs:edi], ax
	add	edi, 2
	jmp	.1

.2:
	mov	[dwDispPos], edi

	pop	edi
	pop	esi
	pop	ebx
	pop	ebp
	ret
; DispStr 结束------------------------------------------------------------

; ------------------------------------------------------------------------
; 作用
; ------------------------------------------------------------------------
DispReturn:
	push	szReturn
	call	DispStr
	add	esp, 4

	ret
; DispReturn 结束---------------------------------------------------------


; ------------------------------------------------------------------------
; 内存拷贝函数 memcpy
; ------------------------------------------------------------------------
MemCpy:
	push	ebp
	mov	ebp, esp

	push	esi
	push	edi
	push	ecx

	mov	edi, [ebp + 8]	; Destination
	mov	esi, [ebp + 12]	; Source
	mov	ecx, [ebp + 16]	; Counter
.1:
	cmp	ecx, 0
	jz	.2

	mov	al, [ds:esi]
	inc	esi
	mov	byte [es:edi], al
	inc	edi

	dec	ecx
	jmp	.1
.2:
	mov	eax, [ebp + 8]

	pop	ecx
	pop	edi
	pop	esi
	mov	esp, ebp
	pop	ebp

	ret
; MemCpy 结束-------------------------------------------------------------



; 显示内存信息 --------------------------------------------------------------
DispMemInfo:
	push	esi
	push	edi
	push	ecx

	mov	esi, MemChkBuf
	mov	ecx, [dwMCRNumber]
.loop:
	mov	edx, 5
	mov	edi, ARDStruct
.1:
	push	dword [esi]
	call	DispInt
	pop	eax
	stosd
	add	esi, 4
	dec	edx
	cmp	edx, 0
	jnz	.1
	call	DispReturn
	cmp	dword [dwType], 1
	jne	.2
	mov	eax, [dwBaseAddrLow]
	add	eax, [dwLengthLow]
	cmp	eax, [dwMemSize]
	jb	.2
	mov	[dwMemSize], eax
.2:
	loop	.loop

	call	DispReturn
	push	szRAMSize
	call	DispStr
	add	esp, 4

	push	dword [dwMemSize]
	call	DispInt
	add	esp, 4

	pop	ecx
	pop	edi
	pop	esi
	ret
; ---------------------------------------------------------------------------

; 启动分页功能 --------------------------------------------------------------
SetupPaging:
	; 根据内存大小计算应初始化多少PDE以及多少页表
	xor	edx, edx
	mov	eax, [dwMemSize]
	mov	ebx, 400000h	; 4M
	div	ebx
	mov	ecx, eax
	test	edx, edx
	jz	.no_remainder
	inc	ecx
.no_remainder:
	; 保底: 至少映射前16MB
	cmp	ecx, 4
	jge	.paging_ok
	mov	ecx, 4
.paging_ok:
	push	ecx

	; 初始化页目录
	mov	ax, SelectorFlatRW
	mov	es, ax
	mov	edi, PageDirBase
	xor	eax, eax
	mov	eax, PageTblBase | PG_P  | PG_USU | PG_RWW
.1:
	stosd
	add	eax, 4096
	loop	.1

	; 初始化所有页表
	pop	eax
	mov	ebx, 1024
	mul	ebx
	mov	ecx, eax
	mov	edi, PageTblBase
	xor	eax, eax
	mov	eax, PG_P  | PG_USU | PG_RWW
.2:
	stosd
	add	eax, 4096
	loop	.2

	; ---- 额外映射 VBE LFB 区域 (如果是 VBE 模式) ----
	mov	al, [0504h]
	cmp	al, 2			; VBE_LFB 模式?
	jnz	.SetCR3			; 不是, 跳过

	; 读取 LFB 基址
	mov	eax, [0506h]		; LFB 物理基址
	; 计算 LFB 所在的 4MB 对齐地址 (PDE 索引)
	shr	eax, 22			; eax = PDE 索引 (bit 22-31)
	push	eax			; ★ 保存 PDE 索引到栈上 (避免后面被破坏)

	; 计算现有页表总数量
	mov	eax, [dwMemSize]
	mov	ebx, 400000h	; 4M
	xor	edx, edx
	div	ebx
	test	edx, edx
	jz	.LFB_PagingOK
	inc	eax
.LFB_PagingOK:
	cmp	eax, 4
	jge	.LFB_HasCount
	mov	eax, 4
.LFB_HasCount:
	; eax = PDE 数量 (即页表数量)
	mov	ebx, 4096
	mul	ebx			; eax = 现有页表总大小
	add	eax, PageTblBase	; eax = 新页表的起始地址
	mov	ecx, eax		; ecx = 新页表地址

	; 在页目录中添加 PDE
	pop	edx			; ★ 从栈上恢复 PDE 索引
	mov	edi, PageDirBase
	mov	eax, edx		; eax = PDE 索引
	shl	eax, 2			; eax = PDE 偏移 (索引 * 4)
	add	edi, eax
	mov	edx, ecx
	or	edx, PG_P | PG_USU | PG_RWW
	mov	[edi], edx

	; 初始化新页表 (映射 4MB = 1024 页)
	mov	edi, ecx		; edi = 页表地址
	mov	eax, [0506h]		; eax = LFB 基址
	and	eax, 0xFFC00000	; 4MB 对齐
	or	eax, PG_P | PG_USU | PG_RWW
	mov	ecx, 1024		; 1024 个 PTE = 4MB
.MapLFBPage:
	stosd
	add	eax, 4096
	loop	.MapLFBPage

.SetCR3:
	mov	eax, PageDirBase
	mov	cr3, eax
	mov	eax, cr0
	or	eax, 80000000h
	mov	cr0, eax
	jmp	short .3
.3:
	nop

	ret
; 启动分页功能结束 ----------------------------------------------------------



; InitKernel ---------------------------------------------------------------------------------
; 将 KERNEL.BIN 从临时缓冲区 (0x10000) 复制到内核执行地址 (0x30400)
; 自动检测 ELF / Flat Binary 格式
; 0x10000 处保留 kernel.bin 原始副本供 setup 命令使用
; --------------------------------------------------------------------------------------------
KERNEL_TEMP_ADDR	equ	010000h

InitKernel:
	xor	esi, esi
	; 检查 ELF magic: 0x7F 'E' 'L' 'F'
	mov	eax, dword [KERNEL_TEMP_ADDR]
	cmp	eax, 0464C457Fh
	je	.ELFFormat

	; === Flat Binary 格式 ===
	; 注意: 源(0x10000)与目的(0x30400)在大容量复制时会产生重叠
	; (源终点 0x50000 > 目的起点 0x30400), 必须用 STD 向后复制避免数据腐败
	mov	esi, KERNEL_TEMP_ADDR + 262144 - 4
	mov	edi, KernelEntryPointPhyAddr + 262144 - 4
	mov	ecx, 262144 / 4	; 512 扇区 × 512 字节 / 4 = 65536 dwords
	std
	rep	movsd
	cld
	
	cmp	byte [KernelEntryPointPhyAddr], 0
	jne	.KernelOK
	jmp	.InitKernelDone

.ELFFormat:
	; === ELF 格式: 解析程序头 ===
	mov	cx, word [KERNEL_TEMP_ADDR + 2Ch]
	movzx	ecx, cx
	mov	esi, [KERNEL_TEMP_ADDR + 1Ch]
	add	esi, KERNEL_TEMP_ADDR
.Begin:
	mov	eax, [esi + 0]
	cmp	eax, 0
	jz	.NoAction
	push	dword [esi + 010h]
	mov	eax, [esi + 04h]
	add	eax, KERNEL_TEMP_ADDR
	push	eax
	push	dword [esi + 08h]
	call	MemCpy
	add	esp, 12
.NoAction:
	add	esi, 020h
	dec	ecx
	jnz	.Begin

.KernelOK:

.InitKernelDone:
	ret
; InitKernel ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


; SECTION .data1 之开始 ---------------------------------------------------------------------------------------------
[SECTION .data1]

ALIGN	32

LABEL_DATA:
; 实模式下使用这些变量
; 字符串
_szBootOK:	db	"[INFO] Entered protected mode", 0Ah, 0
_szKernelLoaded:	db	"[INFO] Kernel loaded at ", 0
_szMemChkTitle:			db	"[INFO] BaseAddrL BaseAddrH LengthLow LengthHigh   Type", 0Ah, 0
_szRAMSize:			db	"RAM size:", 0
_szReturn:			db	0Ah, 0
;; 变量
_dwMCRNumber:			dd	0
_dwDispPos:			dd	(80 * 6 + 0) * 2
_dwMemSize:			dd	0
_ARDStruct:
	_dwBaseAddrLow:		dd	0
	_dwBaseAddrHigh:	dd	0
	_dwLengthLow:		dd	0
	_dwLengthHigh:		dd	0
	_dwType:		dd	0
_SMAP_SIGNATURE:	dd	0
_kernel_lba_temp:	dd	0
_MemChkBuf:	times	256	db	0
_VBE_ModeInfo:	times	256	db	0	; VBE Mode Info Block (256 bytes)
_VBE_CtrlInfo:	times	512	db	0	; VBE Controller Info Block (512 bytes)
;
; 保护模式下使用这些变量
szBootOK		equ	BaseOfLoaderPhyAddr + _szBootOK
szKernelLoaded		equ	BaseOfLoaderPhyAddr + _szKernelLoaded
szMemChkTitle		equ	BaseOfLoaderPhyAddr + _szMemChkTitle
szRAMSize		equ	BaseOfLoaderPhyAddr + _szRAMSize
szReturn		equ	BaseOfLoaderPhyAddr + _szReturn
dwDispPos		equ	BaseOfLoaderPhyAddr + _dwDispPos
dwMemSize		equ	BaseOfLoaderPhyAddr + _dwMemSize
dwMCRNumber		equ	BaseOfLoaderPhyAddr + _dwMCRNumber
ARDStruct		equ	BaseOfLoaderPhyAddr + _ARDStruct
	dwBaseAddrLow	equ	BaseOfLoaderPhyAddr + _dwBaseAddrLow
	dwBaseAddrHigh	equ	BaseOfLoaderPhyAddr + _dwBaseAddrHigh
	dwLengthLow	equ	BaseOfLoaderPhyAddr + _dwLengthLow
	dwLengthHigh	equ	BaseOfLoaderPhyAddr + _dwLengthHigh
	dwType		equ	BaseOfLoaderPhyAddr + _dwType
MemChkBuf		equ	BaseOfLoaderPhyAddr + _MemChkBuf


; 栈在数据段的末尾
StackSpace:	times	1000h	db	0
TopOfStack	equ	BaseOfLoaderPhyAddr + $
; SECTION .data1 之结束 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
