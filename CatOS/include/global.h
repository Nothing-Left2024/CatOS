
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            global.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/* EXTERN is defined as extern except in global.c */
#ifdef	GLOBAL_VARIABLES_HERE
#undef	EXTERN
#define	EXTERN
#endif

EXTERN	int		ticks;

EXTERN	t_8		g_user_attr;	/* 用户程序默认文本属性 (0x0F=黑底亮白), set_color 修改 */
EXTERN	int		g_io_size;	/* 文件 IO 缓冲区大小 (set_io_size 设置, exec 重置) */

EXTERN	int		disp_pos;
EXTERN	t_8		gdt_ptr[6];	// 0~15:Limit  16~47:Base
EXTERN	DESCRIPTOR	gdt[GDT_SIZE];
EXTERN	t_8		idt_ptr[6];	// 0~15:Limit  16~47:Base
EXTERN	GATE		idt[IDT_SIZE];

EXTERN	t_32		k_reenter;

EXTERN	TSS		tss;
EXTERN	PROCESS*	p_proc_ready;
EXTERN	int		user_proc_free_slots;	/* 空闲用户进程槽位数 (NR_USER_PROCS..0) */

EXTERN	int		nr_current_console;

extern	PROCESS		proc_table[];
/* task_stack 迁移到 4 个用户进程区之后 (0x1A0000), 避免 0x140000 与槽位1冲突 */
#define	TASK_STACK_BASE		0x1A0000
#define	task_stack		((char*)(TASK_STACK_BASE))
extern	TASK		task_table[];
extern	TASK		user_proc_table[];
extern	TTY		tty_table[];
extern	CONSOLE		console_table[];

extern	t_pf_irq_handler	irq_table[];

extern	t_sys_call		sys_call_table[];
