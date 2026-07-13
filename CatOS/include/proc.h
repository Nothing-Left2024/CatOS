
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               proc.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/


typedef struct s_stackframe {	/* proc_ptr points here				┃ Low			*/
	t_32	gs;		/* ┃						┃			*/
	t_32	fs;		/* ┃						┃			*/
	t_32	es;		/* ┃						┃			*/
	t_32	ds;		/* ┃						┃			*/
	t_32	edi;		/* ┃						┃			*/
	t_32	esi;		/* ┃ pushed by save()				┃			*/
	t_32	ebp;		/* ┃						┃			*/
	t_32	kernel_esp;	/* <- 'popad' will ignore it			┃			*/
	t_32	ebx;		/* ┃						┃栈从高地址向低地址增长*/
	t_32	edx;		/* ┃						┃			*/
	t_32	ecx;		/* ┃						┃			*/
	t_32	eax;		/* ┃						┃			*/
	t_32	retaddr;	/* return address for assembly code save()	┃			*/
	t_32	eip;		/*  ┃						┃			*/
	t_32	cs;		/*  ┃						┃			*/
	t_32	eflags;		/*  ┃ these are pushed by CPU during interrupt	┃			*/
	t_32	esp;		/*  ┃						┃			*/
	t_32	ss;		/*  ┃						┃High			*/
}STACK_FRAME;


typedef struct s_proc {
	STACK_FRAME			regs;			/* process' registers saved in stack frame */

	t_16				ldt_sel;		/* selector in gdt giving ldt base and limit*/
	DESCRIPTOR			ldts[LDT_SIZE];		/* local descriptors for code and data */
								/* 2 is LDT_SIZE - avoid include protect.h */
	int				ticks;			/* remained ticks */
	int				priority;
	t_32				pid;			/* process id passed in from MM */
	char				name[16];		/* name of the process */

	int				nr_tty;
	int				exit_code;		/* 用户程序退出码 */
	int				is_user_proc;		/* 1=动态加载的用户程序, 0=内核进程 */
}PROCESS;


/* CE (CatOS Executable) 可执行文件格式头 (64 字节) */
typedef struct s_ce_header {
	t_32	magic;		/* 魔数 'CE\x01\x00' = 0x00014543 (小端) */
	t_16	version;	/* 格式版本: 1 */
	t_16	flags;		/* 标志位: bit0=32位 */
	t_32	entry;		/* 入口地址 (段内偏移, 通常为 0) */
	t_32	text_size;	/* 代码段大小 (含 .rodata) */
	t_32	data_size;	/* 数据段大小 */
	t_32	bss_size;	/* BSS 段大小 */
	t_32	stack_size;	/* 请求栈大小 */
	t_32	total_size;	/* text+data+bss 总大小 (用于校验) */
	char	name[28];	/* 程序名称 (ASCII, 以 \0 结尾) */
	t_32	header_crc;	/* 头部 CRC32 (预留, 当前为 0) */
} __attribute__((packed)) CE_HEADER;

#define CE_MAGIC	0x00014543	/* 'CE\x01\x00' 小端: 'C'=0x43 'E'=0x45 ver=1 */
#define CE_VERSION	1


typedef struct s_task {
	t_pf_task	initial_eip;
	int		stacksize;
	char		name[32];
}TASK;


/* Number of tasks & processes */
#define NR_TASKS		1
#define NR_PROCS		3


/* stacks of tasks */
#define STACK_SIZE_TTY		0x8000
#define STACK_SIZE_TESTA	0x8000
#define STACK_SIZE_TESTB	0x8000
#define STACK_SIZE_TESTC	0x8000

#define STACK_SIZE_TOTAL	(STACK_SIZE_TTY + \
				STACK_SIZE_TESTA + \
				STACK_SIZE_TESTB + \
				STACK_SIZE_TESTC)

