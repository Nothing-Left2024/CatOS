
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                               kernel.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                                                     Forrest Yu, 2005
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


%include "sconst.inc"

; ===== 段选择器定义 (与Loader的GDT对应) =====
; GDT索引: 0=Null, 1=FlatC(0x08), 2=FlatRW(0x10), 3=Video(0x18)
;         4=TSS(0x20), 5+=LDTs(0x28+)
SELECTOR_FLAT_C		equ	0x08	; 内核代码段 (=SELECTOR_KERNEL_CS)
SELECTOR_FLAT_RW	equ	0x10	; 内核数据段
SELECTOR_VIDEO		equ	0x1B	; 显存段 (Index3 + RPL3)
SELECTOR_TSS		equ	0x20	; TSS段 (Index4)

; 导入函数
extern	cstart
extern	tinix_main
extern	exception_handler
extern	spurious_irq
extern	clock_handler
extern	disp_str
extern	delay

; 导入全局变量
extern	gdt_ptr
extern	idt_ptr
extern	p_proc_ready
extern	tss
extern	disp_pos
extern	k_reenter
extern	irq_table
extern	sys_call_table
extern	__bss_start__
extern	__bss_end__

bits 32

[SECTION .data]
BootMsg_Start	db	' _start: kernel entry', 0
szCStartOK		db	'[OK] cstart() returned', 0Ah, 0
clock_int_msg		db	"^", 0

; ===== Loader GDT 保存区 (BSS 清零会覆盖 0x90000 处的 Loader GDT) =====
saved_gdt_ptr	times	8	db	0	; sgdt 保存: 2字节limit + 4字节base
saved_gdt	times	64	db	0	; GDT 内容副本 (Loader GDT 仅3~4个描述符)

; ===== 嵌入的引导二进制 (供 setup 命令写入硬盘) =====
global	_hdboot_bin
global	_hdboot_bin_end
global	_loader_bin
global	_loader_bin_end
global	_mbr_bin
global	_mbr_bin_end

_hdboot_bin:
	incbin	"boot/hdboot.bin"
_hdboot_bin_end:

_loader_bin:
	incbin	"boot/loader.bin"
_loader_bin_end:

_mbr_bin:
	incbin	"boot/mbr.bin"
_mbr_bin_end:

[SECTION .bss]
StackSpace		resb	2 * 1024
StackTop:		; 栈顶

[section .text]	; 代码段内存

global _start	; 导出 _start

global	restart
global	sys_call

global	divide_error
global	single_step_exception
global	nmi
global	breakpoint_exception
global	overflow
global	bounds_check
global	inval_opcode
global	copr_not_available
global	double_fault
global	copr_seg_overrun
global	inval_tss
global	segment_not_present
global	stack_exception
global	general_protection
global	page_fault
global	copr_error
global	hwint00
global	hwint01
global	hwint02
global	hwint03
global	hwint04
global	hwint05
global	hwint06
global	hwint07
global	hwint08
global	hwint09
global	hwint10
global	hwint11
global	hwint12
global	hwint13
global	hwint14
global	hwint15


_start:
	mov	esp, 0x7C00

	mov	ax, SELECTOR_FLAT_RW
	mov	ds, ax
	mov	es, ax

	mov	ax, SELECTOR_VIDEO
	mov	gs, ax

	; ===== 保存 Loader 的 GDT 到 .data 缓冲区 =====
	; 原因: BSS 范围 [0x47014, 0x9CCC0) 覆盖了 Loader 的 GDT (位于 0x90000 附近)。
	; 若不保存, BSS 清零会把 Loader GDT 清成全0, 之后 cstart 的 memcpy 会拷贝全0,
	; 导致 GDT[1](FlatC) 变成 null 描述符, jmp 0x08:csinit 触发 #GP → 三重故障。
	sgdt	[saved_gdt_ptr]			; 保存 GDT 寄存器 (limit + base, 共6字节)
	movzx	ecx, word [saved_gdt_ptr]	; ecx = GDT limit
	inc	ecx				; ecx = limit + 1 = 字节数
	mov	esi, [saved_gdt_ptr + 2]	; esi = GDT 基地址 (0x90000 附近)
	mov	edi, saved_gdt			; edi = .data 中的保存缓冲区
	rep	movsb

	; 清零 BSS 段 (objcopy 不输出 .bss, 内存中为垃圾数据)
	; fat_initialized / fat_info / proc_table 等全局变量依赖此步骤
	mov	edi, __bss_start__
	mov	ecx, __bss_end__
	sub	ecx, edi
	xor	eax, eax
	add	ecx, 3
	shr	ecx, 2
	rep	stosd

	; ===== 恢复 Loader 的 GDT (BSS 清零已覆盖 0x90000 处) =====
	movzx	ecx, word [saved_gdt_ptr]	; ecx = GDT limit
	inc	ecx				; ecx = limit + 1
	mov	esi, saved_gdt			; esi = 保存的副本
	mov	edi, [saved_gdt_ptr + 2]	; edi = 原 GDT 地址
	rep	movsb

	mov	dword [disp_pos], (80 * 3) * 2

	sgdt	[gdt_ptr]

	call	cstart

	lgdt	[gdt_ptr]

	lidt	[idt_ptr]

	jmp	SELECTOR_FLAT_C:csinit

csinit:
	xor	eax, eax
	mov	ax, SELECTOR_TSS
	ltr	ax

	mov	esp, StackTop

	jmp	tinix_main


; 中断和异常 -- 硬件中断
; ---------------------------------
%macro	hwint_master	1
	call	save
	in	al, INT_M_CTLMASK	; ┓
	or	al, (1 << %1)		; ┃ 屏蔽当前中断
	out	INT_M_CTLMASK, al	; ┛
	mov	al, EOI			; 设置EOI位
	out	INT_M_CTL, al		; ┓
	sti	; CPU在响应中断的过程中会自动关闭中断，之后才允许响应新的中断
	push	%1			; ┓
	call	[irq_table + 4 * %1]	; ┃ 中断处理程序
	pop	ecx			; ┛
	cli
	in	al, INT_M_CTLMASK	; ┓
	and	al, ~(1 << %1)		; ┃ 恢复允许当前中断
	out	INT_M_CTLMASK, al	; ┛
	ret
%endmacro



ALIGN	16
hwint00:		; Interrupt routine for irq 0 (the clock).
	hwint_master	0

ALIGN	16
hwint01:		; Interrupt routine for irq 1 (keyboard)
	hwint_master	1

ALIGN	16
hwint02:		; Interrupt routine for irq 2 (cascade!)
	hwint_master	2

ALIGN	16
hwint03:		; Interrupt routine for irq 3 (second serial)
	hwint_master	3

ALIGN	16
hwint04:		; Interrupt routine for irq 4 (first serial)
	hwint_master	4

ALIGN	16
hwint05:		; Interrupt routine for irq 5 (XT winchester)
	hwint_master	5

ALIGN	16
hwint06:		; Interrupt routine for irq 6 (floppy)
	hwint_master	6

ALIGN	16
hwint07:		; Interrupt routine for irq 7 (printer)
	hwint_master	7

; ---------------------------------
%macro	hwint_slave	1
	call	save
	in	al, INT_S_CTLMASK	; ┓
	or	al, (1 << (%1 - 8))	; ┃ 屏蔽当前中断
	out	INT_S_CTLMASK, al	; ┛
	mov	al, EOI			; 设置EOI位
	out	INT_S_CTL, al		; ┓ 先向从片发EOI
	out	INT_M_CTL, al		; ┃ 再向主片发EOI (级联中断必须同时通知主片, 否则主片IR2 ISR位不清, 阻塞后续从片中断)
	sti	; CPU在响应中断的过程中会自动关闭中断，之后才允许响应新的中断
	push	%1			; ┓
	call	[irq_table + 4 * %1]	; ┃ 中断处理程序
	pop	ecx			; ┛
	cli
	in	al, INT_S_CTLMASK	; ┓
	and	al, ~(1 << (%1 - 8))	; ┃ 恢复允许当前中断
	out	INT_S_CTLMASK, al	; ┛
	ret
%endmacro
; ---------------------------------

ALIGN	16
hwint08:		; Interrupt routine for irq 8 (realtime clock).
	hwint_slave	8

ALIGN	16
hwint09:		; Interrupt routine for irq 9 (irq 2 redirected)
	hwint_slave	9

ALIGN	16
hwint10:		; Interrupt routine for irq 10
	hwint_slave	10

ALIGN	16
hwint11:		; Interrupt routine for irq 11
	hwint_slave	11

ALIGN	16
hwint12:		; Interrupt routine for irq 12
	hwint_slave	12

ALIGN	16
hwint13:		; Interrupt routine for irq 13 (FPU exception)
	hwint_slave	13

ALIGN	16
hwint14:		; Interrupt routine for irq 14 (AT winchester)
	hwint_slave	14

ALIGN	16
hwint15:		; Interrupt routine for irq 15
	hwint_slave	15



; 中断和异常 -- 异常
divide_error:
	push	0xFFFFFFFF	; no err code
	push	0		; vector_no	= 0
	jmp	exception
single_step_exception:
	push	0xFFFFFFFF	; no err code
	push	1		; vector_no	= 1
	jmp	exception
nmi:
	push	0xFFFFFFFF	; no err code
	push	2		; vector_no	= 2
	jmp	exception
breakpoint_exception:
	push	0xFFFFFFFF	; no err code
	push	3		; vector_no	= 3
	jmp	exception
overflow:
	push	0xFFFFFFFF	; no err code
	push	4		; vector_no	= 4
	jmp	exception
bounds_check:
	push	0xFFFFFFFF	; no err code
	push	5		; vector_no	= 5
	jmp	exception
inval_opcode:
	push	0xFFFFFFFF	; no err code
	push	6		; vector_no	= 6
	jmp	exception
copr_not_available:
	push	0xFFFFFFFF	; no err code
	push	7		; vector_no	= 7
	jmp	exception
double_fault:
	push	8		; vector_no	= 8
	jmp	exception
copr_seg_overrun:
	push	0xFFFFFFFF	; no err code
	push	9		; vector_no	= 9
	jmp	exception
inval_tss:
	push	10		; vector_no	= A
	jmp	exception
segment_not_present:
	push	11		; vector_no	= B
	jmp	exception
stack_exception:
	push	12		; vector_no	= C
	jmp	exception
general_protection:
	push	13		; vector_no	= D
	jmp	exception
page_fault:
	push	14		; vector_no	= E
	jmp	exception
copr_error:
	push	0xFFFFFFFF	; no err code
	push	16		; vector_no	= 10h
	jmp	exception

exception:
	; 注意: 不能 push/pop gs —— 用户进程 gs=0, 若 pop gs 恢复成 0,
	; 后续 exception_handler 内的 disp_str 会用 gs 访问显存触发 #GP,
	; 形成异常嵌套 → 三重故障 → CPU 关闭。整段处理期间保持 gs=SELECTOR_VIDEO。
	push	eax
	mov	ax, SELECTOR_VIDEO
	mov	gs, ax
	pop	eax
	call	exception_handler
	add	esp, 4*2	; 栈指针指向 EIP，栈中从下到上依次为：EIP、CS、EFLAGS
	cli
.halt:
	hlt
	jmp	.halt

; ====================================================================================
;                                   save
; ====================================================================================
save:
	pushad		; ┓
	push	ds	; ┃
	push	es	; ┃ 保存原寄存器值
	push	fs	; ┃
	push	gs	; ┛
	mov	dx, ss
	mov	ds, dx
	mov	es, dx
	mov	fs, dx

	mov	esi, esp			; esi = 进程表起始地址

	inc	dword [k_reenter]		; k_reenter++;
	cmp	dword [k_reenter], 0		; if(k_reenter ==0)
	jne	.1				; {
	mov	esp, StackTop			;	mov esp, StackTop <-- 切换到内核栈
	push	restart				;	push restart
	jmp	[esi + RETADR - P_STACKBASE]	;	return;
.1:						; } else { 已经在内核栈，不需要切换
	push	restart_reenter			;	push restart_reenter
	jmp	[esi + RETADR - P_STACKBASE]	;	return;
						; }


; ====================================================================================
;                                 sys_call
; ====================================================================================
sys_call:
	call	save

	push	dword [p_proc_ready]

	sti

	push	ecx
	push	ebx
	call	[sys_call_table + eax * 4]
	add	esp, 4 * 3

	mov	[esi + EAXREG - P_STACKBASE], eax

	cli

	ret


; ====================================================================================
;                                   restart
; ====================================================================================
restart:
	mov	esp, [p_proc_ready]
	lldt	[esp + P_LDT_SEL]

	lea	eax, [esp + P_STACKTOP]
	mov	dword [tss + TSS3_S_SP0], eax
restart_reenter:
	dec	dword [k_reenter]

	pop	gs
	pop	fs
	pop	es
	pop	ds
	popad
	add	esp, 4
	iretd


