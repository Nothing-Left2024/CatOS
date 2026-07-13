
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                              klib.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                                                       Forrest Yu, 2005
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


%include "sconst.inc"

; ===== 全局变量引用 =====
extern	disp_pos


[SECTION .text]

; ===== 导出函数列表 =====
global	disp_str
global	disp_color_str
global	clear_screen
global	vm_putc_asm
global	vm_getc
global	vm_get_attr
global	out_byte
global	in_byte
global	enable_irq
global	disable_irq
global	enable_int
global	disable_int

; ========================================================================
;                  void disp_str(char * info);
; ========================================================================
disp_str:
	push	ebp
	mov	ebp, esp

	; ★ 图形模式下直接返回, 避免文本模式输出污染图形显存
	mov	al, [0x504]
	test	al, al
	jnz	.disp_str_exit

	mov	esi, [ebp + 8]	; pszInfo
	mov	edi, [disp_pos]
	mov	ah, 0Fh
.1:
	lodsb
	test	al, al
	jz	.2
	cmp	al, 0Ah	; 是回车吗?
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
	mov	[disp_pos], edi

.disp_str_exit:
	pop	ebp
	ret

; ========================================================================
;                  void disp_color_str(char * info, int color);
; ========================================================================
disp_color_str:
	push	ebp
	mov	ebp, esp

	; ★ 图形模式下直接返回, 避免文本模式输出污染图形显存
	mov	al, [0x504]
	test	al, al
	jnz	.disp_color_str_exit

	mov	esi, [ebp + 8]	; pszInfo
	mov	edi, [disp_pos]
	mov	ah, [ebp + 12]	; color
.1:
	lodsb
	test	al, al
	jz	.2
	cmp	al, 0Ah	; 是回车吗?
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
	mov	[disp_pos], edi

.disp_color_str_exit:
	pop	ebp
	ret

; ========================================================================
;                  void clear_screen(void);
;
;  清屏: 用空格填充全部 80x25 显存
;  重置 disp_pos = 0 (光标回到左上角)
;  前提: GS 必须已设置为 Video 段选择器 (0x1B)
;
;  注意: 必须用 [gs:edi] 显式指定GS段!
;        rep stosw 默认写到 ES:EDI (不是显存!)
; ========================================================================
clear_screen:
	pushad

	mov	edi, 0			; 从显存偏移0开始
	mov	ax, 0x0720		; 空格(0x20) + 白色属性(0x07)
	mov	cx, 80 * 25		; 共2000个字符单元
.cls_loop:
	mov	[gs:edi], ax		; ★ 显式GS段! 写入显存
	add	edi, 2
	loop	.cls_loop

	mov	dword [disp_pos], 0	; 光标归位到左上角

	popad
	ret

; ========================================================================
;                  void vm_putc_asm(int pos, char ch, int color);
;
;  在显存指定位置(pos字节偏移)写入一个字符 (文本模式底层)
;  ★ 保存/恢复EDI (被调用者保存寄存器!)
; ========================================================================
vm_putc_asm:
	push	edi
	mov	edi, [esp + 8]	; pos (跳过push edi 4B + ret addr 4B)
	movzx	eax, byte [esp + 12]	; ch
	mov	ah, [esp + 16]	; color
	mov	[gs:edi], ax
	pop	edi
	ret

; ========================================================================
;                  char vm_getc(int pos);
;
;  从显存指定位置(pos字节偏移)读取字符部分
;  返回: AL = 字符
; ========================================================================
vm_getc:
	push	edi
	mov	edi, [esp + 8]	; pos
	movzx	eax, byte [gs:edi]	; 读取字符部分
	pop	edi
	ret

; ========================================================================
;                  unsigned char vm_get_attr(int pos);
;
;  从显存指定位置(pos字节偏移)读取属性字节
;  返回: AL = 属性字节 (颜色)
; ========================================================================
vm_get_attr:
	push	edi
	mov	edi, [esp + 8]	; pos
	movzx	eax, byte [gs:edi + 1]	; 读取属性部分
	pop	edi
	ret
; ========================================================================
out_byte:
	mov	edx, [esp + 4]		; port
	mov	al, [esp + 4 + 4]	; value
	out	dx, al
	nop	; 一点延迟
	nop
	ret

; ========================================================================
;                  t_8 in_byte(t_port port);
; ========================================================================
in_byte:
	mov	edx, [esp + 4]		; port
	xor	eax, eax
	in	al, dx
	nop	; 一点延迟
	nop
	ret

; ========================================================================
;                  void disable_irq(int irq);
; ========================================================================
; Disable an interrupt request line by setting an 8259 bit.
; Equivalent code for irq < 8:
;       out_byte(INT_CTLMASK, in_byte(INT_CTLMASK) | (1 << irq));
; Returns true iff the interrupt was not already disabled.
;
disable_irq:
	mov	ecx, [esp + 4]		; irq
	pushf
	cli
	mov	ah, 1
	rol	ah, cl			; ah = (1 << (irq % 8))
	cmp	cl, 8
	jae	disable_8		; disable irq >= 8 at the slave 8259
disable_0:
	in	al, INT_M_CTLMASK
	test	al, ah
	jnz	dis_already		; already disabled?
	or	al, ah
	out	INT_M_CTLMASK, al	; set bit at master 8259
	popf
	mov	eax, 1			; disabled by this function
	ret
disable_8:
	in	al, INT_S_CTLMASK
	test	al, ah
	jnz	dis_already		; already disabled?
	or	al, ah
	out	INT_S_CTLMASK, al	; set bit at slave 8259
	popf
	mov	eax, 1			; disabled by this function
	ret
dis_already:
	popf
	xor	eax, eax		; already disabled
	ret

; ========================================================================
;                  void enable_irq(int irq);
; ========================================================================
; Enable an interrupt request line by clearing an 8259 bit.
; Equivalent code:
;	if(irq < 8){
;		out_byte(INT_M_CTLMASK, in_byte(INT_M_CTLMASK) & ~(1 << irq));
;	}
;	else{
;		out_byte(INT_S_CTLMASK, in_byte(INT_S_CTLMASK) & ~(1 << irq));
;	}
;
enable_irq:
        mov	ecx, [esp + 4]		; irq
        pushf
        cli
        mov	ah, ~1
        rol	ah, cl			; ah = ~(1 << (irq % 8))
        cmp	cl, 8
        jae	enable_8		; enable irq >= 8 at the slave 8259
enable_0:
        in	al, INT_M_CTLMASK
        and	al, ah
        out	INT_M_CTLMASK, al	; clear bit at master 8259
        popf
        ret
enable_8:
        in	al, INT_S_CTLMASK
        and	al, ah
        out	INT_S_CTLMASK, al	; clear bit at slave 8259
        popf
        ret

; ========================================================================
;                  void disable_int();
; ========================================================================
disable_int:
	cli
	ret

; ========================================================================
;                  void enable_int();
; ========================================================================
enable_int:
	sti
	ret

