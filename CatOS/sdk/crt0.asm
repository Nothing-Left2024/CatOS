;=================================================================
;                  CatOS User Program C Runtime           crt0.asm
;=================================================================
;                                        Nothing_Left     2026/7/9
; CatOS User Program C Runtime
; 入口 _start: 设置栈, 调用 main, 调用 exit
; 提供 syscall 包装: get_ticks/write/putc/puts/getc/exit

INT_VECTOR_SYS_CALL equ 0x90
NR_get_ticks  equ 0
NR_write      equ 1
NR_putc       equ 2
NR_puts       equ 3
NR_getc       equ 4
NR_exit       equ 5
NR_clrscr     equ 6
NR_gotoxy     equ 7
NR_get_xy     equ 8
NR_get_cols   equ 9
NR_get_rows   equ 10
NR_get_vmode  equ 11
NR_set_color  equ 12
NR_putc_color equ 13
NR_get_pid    equ 14
NR_get_name   equ 15
NR_rand       equ 16
NR_srand      equ 17
NR_put_int    equ 18
NR_put_uint   equ 19
NR_put_hex    equ 20
NR_put_bin    equ 21
NR_delay      equ 22
NR_getch      equ 23
NR_kbhit      equ 24
NR_scroll     equ 25
NR_set_io_size equ 26
NR_file_read   equ 27
NR_file_write  equ 28
NR_file_delete equ 29
NR_file_exists equ 30
NR_file_size   equ 31
NR_file_list   equ 32
NR_reboot      equ 33
NR_get_key_flags equ 34
NR_file_append  equ 35
NR_int_to_str  equ 36
NR_atoi        equ 37
NR_file_stat   equ 38
NR_spawn       equ 39
NR_uc_putc     equ 40
NR_uc_clear    equ 41
NR_win_create  equ 42
NR_win_close   equ 43
NR_win_draw_text equ 44
NR_win_draw_rect equ 45
NR_win_get_event equ 46
NR_win_clear    equ 47
NR_win_set_title equ 48
NR_win_draw_line equ 49
NR_win_set_pixel equ 50
NR_win_peek_event equ 51
NR_win_get_size equ 52
NR_win_set_closable equ 53
NR_win_get_closable equ 54

bits 32
section .text

global _start
global _get_ticks
global _write
global _putc
global _puts
global _getc
global _exit
global _clrscr
global _gotoxy
global _get_xy
global _get_cols
global _get_rows
global _get_vmode
global _set_color
global _putc_color
global _get_pid
global _get_name
global _rand
global _srand
global _put_int
global _put_uint
global _put_hex
global _put_bin
global _msleep
global _getch
global _kbhit
global _scroll
global _set_io_size
global _file_read
global _file_write
global _file_delete
global _file_exists
global _file_size
global _file_list
global _reboot
global _get_key_flags
global _file_append
global _int_to_str
global _atoi
global _file_stat
global _spawn
global _uc_putc
global _uc_clear
global _win_create
global _win_close
global _win_draw_text
global _win_draw_rect
global _win_get_event
global _win_clear
global _win_set_title
global _win_draw_line
global _win_set_pixel
global _win_peek_event
global _win_get_size
global _win_set_closable
global _win_get_closable
; 字符串工具函数
global _strlen
global _strcmp
global _strcpy
global _memcpy
global _memset
global ___main

extern _main

; ___main: GCC -m32 在 main 开头插入 call ___main (C++ 全局构造用)
; 裸机环境无 C 运行时, 提供空实现直接返回
; 注意: GCC 会给符号加 _ 前缀, 所以 __main 变成 ___main (三个下划线)
___main:
    ret

; _start: 程序入口 (CE header.entry 指向这里)
; 栈顶 = 0x1000 (段内偏移, 对应线性 0x101000)
; 注意: 栈必须在程序加载范围内! 程序最大 USER_PROC_MAX_SIZE (64KB)
_start:
    mov     esp, 0x1000
    call    _main
    push    eax             ; main 返回值作为 exit code
    call    _exit
.hang:
    jmp     .hang           ; exit 不返回, 但以防万一

; int get_ticks(void)
_get_ticks:
    mov     eax, NR_get_ticks
    int     INT_VECTOR_SYS_CALL
    ret

; void write(char* buf, int len)
_write:
    mov     eax, NR_write
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; void putc(int ch)
_putc:
    mov     eax, NR_putc
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; void puts(char* str)
_puts:
    mov     eax, NR_puts
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; int getc(void) - 阻塞读取一个字符 (返回 ASCII 0..255)
_getc:
    mov     eax, NR_getc
    int     INT_VECTOR_SYS_CALL
    cmp     eax, -1
    je      _getc          ; 无按键 (-1), 重试
    ret

; void exit(int code) - 不返回
_exit:
    mov     eax, NR_exit
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 扩展 API (NR 6-17) =====

; void clrscr(void)
_clrscr:
    mov     eax, NR_clrscr
    int     INT_VECTOR_SYS_CALL
    ret

; void gotoxy(int x, int y)
_gotoxy:
    mov     eax, NR_gotoxy
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; int get_xy(void)  -> (x & 0xFFFF) | (y << 16)
_get_xy:
    mov     eax, NR_get_xy
    int     INT_VECTOR_SYS_CALL
    ret

; int get_cols(void)
_get_cols:
    mov     eax, NR_get_cols
    int     INT_VECTOR_SYS_CALL
    ret

; int get_rows(void)
_get_rows:
    mov     eax, NR_get_rows
    int     INT_VECTOR_SYS_CALL
    ret

; int get_vmode(void)
_get_vmode:
    mov     eax, NR_get_vmode
    int     INT_VECTOR_SYS_CALL
    ret

; void set_color(int color)
_set_color:
    mov     eax, NR_set_color
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; void putc_color(int ch, int color)
_putc_color:
    mov     eax, NR_putc_color
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; int get_pid(void)
_get_pid:
    mov     eax, NR_get_pid
    int     INT_VECTOR_SYS_CALL
    ret

; int get_name(char* buf)
_get_name:
    mov     eax, NR_get_name
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; int rand(void)
_rand:
    mov     eax, NR_rand
    int     INT_VECTOR_SYS_CALL
    ret

; void srand(int seed)
_srand:
    mov     eax, NR_srand
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 扩展 API (NR 18-25) =====

; void put_int(int n)
_put_int:
    mov     eax, NR_put_int
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; void put_uint(unsigned int n)
_put_uint:
    mov     eax, NR_put_uint
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; void put_hex(unsigned int n, int upper)
_put_hex:
    mov     eax, NR_put_hex
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; void put_bin(unsigned int n)
_put_bin:
    mov     eax, NR_put_bin
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; void msleep(int ms)
_msleep:
    mov     eax, NR_delay
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; int getch(void)
;   控制台模式: 内核非阻塞返回 (0=无按键), 这里循环重试.
;   用户态循环可被时钟中断打断并正常调度 (避免内核忙等死锁).
_getch:
    mov     eax, NR_getch
    int     INT_VECTOR_SYS_CALL
    test    eax, eax
    jz      _getch          ; 无按键, 重试
    ret

; int kbhit(void)
_kbhit:
    mov     eax, NR_kbhit
    int     INT_VECTOR_SYS_CALL
    ret

; void scroll(int lines)
_scroll:
    mov     eax, NR_scroll
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 文件 IO API (NR 26-32) =====

; void set_io_size(int size)
_set_io_size:
    mov     eax, NR_set_io_size
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; int file_read(char* name, char* buf, int bufsize)
_file_read:
    ; 先设置 bufsize
    mov     eax, NR_set_io_size
    mov     ebx, [esp+12]
    int     INT_VECTOR_SYS_CALL
    ; 再调用 file_read(name, buf)
    mov     eax, NR_file_read
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; int file_write(char* name, char* buf, int size)
_file_write:
    ; 先设置 size
    mov     eax, NR_set_io_size
    mov     ebx, [esp+12]
    int     INT_VECTOR_SYS_CALL
    ; 再调用 file_write(name, buf)
    mov     eax, NR_file_write
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; int file_delete(char* name)
_file_delete:
    mov     eax, NR_file_delete
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; int file_exists(char* name)
_file_exists:
    mov     eax, NR_file_exists
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; int file_size(char* name)
_file_size:
    mov     eax, NR_file_size
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; int file_list(char* buf, int bufsize)
_file_list:
    ; 先设置 bufsize
    mov     eax, NR_set_io_size
    mov     ebx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ; 再调用 file_list(buf)
    mov     eax, NR_file_list
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 系统控制 API (NR 33-34) =====

; void reboot(void)
_reboot:
    mov     eax, NR_reboot
    int     INT_VECTOR_SYS_CALL
    ret

; int get_key_flags(void)
_get_key_flags:
    mov     eax, NR_get_key_flags
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 文件扩展 API (NR 35) =====

; int file_append(char* name, char* buf, int size)
_file_append:
    ; 先设置 size
    mov     eax, NR_set_io_size
    mov     ebx, [esp+12]
    int     INT_VECTOR_SYS_CALL
    ; 再调用 file_append(name, buf)
    mov     eax, NR_file_append
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 类型转换 API (NR 36-37) =====

; int int_to_str(int value, char* buf, int radix)
_int_to_str:
    ; 先设置 radix (通过 set_io_size)
    mov     eax, NR_set_io_size
    mov     ebx, [esp+12]
    int     INT_VECTOR_SYS_CALL
    ; 再调用 int_to_str(value, buf)
    mov     eax, NR_int_to_str
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; int atoi(char* str)
_atoi:
    mov     eax, NR_atoi
    mov     ebx, [esp+4]
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 文件信息 API (NR 38) =====

; int file_stat(char* name, int* info)
_file_stat:
    mov     eax, NR_file_stat
    mov     ebx, [esp+4]
    mov     ecx, [esp+8]
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 进程启动 API (NR 39) =====

; int spawn(char* name)
;   总是非阻塞: 启动后立即返回, 返回槽位号(>=0成功) 或 错误码(<0失败)
;   阻塞/非阻塞的选择权在 shell run 命令, 不对应用开放, 避免恶意程序卡住系统
_spawn:
    mov     eax, NR_spawn
    mov     ebx, [esp+4]
    xor     ecx, ecx         ; blocking = 0 (强制非阻塞)
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 用户控制台与窗口 API (NR 40-46) =====

; void uc_putc(int ch, int attr)
_uc_putc:
    mov     eax, NR_uc_putc
    mov     ebx, [esp+4]        ; ch
    mov     ecx, [esp+8]        ; attr
    int     INT_VECTOR_SYS_CALL
    ret

; void uc_clear(void)
_uc_clear:
    mov     eax, NR_uc_clear
    int     INT_VECTOR_SYS_CALL
    ret

; int win_create(int x, int y, int w, int h, char *title)
;   在栈上构造参数块 {x, y, w, h, title_off}, 传偏移给内核
;   返回: win_id (>=0) 或 -1
_win_create:
    sub     esp, 20             ; 5 个 int 的参数块
    ; 复制参数到参数块 (栈布局: [esp]=buf, [esp+20]=ret, [esp+24]=x, ...)
    mov     eax, [esp+24]       ; x
    mov     [esp], eax
    mov     eax, [esp+28]       ; y
    mov     [esp+4], eax
    mov     eax, [esp+32]       ; w
    mov     [esp+8], eax
    mov     eax, [esp+36]       ; h
    mov     [esp+12], eax
    mov     eax, [esp+40]       ; title (段内偏移)
    mov     [esp+16], eax

    mov     eax, NR_win_create
    mov     ebx, esp            ; args_off = 参数块地址
    int     INT_VECTOR_SYS_CALL
    ; eax = win_id

    add     esp, 20
    ret

; void win_close(int win_id)
_win_close:
    mov     eax, NR_win_close
    mov     ebx, [esp+4]        ; win_id
    int     INT_VECTOR_SYS_CALL
    ret

; void win_draw_text(int win_id, int x, int y, char *str, int fg, int bg)
;   在栈上构造参数块 {win_id, x, y, str_off, fg, bg}
_win_draw_text:
    sub     esp, 24             ; 6 个 int 的参数块
    mov     eax, [esp+28]       ; win_id
    mov     [esp], eax
    mov     eax, [esp+32]       ; x
    mov     [esp+4], eax
    mov     eax, [esp+36]       ; y
    mov     [esp+8], eax
    mov     eax, [esp+40]       ; str
    mov     [esp+12], eax
    mov     eax, [esp+44]       ; fg
    mov     [esp+16], eax
    mov     eax, [esp+48]       ; bg
    mov     [esp+20], eax

    mov     eax, NR_win_draw_text
    mov     ebx, esp
    int     INT_VECTOR_SYS_CALL

    add     esp, 24
    ret

; void win_draw_rect(int win_id, int x, int y, int w, int h, int color)
;   在栈上构造参数块 {win_id, x, y, w, h, color}
_win_draw_rect:
    sub     esp, 24             ; 6 个 int 的参数块
    mov     eax, [esp+28]       ; win_id
    mov     [esp], eax
    mov     eax, [esp+32]       ; x
    mov     [esp+4], eax
    mov     eax, [esp+36]       ; y
    mov     [esp+8], eax
    mov     eax, [esp+40]       ; w
    mov     [esp+12], eax
    mov     eax, [esp+44]       ; h
    mov     [esp+16], eax
    mov     eax, [esp+48]       ; color
    mov     [esp+20], eax

    mov     eax, NR_win_draw_rect
    mov     ebx, esp
    int     INT_VECTOR_SYS_CALL

    add     esp, 24
    ret

; int win_get_event(int win_id, int *type, int *x, int *y)
;   阻塞等待事件 (用户侧循环重试), 返回事件类型, 坐标写入 *x/*y.
;   内核非阻塞返回 (0=无事件), 这里循环重试.
;   用户态循环可被时钟中断打断并正常调度 (避免内核忙等死锁).
_win_get_event:
.wge_retry:
    sub     esp, 12             ; 3 个 int 输出缓冲
    ; 栈布局: [esp]=buf, [esp+12]=ret, [esp+16]=win_id, [esp+20]=type*, [esp+24]=x*, [esp+28]=y*
    mov     eax, NR_win_get_event
    mov     ebx, [esp+16]       ; win_id
    mov     ecx, esp            ; ev_off = 输出缓冲地址
    int     INT_VECTOR_SYS_CALL
    ; eax = 事件类型, 0=无事件

    test    eax, eax
    jz      .wge_no_event

    ; 有事件, 分发缓冲区到用户指针 (int 0x90 可能破坏 ebx/ecx/edx, 重新从栈读取)
    mov     edx, [esp+20]       ; type*
    test    edx, edx
    jz      .wge_skip_t
    mov     [edx], eax          ; type = 返回值
.wge_skip_t:
    mov     edx, [esp+24]       ; x*
    test    edx, edx
    jz      .wge_skip_x
    mov     ecx, [esp+4]        ; ev[1] = x
    mov     [edx], ecx
.wge_skip_x:
    mov     edx, [esp+28]       ; y*
    test    edx, edx
    jz      .wge_skip_y
    mov     ecx, [esp+8]        ; ev[2] = y
    mov     [edx], ecx
.wge_skip_y:

    add     esp, 12
    ret

.wge_no_event:
    add     esp, 12
    jmp     .wge_retry          ; 无事件, 重试

; void win_clear(int win_id, int color)
;   清空用户窗口画布为指定颜色.
_win_clear:
    ; 栈: [esp+4]=win_id, [esp+8]=color
    sub     esp, 8              ; 2 个 int 参数块
    mov     eax, [esp+12]       ; win_id
    mov     [esp], eax
    mov     eax, [esp+16]       ; color
    mov     [esp+4], eax
    mov     eax, NR_win_clear
    mov     ebx, esp            ; args_off
    int     INT_VECTOR_SYS_CALL
    add     esp, 8
    ret

; void win_set_title(int win_id, char *title)
;   设置窗口标题.
_win_set_title:
    ; 栈: [esp+4]=win_id, [esp+8]=title
    sub     esp, 8              ; 2 个 int 参数块
    mov     eax, [esp+12]       ; win_id
    mov     [esp], eax
    mov     eax, [esp+16]       ; title (段内偏移)
    mov     [esp+4], eax
    mov     eax, NR_win_set_title
    mov     ebx, esp            ; args_off
    int     INT_VECTOR_SYS_CALL
    add     esp, 8
    ret

; void win_draw_line(int win_id, int x1, int y1, int x2, int y2, int color)
;   在画布上画线 (Bresenham).
_win_draw_line:
    ; 栈: [esp+4..24] = win_id,x1,y1,x2,y2,color
    sub     esp, 24             ; 6 个 int 参数块
    mov     eax, [esp+28]       ; win_id
    mov     [esp], eax
    mov     eax, [esp+32]       ; x1
    mov     [esp+4], eax
    mov     eax, [esp+36]       ; y1
    mov     [esp+8], eax
    mov     eax, [esp+40]       ; x2
    mov     [esp+12], eax
    mov     eax, [esp+44]       ; y2
    mov     [esp+16], eax
    mov     eax, [esp+48]       ; color
    mov     [esp+20], eax
    mov     eax, NR_win_draw_line
    mov     ebx, esp            ; args_off
    int     INT_VECTOR_SYS_CALL
    add     esp, 24
    ret

; void win_set_pixel(int win_id, int x, int y, int color)
;   设置单个像素.
_win_set_pixel:
    ; 栈: [esp+4..16] = win_id,x,y,color
    sub     esp, 16             ; 4 个 int 参数块
    mov     eax, [esp+20]       ; win_id
    mov     [esp], eax
    mov     eax, [esp+24]       ; x
    mov     [esp+4], eax
    mov     eax, [esp+28]       ; y
    mov     [esp+8], eax
    mov     eax, [esp+32]       ; color
    mov     [esp+12], eax
    mov     eax, NR_win_set_pixel
    mov     ebx, esp            ; args_off
    int     INT_VECTOR_SYS_CALL
    add     esp, 16
    ret

; int win_peek_event(int win_id, int *type, int *x, int *y)
;   非阻塞获取事件. 返回事件类型 (0=无事件).
_win_peek_event:
    sub     esp, 12             ; 3 个 int 输出缓冲
    mov     eax, NR_win_peek_event
    mov     ebx, [esp+16]       ; win_id
    mov     ecx, esp            ; ev_off
    int     INT_VECTOR_SYS_CALL
    ; eax = 事件类型, 0=无事件
    test    eax, eax
    jz      .wpe_none
    ; 有事件, 分发到用户指针
    mov     edx, [esp+20]       ; type*
    test    edx, edx
    jz      .wpe_skip_t
    mov     [edx], eax
.wpe_skip_t:
    mov     edx, [esp+24]       ; x*
    test    edx, edx
    jz      .wpe_skip_x
    mov     ecx, [esp+4]
    mov     [edx], ecx
.wpe_skip_x:
    mov     edx, [esp+28]       ; y*
    test    edx, edx
    jz      .wpe_skip_y
    mov     ecx, [esp+8]
    mov     [edx], ecx
.wpe_skip_y:
    add     esp, 12
    ret
.wpe_none:
    add     esp, 12
    xor     eax, eax            ; 返回 0
    ret

; int win_get_size(int win_id, int *w, int *h)
;   获取窗口客户区尺寸. 返回 0=成功, -1=失败.
_win_get_size:
    sub     esp, 8              ; 2 个 int 输出缓冲
    mov     eax, NR_win_get_size
    mov     ebx, [esp+16]       ; win_id
    mov     ecx, esp            ; size_off
    int     INT_VECTOR_SYS_CALL
    ; eax = 0(成功)/-1(失败)
    mov     edx, [esp+16]       ; 恢复 (int 0x90 可能破坏)
    test    eax, eax
    jnz     .wgs_fail
    ; 成功: 分发 w,h 到用户指针
    mov     edx, [esp+20]       ; *w
    test    edx, edx
    jz      .wgs_skip_w
    mov     ecx, [esp]
    mov     [edx], ecx
.wgs_skip_w:
    mov     edx, [esp+24]       ; *h
    test    edx, edx
    jz      .wgs_skip_h
    mov     ecx, [esp+4]
    mov     [edx], ecx
.wgs_skip_h:
    add     esp, 8
    xor     eax, eax
    ret
.wgs_fail:
    add     esp, 8
    mov     eax, -1
    ret

; void win_set_closable(int win_id, int closable)
;   设置窗口是否允许点击叉号关闭 (0=禁止, 1=允许)
_win_set_closable:
    mov     eax, NR_win_set_closable
    mov     ebx, [esp+4]        ; win_id
    mov     ecx, [esp+8]        ; closable
    int     INT_VECTOR_SYS_CALL
    ret

; int win_get_closable(int win_id)
;   查询窗口是否允许点击叉号关闭. 返回 1=允许, 0=禁止
_win_get_closable:
    mov     eax, NR_win_get_closable
    mov     ebx, [esp+4]        ; win_id
    xor     ecx, ecx
    int     INT_VECTOR_SYS_CALL
    ret

; ===== 字符串工具函数 (纯用户态, 无需系统调用) =====

; int strlen(char* str) - 返回字符串长度 (不含 \0)
_strlen:
    mov     edx, [esp+4]        ; edx = str
    xor     eax, eax            ; eax = 计数器
.strlen_loop:
    cmp     byte [edx+eax], 0
    je      .strlen_done
    inc     eax
    jmp     .strlen_loop
.strlen_done:
    ret

; int strcmp(char* a, char* b) - 比较, 返回 0=相等, <0=a<b, >0=a>b
_strcmp:
    mov     edx, [esp+4]        ; edx = a
    mov     ecx, [esp+8]        ; ecx = b
.strcmp_loop:
    mov     al, [edx]           ; al = *a
    mov     bl, [ecx]           ; bl = *b
    test    al, al
    jz      .strcmp_a_end      ; a 到末尾
    cmp     al, bl
    jne     .strcmp_diff
    inc     edx
    inc     ecx
    jmp     .strcmp_loop
.strcmp_a_end:
    ; *a == 0, 比较 *a 和 *b
    xor     eax, eax
    mov     al, bl              ; eax = *b (0 则相等, 非 0 则 a < b)
    ret
.strcmp_diff:
    ; *a != *b
    movzx   eax, al
    movzx   ebx, bl
    sub     eax, ebx            ; eax = *a - *b
    ret

; void strcpy(char* dst, char* src) - 复制字符串 (含 \0)
_strcpy:
    mov     edx, [esp+4]        ; edx = dst
    mov     ecx, [esp+8]        ; ecx = src
.strcpy_loop:
    mov     al, [ecx]
    mov     [edx], al
    test    al, al
    jz      .strcpy_done
    inc     edx
    inc     ecx
    jmp     .strcpy_loop
.strcpy_done:
    ret

; void memcpy(char* dst, char* src, int n) - 复制 n 字节
_memcpy:
    push    edi
    push    esi
    mov     edi, [esp+12]       ; edi = dst (esp+4+8)
    mov     esi, [esp+16]       ; esi = src (esp+8+8)
    mov     ecx, [esp+20]       ; ecx = n   (esp+12+8)
    cld
    rep     movsb
    pop     esi
    pop     edi
    ret

; void memset(char* dst, int val, int n) - 填充 n 字节
_memset:
    push    edi
    mov     edi, [esp+8]        ; edi = dst (esp+4+4)
    mov     eax, [esp+12]       ; eax = val (esp+8+4)
    mov     ecx, [esp+16]       ; ecx = n   (esp+12+4)
    cld
    rep     stosb
    pop     edi
    ret
