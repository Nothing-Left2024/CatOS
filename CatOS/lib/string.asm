
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                              string.asm
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
;                                                       Forrest Yu, 2005
; ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

[SECTION .text]

; 导出函数
global	memcpy
global	memset
global	strcpy
global	strlen
global	strcmp


; ------------------------------------------------------------------------
; void* memcpy(void* es:p_dst, void* ds:p_src, int size);
; ------------------------------------------------------------------------
memcpy:
	push	ebp
	mov	ebp, esp

	push	esi
	push	edi
	push	ecx

	mov	edi, [ebp + 8]	; Destination
	mov	esi, [ebp + 12]	; Source
	mov	ecx, [ebp + 16]	; Counter
.1:
	cmp	ecx, 0		; 判断计数器
	jz	.2		; 计数器为零时跳出

	mov	al, [ds:esi]		; ┓
	inc	esi			; ┃
					; ┣ 逐字节移动
	mov	byte [es:edi], al	; ┃
	inc	edi			; ┛

	dec	ecx		; 计数器减一
	jmp	.1		; 循环
.2:
	mov	eax, [ebp + 8]	; 返回值

	pop	ecx
	pop	edi
	pop	esi
	mov	esp, ebp
	pop	ebp

	ret			; 返回调用者指令
; memcpy 结束-------------------------------------------------------------


; ------------------------------------------------------------------------
; void memset(void* p_dst, char ch, int size);
; ------------------------------------------------------------------------
memset:
	push	ebp
	mov	ebp, esp

	push	esi
	push	edi
	push	ecx

	mov	edi, [ebp + 8]	; Destination
	mov	edx, [ebp + 12]	; Char to be putted
	mov	ecx, [ebp + 16]	; Counter
.1:
	cmp	ecx, 0		; 判断计数器
	jz	.2		; 计数器为零时跳出

	mov	byte [edi], dl		; ┓
	inc	edi			; ┛

	dec	ecx		; 计数器减一
	jmp	.1		; 循环
.2:

	pop	ecx
	pop	edi
	pop	esi
	mov	esp, ebp
	pop	ebp

	ret			; 返回调用者指令
; memset 结束-------------------------------------------------------------


; ------------------------------------------------------------------------
; char* strcpy(char* p_dst, char* p_src);
; ------------------------------------------------------------------------
strcpy:
	push	ebp
	mov	ebp, esp

	mov	esi, [ebp + 12]	; Source
	mov	edi, [ebp + 8]	; Destination

.1:
	mov	al, [esi]		; ┓
	inc	esi			; ┃
					; ┣ 逐字节移动
	mov	byte [edi], al		; ┃
	inc	edi			; ┛

	cmp	al, 0		; 是否遇到 '\0'
	jnz	.1		; 没遇到就继续循环，遇到了就结束

	mov	eax, [ebp + 8]	; 返回值

	pop	ebp
	ret			; 返回调用者指令
; strcpy 结束-------------------------------------------------------------


; ------------------------------------------------------------------------
; int strlen(char* p_str);
; ------------------------------------------------------------------------
strlen:
	push	ebp
	mov	ebp, esp

	mov	eax, 0			; 字符串长度初始为 0
	mov	esi, [ebp + 8]		; esi 指向首地址

.1:
	cmp	byte [esi], 0		; 看 esi 指向的字符是否为 '\0'
	jz	.2			; 如果是 '\0'，结束
	inc	esi			; 如果不是 '\0'，esi 指向下一个字符
	inc	eax			;	  并且，eax 加一
	jmp	.1			; 继续循环

.2:
	pop	ebp
	ret				; 返回调用者指令
; ------------------------------------------------------------------------

; ------------------------------------------------------------------------
; int strcmp(char* s1, char* s2);
; Returns: 0 if equal, >0 if s1 > s2, <0 if s1 < s2
; ------------------------------------------------------------------------
strcmp:
	push	ebp
	mov	ebp, esp

	mov	esi, [ebp + 8]	; s1
	mov	edi, [ebp + 12]	; s2

.1:
	mov	al, [esi]
	mov	bl, [edi]
	cmp	al, bl
	jne	.not_equal
	cmp	al, 0
	jz	.equal
	inc	esi
	inc	edi
	jmp	.1

.not_equal:
	sub	al, bl
	movzx	eax, al
	pop	ebp
	ret

.equal:
	xor	eax, eax
	pop	ebp
	ret
; strcmp -------------------------------------------------------------
; ------------------------------------------------------------------------


