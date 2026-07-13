
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            const.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef	_TINIX_CONST_H_
#define	_TINIX_CONST_H_


//#define __TINIX_DEBUG__


/* EXTERN */
#define	EXTERN	extern	/* EXTERN is defined as extern except in global.c */

/* 访问权限 */
#define	PUBLIC		/* PUBLIC is the opposite of PRIVATE */
#define	PRIVATE	static	/* PRIVATE x limits the scope of x */

/* Boolean */
#define	TRUE	1
#define	FALSE	0

/* Color */
/*
 * e.g.	MAKE_COLOR(BLUE, RED)
 *	MAKE_COLOR(BLACK, RED) | BRIGHT
 *	MAKE_COLOR(BLACK, RED) | BRIGHT | FLASH
 */
#define	BLACK	0x0	/* 0000 */
#define	WHITE	0x7	/* 0111 */
#define	RED	0x4	/* 0100 */
#define	GREEN	0x2	/* 0010 */
#define	BLUE	0x1	/* 0001 */
#define	FLASH	0x80	/* 1000 0000 */
#define	BRIGHT	0x08	/* 0000 1000 */
#define	MAKE_COLOR(x,y)	((x<<4) | y)	/* MAKE_COLOR(Background,Foreground) */

/* GDT 和 IDT 描述符的个数 */
#define	GDT_SIZE	128
#define	IDT_SIZE	256

/* 特权级 */
#define	PRIVILEGE_KRNL	0
#define	PRIVILEGE_TASK	1
#define	PRIVILEGE_USER	3
/* RPL */
#define	RPL_KRNL	SA_RPL0
#define	RPL_TASK	SA_RPL1
#define	RPL_USER	SA_RPL3

/* TTY */
#define	NR_CONSOLES	3	/* consoles */

/* 8259A interrupt controller ports. */
#define	INT_M_CTL	0x20	/* I/O port for interrupt controller         <Master> */
#define	INT_M_CTLMASK	0x21	/* setting bits in this port disables ints   <Master> */
#define	INT_S_CTL	0xA0	/* I/O port for second interrupt controller  <Slave>  */
#define	INT_S_CTLMASK	0xA1	/* setting bits in this port disables ints   <Slave>  */

/* 8253/8254 PIT (Programmable Interval Timer) */
#define TIMER0          0x40	/* I/O port for timer channel 0 */
#define TIMER_MODE      0x43	/* I/O port for timer mode control */
#define RATE_GENERATOR	0x34	/* 00-11-010-0 : Counter0 - LSB then MSB - rate generator - binary */
#define TIMER_FREQ	1193182L/* clock frequency for timer in PC and AT */
#define HZ		100	/* clock freq (software settable on IBM-PC) */

/* AT keyboard */
/* 8042 ports */
#define	KB_DATA		0x60	/* I/O port for keyboard data
					Read : Read Output Buffer 
					Write: Write Input Buffer(8042 Data&8048 Command) */
#define	KB_CMD		0x64	/* I/O port for keyboard command
					Read : Read Status Register
					Write: Write Input Buffer(8042 Command) */
#define	LED_CODE	0xED
#define	KB_ACK		0xFA

/* VGA */
#define CRTC_ADDR_REG			0x3D4	/* CRT Controller Registers - Address Register */
#define CRTC_DATA_REG			0x3D5	/* CRT Controller Registers - Data Registers */
#define CRTC_DATA_IDX_START_ADDR_H	0xC	/* register index of video mem start address (MSB) */
#define CRTC_DATA_IDX_START_ADDR_L	0xD	/* register index of video mem start address (LSB) */
#define CRTC_DATA_IDX_CURSOR_H		0xE	/* register index of cursor position (MSB) */
#define CRTC_DATA_IDX_CURSOR_L		0xF	/* register index of cursor position (LSB) */
#define V_MEM_BASE			0xB8000	/* base of color video memory */
#define V_MEM_SIZE			0x8000	/* 32K: B8000H -> BFFFFH */


/* Hardware interrupts */
#define	NR_IRQ		16	/* Number of IRQs */
#define	CLOCK_IRQ	0
#define	KEYBOARD_IRQ	1
#define	CASCADE_IRQ	2	/* cascade enable for 2nd AT controller */
#define	ETHER_IRQ	3	/* default ethernet interrupt vector */
#define	SECONDARY_IRQ	3	/* RS232 interrupt vector for port 2 */
#define	RS232_IRQ	4	/* RS232 interrupt vector for port 1 */
#define	XT_WINI_IRQ	5	/* xt winchester */
#define	FLOPPY_IRQ	6	/* floppy disk */
#define	PRINTER_IRQ	7
#define	MOUSE_IRQ	12	/* PS/2 mouse (slave IR4) */
#define	AT_WINI_IRQ	14	/* at winchester */


/* system call */
#define	NR_SYS_CALL	55
#define	NR_SYSCALLS	47		/* 系统调用总数 (与 syscall.asm 中 _NR_xxx 对应) */

/* 系统调用号 */
#define	NR_get_ticks	0
#define	NR_write	1
#define	NR_putc		2
#define	NR_puts		3
#define	NR_getc		4
#define	NR_exit		5
#define	NR_clrscr	6	/* 清屏 */
#define	NR_gotoxy	7	/* 移动光标到 (x, y) */
#define	NR_get_xy	8	/* 获取光标坐标 (x|y<<16) */
#define	NR_get_cols	9	/* 屏幕列数 */
#define	NR_get_rows	10	/* 屏幕行数 */
#define	NR_get_vmode	11	/* 当前视频模式 (0=文本, 1=图形) */
#define	NR_set_color	12	/* 设置默认文本颜色 */
#define	NR_putc_color	13	/* 输出带颜色的字符 */
#define	NR_get_pid	14	/* 获取当前进程 PID */
#define	NR_get_name	15	/* 获取当前进程名到缓冲区 */
#define	NR_rand		16	/* 伪随机数 */
#define	NR_srand	17	/* 设置随机种子 */
#define	NR_put_int	18	/* 输出有符号十进制整数 */
#define	NR_put_uint	19	/* 输出无符号十进制整数 */
#define	NR_put_hex	20	/* 输出十六进制整数 */
#define	NR_put_bin	21	/* 输出二进制整数 */
#define	NR_delay	22	/* 毫秒级延时 */
#define	NR_getch	23	/* 阻塞读取一个字符 */
#define	NR_kbhit	24	/* 检测键盘缓冲区是否有按键 */
#define	NR_scroll	25	/* 屏幕向上滚动 N 行 */
#define	NR_set_io_size	26	/* 设置文件 IO 缓冲区大小 */
#define	NR_file_read	27	/* 读取文件 */
#define	NR_file_write	28	/* 写入文件 (创建/覆盖) */
#define	NR_file_delete	29	/* 删除文件 */
#define	NR_file_exists	30	/* 检查文件是否存在 */
#define	NR_file_size	31	/* 获取文件大小 */
#define	NR_file_list	32	/* 列出根目录文件名 */
#define	NR_reboot	33	/* 重启系统 */
#define	NR_get_key_flags 34	/* 获取修饰键状态 */
#define	NR_file_append	35	/* 追加数据到文件末尾 */
#define	NR_int_to_str	36	/* 整数转字符串 (支持 10/16/2/8 进制) */
#define	NR_atoi		37	/* 字符串转整数 (十进制) */
#define	NR_file_stat	38	/* 获取文件详细信息 (大小/属性) */
#define	NR_spawn	39	/* 启动程序 (阻塞/非阻塞由参数控制) */
#define	NR_uc_putc	40	/* 用户控制台: 输出字符 */
#define	NR_uc_clear	41	/* 用户控制台: 清屏 */
#define	NR_win_create	42	/* 创建用户窗口 */
#define	NR_win_close	43	/* 关闭用户窗口 */
#define	NR_win_draw_text 44	/* 用户窗口: 绘制文本 */
#define	NR_win_draw_rect 45	/* 用户窗口: 绘制矩形 */
#define	NR_win_get_event 46	/* 用户窗口: 获取事件 (阻塞) */
#define	NR_win_clear     47	/* 用户窗口: 清空画布 */
#define	NR_win_set_title 48	/* 用户窗口: 设置标题 */
#define	NR_win_draw_line 49	/* 用户窗口: 画线 (Bresenham) */
#define	NR_win_set_pixel 50	/* 用户窗口: 设置单个像素 */
#define	NR_win_peek_event 51	/* 用户窗口: 非阻塞获取事件 */
#define	NR_win_get_size  52	/* 用户窗口: 获取客户区尺寸 */
#define	NR_win_set_closable 53	/* 用户窗口: 设置是否允许叉号关闭 */
#define	NR_win_get_closable 54	/* 用户窗口: 查询是否允许叉号关闭 */

/* 用户程序内存布局 (Phase 3: 多槽位并发) */
#define	USER_PROC_BASE		0x100000	/* 用户程序加载基址 (1MB) */
#define	USER_PROC_SIZE		0x20000		/* 每个用户程序内存区大小 (128KB) */
#define	NR_USER_PROCS		4		/* 动态用户进程槽位数 (可同时运行 4 个 ring3 程序) */
/* 槽位 i 的内存基址 = USER_PROC_BASE + i * USER_PROC_SIZE (0x100000..0x17FFFF) */

/* CE (CatOS Executable) 可执行文件格式 */
#define	USER_PROC_MAX_SIZE	0x1F000		/* 124KB (留 4KB 给栈) */

/* file_buf 与 task_stack 迁移到 4 个用户进程区之后, 避免冲突:
 * 0x100000..0x180000  用户进程区 (4 * 128KB)
 * 0x180000..0x1A0000  file_buf (128KB)
 * 0x1A0000..0x1C0000  task_stack (128KB, 见 global.h) */
#define	FILE_BUF_BASE		0x180000	/* 文件读取缓冲区基址 */


#endif /* _TINIX_CONST_H_ */
