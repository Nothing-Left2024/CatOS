
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                              protect.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"


/* 本文件内函数声明 */
PRIVATE void init_idt_desc(unsigned char vector, t_8 desc_type, t_pf_int_handler handler, unsigned char privilege);
PRIVATE void init_descriptor(DESCRIPTOR * p_desc, t_32 base, t_32 limit, t_16 attribute);


/* 中断处理函数 */
void	divide_error();
void	single_step_exception();
void	nmi();
void	breakpoint_exception();
void	overflow();
void	bounds_check();
void	inval_opcode();
void	copr_not_available();
void	double_fault();
void	copr_seg_overrun();
void	inval_tss();
void	segment_not_present();
void	stack_exception();
void	general_protection();
void	page_fault();
void	copr_error();
void	hwint00();
void	hwint01();
void	hwint02();
void	hwint03();
void	hwint04();
void	hwint05();
void	hwint06();
void	hwint07();
void	hwint08();
void	hwint09();
void	hwint10();
void	hwint11();
void	hwint12();
void	hwint13();
void	hwint14();
void	hwint15();


/*======================================================================*
                            init_prot
 *----------------------------------------------------------------------*
 初始化 IDT
 *======================================================================*/
PUBLIC void init_prot()
{
	init_8259A();

	// 全部初始化成中断门(没有陷阱门)
	init_idt_desc(INT_VECTOR_DIVIDE,	DA_386IGate, divide_error,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_DEBUG,		DA_386IGate, single_step_exception,	PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_NMI,		DA_386IGate, nmi,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_BREAKPOINT,	DA_386IGate, breakpoint_exception,	PRIVILEGE_USER);
	init_idt_desc(INT_VECTOR_OVERFLOW,	DA_386IGate, overflow,			PRIVILEGE_USER);
	init_idt_desc(INT_VECTOR_BOUNDS,	DA_386IGate, bounds_check,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_INVAL_OP,	DA_386IGate, inval_opcode,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_COPROC_NOT,	DA_386IGate, copr_not_available,	PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_DOUBLE_FAULT,	DA_386IGate, double_fault,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_COPROC_SEG,	DA_386IGate, copr_seg_overrun,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_INVAL_TSS,	DA_386IGate, inval_tss,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_SEG_NOT,	DA_386IGate, segment_not_present,	PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_STACK_FAULT,	DA_386IGate, stack_exception,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_PROTECTION,	DA_386IGate, general_protection,	PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_PAGE_FAULT,	DA_386IGate, page_fault,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_COPROC_ERR,	DA_386IGate, copr_error,		PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 0,	DA_386IGate, hwint00,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 1,	DA_386IGate, hwint01,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 2,	DA_386IGate, hwint02,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 3,	DA_386IGate, hwint03,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 4,	DA_386IGate, hwint04,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 5,	DA_386IGate, hwint05,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 6,	DA_386IGate, hwint06,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ0 + 7,	DA_386IGate, hwint07,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 0,	DA_386IGate, hwint08,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 1,	DA_386IGate, hwint09,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 2,	DA_386IGate, hwint10,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 3,	DA_386IGate, hwint11,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 4,	DA_386IGate, hwint12,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 5,	DA_386IGate, hwint13,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 6,	DA_386IGate, hwint14,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_IRQ8 + 7,	DA_386IGate, hwint15,			PRIVILEGE_KRNL);
	init_idt_desc(INT_VECTOR_SYS_CALL,	DA_386IGate, sys_call,			PRIVILEGE_USER);

	/* 初始化 GDT 中的 TSS 描述符 */
	memset(&tss, 0, sizeof(tss));
	tss.ss0		= SELECTOR_KERNEL_DS;
	tss.iobase	= sizeof(tss);	/* 显式设置: I/O位图基址=TSS末尾 → 无I/O位图 */
	/* TSS 描述符的 limit 必须是 sizeof(tss) - 1 (段界限 = 大小 - 1)。
	 * 如果 limit == sizeof(tss)，则 iobase == limit，CPU 会认为存在 I/O 位图，
	 * 但实际上 I/O 位图位置是 TSS 结构体之外的垃圾数据。
	 * VMware 对 TSS 检查严格，这种不一致可能导致 #TS → #DF → 三重故障。
	 * 正确: limit = size - 1 → iobase > limit → CPU 识别为"无 I/O 位图"。*/
	{
		t_32 tss_limit = sizeof(tss) - 1;
		if(tss_limit < 0x67) tss_limit = 0x67; /* 386 TSS 最小 limit = 0x67 (104 字节) */
		init_descriptor(&gdt[INDEX_TSS],
				vir2phys(seg2phys(SELECTOR_KERNEL_DS), &tss),
				tss_limit,
				DA_386TSS);
	}

	/* 在 GDT 中为每个进程的 LDT 建立描述符 */
	int i;
	PROCESS* p_proc	= proc_table;
	t_16 selector_ldt = INDEX_LDT_FIRST << 3;
	for(i=0;i<NR_TASKS+NR_PROCS;i++){
		init_descriptor(&gdt[selector_ldt>>3],
				vir2phys(seg2phys(SELECTOR_KERNEL_DS), proc_table[i].ldts),
				LDT_SIZE * sizeof(DESCRIPTOR),
				DA_LDT);
		p_proc++;
		selector_ldt += 1 << 3;
	}

	/* ★ 动态修改视频段描述符基址 */
	/* 读取 loader 写入的 video_mode 标志（物理地址 0x504） */
	t_8 video_mode = *((volatile t_8*)0x504);
	t_32 vmem_base;
	if (video_mode == 1) {
		/* VGA Mode 13h: 基址 0xA0000, 界限 64KB */
		vmem_base = 0xA0000;
		init_descriptor(&gdt[INDEX_VIDEO], vmem_base, 0xFFFF,
				DA_DRW | DA_DPL3 | DA_32);
	} else if (video_mode == 2) {
		/* VBE LFB: 基址从 0x506 读取, 界限 4GB */
		vmem_base = *((volatile t_32*)0x506);
		init_descriptor(&gdt[INDEX_VIDEO], vmem_base, 0xFFFFF,
				DA_DRW | DA_DPL3 | DA_32 | DA_LIMIT_4K);
	} else {
		/* 文本模式: 基址 0xB8000, 界限 32KB */
		vmem_base = 0xB8000;
		init_descriptor(&gdt[INDEX_VIDEO], vmem_base, 0x7FFF, DA_DRW | DA_DPL3);
	}

	/* ★ 重新加载 gs 段寄存器（使用修改后的描述符） */
	__asm__ __volatile__(
		"movw %0, %%ax  \n\t"
		"movw %%ax, %%gs"
		:
		: "i"(SELECTOR_VIDEO)
		: "ax"
	);

	/* 图形模式下跳过文本输出, 避免在图形显存中写入垃圾像素 */
	if (*((volatile t_8*)0x504) == 0) {
		disp_str("[INFO] init_prot(): TSS and LDTs configured\n");
	}
}


/*======================================================================*
                             init_idt_desc
 *----------------------------------------------------------------------*
 初始化 386 中断门
 *======================================================================*/
PUBLIC void init_idt_desc(unsigned char vector, t_8 desc_type, t_pf_int_handler handler, unsigned char privilege)
{
	GATE *	p_gate	= &idt[vector];
	t_32	base	= (t_32)handler;
	p_gate->offset_low	= base & 0xFFFF;
	p_gate->selector	= SELECTOR_KERNEL_CS;
	p_gate->dcount		= 0;
	p_gate->attr		= desc_type | (privilege << 5);
	p_gate->offset_high	= (base >> 16) & 0xFFFF;
}


/*======================================================================*
                           seg2phys
 *----------------------------------------------------------------------*
 由描述符求物理地址
 *======================================================================*/
PUBLIC t_32 seg2phys(t_16 seg)
{
	DESCRIPTOR* p_dest = &gdt[seg >> 3];

	return (p_dest->base_high << 24) | (p_dest->base_mid << 16) | (p_dest->base_low);
}

/*======================================================================*
                           init_descriptor
 *----------------------------------------------------------------------*
 初始化描述符
 *======================================================================*/
PRIVATE void init_descriptor(DESCRIPTOR * p_desc, t_32 base, t_32 limit, t_16 attribute)
{
	p_desc->limit_low		= limit & 0x0FFFF;		// 段界限 1		(2 字节)
	p_desc->base_low		= base & 0x0FFFF;		// 基地址 1		(2 字节)
	p_desc->base_mid		= (base >> 16) & 0x0FF;		// 基地址 2		(1 字节)
	p_desc->attr1			= attribute & 0xFF;		// 属性 1
	p_desc->limit_high_attr2	= ((limit >> 16) & 0x0F) |
						(attribute >> 8) & 0xF0;// 段界限 2 + 属性 2
	p_desc->base_high		= (base >> 24) & 0x0FF;		// 基地址 3		(1 字节)
}

/*======================================================================*
                            exception_handler
 *----------------------------------------------------------------------*
 异常处理
 *======================================================================*/
PUBLIC void exception_handler(int vec_no, int err_code, int eip, int cs, int eflags)
{
	/* ===== 用户程序崩溃保护 =====
	 * 如果当前进程是用户程序（is_user_proc==1），则：
	 * 1. 销毁该用户进程（标记槽位空闲）
	 * 2. 恢复 p_proc_ready 指向 TTY 进程
	 * 3. 调用 restart() 切换到 TTY 进程（不返回）
	 * 这样 shell 能继续运行，不会被用户程序崩溃拖垮
	 */
	if (p_proc_ready != 0 && p_proc_ready->is_user_proc == 1) {
		/* 在屏幕顶部显示崩溃信息 */
		if (*((volatile t_8*)0x504) == 0) {
			disp_pos = 0;
			disp_str("\n\n  Program crashed (exception #");
			/* 显示异常向量号 */
			{
				char buf[16];
				int n = vec_no;
				int i = 0;
				if (n == 0) { buf[0]='0'; i=1; }
				else {
					char tmp[16];
					int j = 0;
					while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
					while (j > 0) buf[i++] = tmp[--j];
				}
				buf[i] = 0;
				disp_str(buf);
			}
			disp_str(")\n  Destroying user process, returning to shell...\n");
		}

		/* 销毁用户进程 */
		p_proc_ready->exit_code = -vec_no - 1;  /* 负数表示异常退出 */
		p_proc_ready->is_user_proc = 0;
		p_proc_ready->ticks = 0;
		p_proc_ready->priority = 0;
		user_proc_free_slots++;

		/* 选择下一个就绪进程 (不再固定切回 TTY) */
		schedule();

		/* 切换上下文（不返回） */
		/* 关键: exception 标签没有调用 save(), 所以 k_reenter 没有被 inc。
		 * 用户进程运行时 k_reenter=-1, 若直接调用 restart(), restart 中的
		 * dec k_reenter 会让 k_reenter 变成 -2。之后 TTY 运行时收到时钟中断,
		 * save 中 inc k_reenter (-2→-1), cmp -1,0 不相等 → 错误走重入路径,
		 * 不切换 esp 到 StackTop, 导致栈覆盖 proc_table 之前的全局变量,
		 * iretd 读取被破坏的 cs/eflags/esp/ss → #GP → 三重故障 → CPU 关闭。
		 * 修复: 这里先 inc k_reenter (-1→0), restart 的 dec 让 k_reenter
		 * 从 0 变 -1, 恢复到进程运行时的正常值。 */
		disable_int();
		k_reenter++;
		restart();
		/* 不会到达这里 */
	}

	/* ===== 以下是原有逻辑：内核异常处理（halt） ===== */
	if (*((volatile t_8*)0x504) == 0) {
		int i;
		int text_color = 0x74; /* 灰底红字 */
		char err_description[][64] = {	"#DE Divide Error",
						"#DB RESERVED",
						"◎  NMI Interrupt",
						"#BP Breakpoint",
						"#OF Overflow",
						"#BR BOUND Range Exceeded",
						"#UD Invalid Opcode (Undefined Opcode)",
						"#NM Device Not Available (No Math Coprocessor)",
						"#DF Double Fault",
						"    Coprocessor Segment Overrun (reserved)",
						"#TS Invalid TSS",
						"#NP Segment Not Present",
						"#SS Stack-Segment Fault",
						"#GP General Protection",
						"#PF Page Fault",
						"◎  (Intel reserved. Do not use.)",
						"#MF x87 FPU Floating-Point Error (Math Fault)",
						"#AC Alignment Check",
						"#MC Machine Check",
						"#XF SIMD Floating-Point Exception"
					};

		/* 通过打印空格的方式清空屏幕前五行，重置 disp_pos 变量 */
		disp_pos = 0;
		for(i=0;i<80*5;i++){
			disp_str(" ");
		}
		disp_pos = 0;

		disp_color_str("Exception! --> ", text_color);
		disp_color_str(err_description[vec_no], text_color);
		disp_color_str("\n\n", text_color);
		disp_color_str("EFLAGS:", text_color);
		disp_int(eflags);
		disp_color_str("CS:", text_color);
		disp_int(cs);
		disp_color_str("EIP:", text_color);
		disp_int(eip);

		if(err_code != 0xFFFFFFFF){
			disp_color_str("Error code:", text_color);
			disp_int(err_code);
		}
	}
}

