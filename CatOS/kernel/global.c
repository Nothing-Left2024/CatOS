
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            global.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#define GLOBAL_VARIABLES_HERE

#include "type.h"
#include "const.h"
#include "protect.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"


PUBLIC	PROCESS	proc_table[NR_TASKS + NR_PROCS + NR_USER_PROCS];

PUBLIC	TASK	task_table[NR_TASKS] = {{task_tty, STACK_SIZE_TTY, "tty"}};
PUBLIC	TASK	user_proc_table[NR_PROCS] = {	{TestA, STACK_SIZE_TESTA, "TestA"},
						{TestB, STACK_SIZE_TESTB, "TestB"},
						{TestC, STACK_SIZE_TESTC, "TestC"}};

PUBLIC	TTY			tty_table[NR_CONSOLES];
PUBLIC	CONSOLE			console_table[NR_CONSOLES];

PUBLIC	t_pf_irq_handler	irq_table[NR_IRQ];

PUBLIC	int			user_proc_free_slots = NR_USER_PROCS;	/* 空闲用户进程槽位数 */

PUBLIC	t_8			g_user_attr = 0x0F;	/* 用户程序默认文本属性 (黑底亮白) */
PUBLIC	int			g_io_size = 0;		/* 文件 IO 缓冲区大小 */

PUBLIC	t_sys_call		sys_call_table[NR_SYS_CALL] = {sys_get_ticks, sys_write,
						    sys_putc, sys_puts,
						    sys_getc, sys_exit,
						    sys_clrscr, sys_gotoxy,
						    sys_get_xy, sys_get_cols,
						    sys_get_rows, sys_get_vmode,
						    sys_set_color, sys_putc_color,
						    sys_get_pid, sys_get_name,
						    sys_rand, sys_srand,
						    sys_put_int, sys_put_uint,
						    sys_put_hex, sys_put_bin,
						    sys_delay, sys_getch,
						    sys_kbhit, sys_scroll,
						    sys_set_io_size, sys_file_read,
						    sys_file_write, sys_file_delete,
						    sys_file_exists, sys_file_size,
						    sys_file_list, sys_reboot,
						    sys_get_key_flags,
						    sys_file_append, sys_int_to_str,
						    sys_atoi, sys_file_stat,
						    sys_spawn,
						    sys_uc_putc, sys_uc_clear,
						    sys_win_create, sys_win_close,
						    sys_win_draw_text, sys_win_draw_rect,
						    sys_win_get_event,
						    sys_win_clear, sys_win_set_title,
						    sys_win_draw_line, sys_win_set_pixel,
						    sys_win_peek_event, sys_win_get_size,
						    sys_win_set_closable, sys_win_get_closable};
