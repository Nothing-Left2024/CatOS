
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                               syscall.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                                                     Forrest Yu, 2005
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

%include "sconst.inc"

INT_VECTOR_SYS_CALL	equ	0x90
_NR_get_ticks		equ	0
_NR_write		equ	1
_NR_putc		equ	2
_NR_puts		equ	3
_NR_getc		equ	4
_NR_exit		equ	5
_NR_clrscr		equ	6
_NR_gotoxy		equ	7
_NR_get_xy		equ	8
_NR_get_cols		equ	9
_NR_get_rows		equ	10
_NR_get_vmode		equ	11
_NR_set_color		equ	12
_NR_putc_color		equ	13
_NR_get_pid		equ	14
_NR_get_name		equ	15
_NR_rand		equ	16
_NR_srand		equ	17
_NR_put_int		equ	18
_NR_put_uint		equ	19
_NR_put_hex		equ	20
_NR_put_bin		equ	21
_NR_delay		equ	22
_NR_getch		equ	23
_NR_kbhit		equ	24
_NR_scroll		equ	25


; 导出函数
global	get_ticks
global	write
global	putc
global	puts
global	getc
global	exit
global	clrscr
global	gotoxy
global	get_xy
global	get_cols
global	get_rows
global	get_vmode
global	set_color
global	putc_color
global	get_pid
global	get_name
global	rand
global	srand
global	put_int
global	put_uint
global	put_hex
global	put_bin
global	msleep
global	getch
global	kbhit
global	scroll


bits 32
[section .text]

; 注意：dx 的值在 save() 中会被改变，所以传参数不能使用 edx。

; ====================================================================================
;                                    get_ticks
; ====================================================================================
get_ticks:
	mov	eax, _NR_get_ticks
	int	INT_VECTOR_SYS_CALL
	ret

; ====================================================================================
;                          void write(char* buf, int len);
; ====================================================================================
write:
	mov	eax, _NR_write
	mov	ebx, [esp + 4]
	mov	ecx, [esp + 8]
	int	INT_VECTOR_SYS_CALL
	ret

; ====================================================================================
;                              void putc(int ch);
; ====================================================================================
putc:
	mov	eax, _NR_putc
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; ====================================================================================
;                              void puts(char* str);
; ====================================================================================
puts:
	mov	eax, _NR_puts
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; ====================================================================================
;                              int getc(void);
; ====================================================================================
getc:
	mov	eax, _NR_getc
	int	INT_VECTOR_SYS_CALL
	ret

; ====================================================================================
;                              void exit(int code);  (不返回)
; ====================================================================================
exit:
	mov	eax, _NR_exit
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	; sys_exit 调用 restart() 切换上下文, 永不返回
	jmp	$

; ====================================================================================
;  扩展 API (NR 6-17) — 内核侧包装, 与 sdk/crt0.asm 中用户侧包装一一对应
; ====================================================================================

; void clrscr(void)
clrscr:
	mov	eax, _NR_clrscr
	int	INT_VECTOR_SYS_CALL
	ret

; void gotoxy(int x, int y)
gotoxy:
	mov	eax, _NR_gotoxy
	mov	ebx, [esp + 4]
	mov	ecx, [esp + 8]
	int	INT_VECTOR_SYS_CALL
	ret

; int get_xy(void) -> (x & 0xFFFF) | (y << 16)
get_xy:
	mov	eax, _NR_get_xy
	int	INT_VECTOR_SYS_CALL
	ret

; int get_cols(void)
get_cols:
	mov	eax, _NR_get_cols
	int	INT_VECTOR_SYS_CALL
	ret

; int get_rows(void)
get_rows:
	mov	eax, _NR_get_rows
	int	INT_VECTOR_SYS_CALL
	ret

; int get_vmode(void)
get_vmode:
	mov	eax, _NR_get_vmode
	int	INT_VECTOR_SYS_CALL
	ret

; void set_color(int color)
set_color:
	mov	eax, _NR_set_color
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; void putc_color(int ch, int color)
putc_color:
	mov	eax, _NR_putc_color
	mov	ebx, [esp + 4]
	mov	ecx, [esp + 8]
	int	INT_VECTOR_SYS_CALL
	ret

; int get_pid(void)
get_pid:
	mov	eax, _NR_get_pid
	int	INT_VECTOR_SYS_CALL
	ret

; int get_name(char* buf)
get_name:
	mov	eax, _NR_get_name
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; int rand(void)
rand:
	mov	eax, _NR_rand
	int	INT_VECTOR_SYS_CALL
	ret

; void srand(int seed)
srand:
	mov	eax, _NR_srand
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; ====================================================================================
;  扩展 API (NR 18-25) — 整数输出 / 延时 / 阻塞输入 / 滚屏
; ====================================================================================

; void put_int(int n)
put_int:
	mov	eax, _NR_put_int
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; void put_uint(unsigned int n)
put_uint:
	mov	eax, _NR_put_uint
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; void put_hex(unsigned int n, int upper)
put_hex:
	mov	eax, _NR_put_hex
	mov	ebx, [esp + 4]
	mov	ecx, [esp + 8]
	int	INT_VECTOR_SYS_CALL
	ret

; void put_bin(unsigned int n)
put_bin:
	mov	eax, _NR_put_bin
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; void msleep(int ms)
msleep:
	mov	eax, _NR_delay
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

; int getch(void)
getch:
	mov	eax, _NR_getch
	int	INT_VECTOR_SYS_CALL
	ret

; int kbhit(void)
kbhit:
	mov	eax, _NR_kbhit
	int	INT_VECTOR_SYS_CALL
	ret

; void scroll(int lines)
scroll:
	mov	eax, _NR_scroll
	mov	ebx, [esp + 4]
	int	INT_VECTOR_SYS_CALL
	ret

