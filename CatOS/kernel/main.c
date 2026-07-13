/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            main.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    CatOS v1.0
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

/*======================================================================*
                            tinix_main
 *======================================================================*/
PUBLIC int tinix_main()
{
	TASK*		p_task;
	PROCESS*	p_proc		= proc_table;
	char*		p_task_stack	= task_stack + STACK_SIZE_TOTAL;
	t_16		selector_ldt	= SELECTOR_LDT_FIRST;
	int		i;
	t_8		privilege;
	t_8		rpl;
	int		eflags;

	for(i=0;i<NR_TASKS+NR_PROCS;i++){
		memset(p_proc, 0, sizeof(PROCESS));

		if (i < NR_TASKS) {
			p_task		= task_table + i;
			privilege	= PRIVILEGE_TASK;
			rpl		= RPL_TASK;
			eflags		= 0x1202;
		}
		else {
			/* TestA/B/C 是内核测试进程 (ring 1), 不是动态加载的 ring 3 用户进程。
			 * 真正的 ring 3 用户进程由 exec_user_program() 设置在 proc_table[4],
			 * 不在此循环中初始化。这里曾误设为 PRIVILEGE_USER/0x202 (ring 3),
			 * 会导致 IOPL=0, TestA/B/C 无法使用 cli/sti/in/out 等特权指令。 */
			p_task		= user_proc_table + (i - NR_TASKS);
			privilege	= PRIVILEGE_TASK;
			rpl		= RPL_TASK;
			eflags		= 0x1202;
		}

		strcpy(p_proc->name, p_task->name);
		p_proc->pid	= i;

		p_proc->ldt_sel	= selector_ldt;
		memcpy(&p_proc->ldts[0], &gdt[SELECTOR_KERNEL_CS >> 3], sizeof(DESCRIPTOR));
		p_proc->ldts[0].attr1 = DA_CR | privilege << 5;
		memcpy(&p_proc->ldts[1], &gdt[SELECTOR_KERNEL_DS >> 3], sizeof(DESCRIPTOR));
		p_proc->ldts[1].attr1 = DA_DRW | privilege << 5;
		p_proc->regs.cs		= ((8 * 0) & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.ds		= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.es		= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.fs		= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.ss		= ((8 * 1) & SA_RPL_MASK & SA_TI_MASK) | SA_TIL | rpl;
		p_proc->regs.gs		= (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;
		p_proc->regs.eip	= (t_32)p_task->initial_eip;
		p_proc->regs.esp	= (t_32)p_task_stack;
		p_proc->regs.eflags	= eflags;
		p_proc->regs.eax	= 0;
		p_proc->regs.ecx	= 0;
		p_proc->regs.edx	= 0;
		p_proc->regs.ebx	= 0;
		p_proc->regs.ebp	= 0;
		p_proc->regs.esi	= 0;
		p_proc->regs.edi	= 0;

		p_proc->nr_tty		= 0;

		p_task_stack -= p_task->stacksize;
		p_proc++;
		p_task++;
		selector_ldt += 1 << 3;
	}

	proc_table[0].ticks = proc_table[0].priority = 15;
	proc_table[1].ticks = proc_table[1].priority =  5;
	proc_table[2].ticks = proc_table[2].priority =  5;
	proc_table[3].ticks = proc_table[3].priority =  5;

	proc_table[1].nr_tty = 0;
	proc_table[2].nr_tty = 1;
	proc_table[3].nr_tty = 1;

	k_reenter	= 0;
	ticks		= 0;
	p_proc_ready	= proc_table;

	/* 初始化所有动态用户进程槽位为空闲状态 */
	{
		int i;
		for (i = 0; i < NR_USER_PROCS; i++) {
			PROCESS* up = &proc_table[NR_TASKS + NR_PROCS + i];
			up->pid		= -1;
			up->is_user_proc	= 0;
			up->exit_code	= 0;
			up->ticks		= 0;
			up->priority	= 0;
			up->name[0]		= 0;
		}
	}
	user_proc_free_slots				= NR_USER_PROCS;

	init_clock();

	restart();

	while(1){}
}
