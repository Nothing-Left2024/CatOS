
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               exec.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS 可执行文件加载器
  从 FAT 文件系统加载 CE (CatOS Executable) 格式的可执行文件, 创建 Ring 3 用户进程
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
#include "fat.h"
#include "gfx.h"
#include "wm.h"

/* 文件读取缓冲区: 迁移到 0x180000 (4 个用户进程区之后), 见 const.h */
#define FILE_BUF_SIZE		(USER_PROC_MAX_SIZE + 256)
#define file_buf		((char*)(FILE_BUF_BASE))

/*======================================================================*
                      user_proc_slot_base
 *======================================================================*/
/* 槽位 i 的物理内存基址 = USER_PROC_BASE + i * USER_PROC_SIZE */
PRIVATE t_32 user_proc_slot_base(int slot)
{
	return USER_PROC_BASE + (t_32)slot * USER_PROC_SIZE;
}

/*======================================================================*
                      find_free_user_slot
 *======================================================================*/
/* 在 proc_table 中找空闲的用户进程槽位 (is_user_proc==0 && priority==0)
 * 返回槽位在 proc_table 中的索引, 或 -1 表示已满 */
PRIVATE int find_free_user_slot()
{
	int i;
	for (i = 0; i < NR_USER_PROCS; i++) {
		PROCESS* p = &proc_table[NR_TASKS + NR_PROCS + i];
		if (!p->is_user_proc && p->priority == 0) {
			return i;
		}
	}
	return -1;
}

/*======================================================================*
                      init_user_ldt_descriptor
 *======================================================================*/
/* 在 GDT 中设置用户进程的 LDT 描述符 (按槽位索引分配) */
PRIVATE void init_user_ldt_descriptor(PROCESS* p_proc, int slot)
{
	DESCRIPTOR* desc = &gdt[INDEX_LDT_FIRST + (NR_TASKS + NR_PROCS) + slot];
	t_32 base = (t_32)p_proc->ldts;
	t_32 limit = sizeof(p_proc->ldts) - 1;

	desc->limit_low   = limit & 0xFFFF;
	desc->base_low    = base & 0xFFFF;
	desc->base_mid    = (base >> 16) & 0xFF;
	desc->attr1       = DA_LDT;
	desc->limit_high_attr2 = ((limit >> 16) & 0x0F);
	desc->base_high   = (base >> 24) & 0xFF;
}

/*======================================================================*
                      setup_user_segments
 *======================================================================*/
/* 设置用户进程的 LDT 段描述符 (CS=代码, DS=数据), 按槽位分配独立内存区 */
PRIVATE void setup_user_segments(PROCESS* p_proc, int slot)
{
	DESCRIPTOR* cs_desc = &p_proc->ldts[0];
	DESCRIPTOR* ds_desc = &p_proc->ldts[1];
	t_32 base = user_proc_slot_base(slot);
	t_32 limit = USER_PROC_SIZE - 1; /* 0x1FFFF (128KB - 1, inclusive) */

	/* CS: 代码段, base=槽位基址, limit=128KB, DPL=3, 32位 */
	cs_desc->limit_low   = limit & 0xFFFF;
	cs_desc->base_low    = base & 0xFFFF;
	cs_desc->base_mid    = (base >> 16) & 0xFF;
	cs_desc->attr1       = DA_CR | DA_DPL3;
	cs_desc->limit_high_attr2 = ((DA_32 >> 8) & 0xF0) | ((limit >> 16) & 0x0F);
	cs_desc->base_high   = (base >> 24) & 0xFF;

	/* DS/SS: 数据段, 同 base/limit, DPL=3 */
	ds_desc->limit_low   = limit & 0xFFFF;
	ds_desc->base_low    = base & 0xFFFF;
	ds_desc->base_mid    = (base >> 16) & 0xFF;
	ds_desc->attr1       = DA_DRW | DA_DPL3;
	ds_desc->limit_high_attr2 = ((DA_32 >> 8) & 0xF0) | ((limit >> 16) & 0x0F);
	ds_desc->base_high   = (base >> 24) & 0xFF;
}

/*======================================================================*
                      exec_user_program
 *======================================================================*/
/*
 * 加载并执行 CE (CatOS Executable) 格式的用户程序
 * 返回值:
 *   0 = 成功
 *  -1 = 文件不存在
 *  -2 = 格式错误 (magic/version 不匹配)
 *  -3 = 程序过大
 *  -4 = 槽位已满
 *  -5 = 文件损坏 (大小校验失败)
 */
#define CE_HEADER_SIZE		64

/* 手动读取字段的宏 (避免结构体对齐问题) */
#define CE_GET_MAGIC(p)		(*(t_32*)((p) + 0))
#define CE_GET_VERSION(p)	(*(t_16*)((p) + 4))
#define CE_GET_FLAGS(p)		(*(t_16*)((p) + 6))
#define CE_GET_ENTRY(p)		(*(t_32*)((p) + 8))
#define CE_GET_TEXTSZ(p)	(*(t_32*)((p) + 12))
#define CE_GET_DATASZ(p)	(*(t_32*)((p) + 16))
#define CE_GET_BSSSZ(p)		(*(t_32*)((p) + 20))
#define CE_GET_STACKSZ(p)	(*(t_32*)((p) + 24))
#define CE_GET_TOTALSZ(p)	(*(t_32*)((p) + 28))
#define CE_NAME_OFFSET		32

PUBLIC int exec_debug_n = 0;

/* 在指定目录中加载并执行用户程序
 * dir_cluster=0 表示根目录, >=2 表示子目录首簇
 */
PUBLIC int exec_user_program_in(t_32 dir_cluster, const char* filename)
{
	int n;
	t_8* hdr;
	PROCESS* p_proc;
	t_8* dst;
	t_32 total;
	t_32 text_sz, data_sz, bss_sz;
	t_32 entry;
	int slot;
	t_32 slot_base;
	t_16 ldt_selector;

	/* 1. 找空闲槽位 */
	slot = find_free_user_slot();
	if (slot < 0) {
		return -4;
	}

	n = fat_read_file_in(dir_cluster, filename, file_buf, FILE_BUF_SIZE);
	exec_debug_n = n;
	if (n < 0) {
		return -1;
	}

	if (n < CE_HEADER_SIZE) {
		return -21;
	}
	hdr = (t_8*)file_buf;

	if (CE_GET_MAGIC(hdr) != CE_MAGIC) {
		return -22;
	}

	if (CE_GET_VERSION(hdr) != CE_VERSION) {
		return -23;
	}

	text_sz = CE_GET_TEXTSZ(hdr);
	data_sz = CE_GET_DATASZ(hdr);
	bss_sz = CE_GET_BSSSZ(hdr);
	total = text_sz + data_sz + bss_sz;

	if (CE_GET_TOTALSZ(hdr) != 0 && CE_GET_TOTALSZ(hdr) != total) {
		return -5;
	}

	if (total > USER_PROC_MAX_SIZE) {
		return -3;
	}

	if (n < CE_HEADER_SIZE + (int)(text_sz + data_sz)) {
		return -5;
	}

	/* 2. 拷贝到该槽位的独立内存区 */
	slot_base = user_proc_slot_base(slot);
	dst = (t_8*)slot_base;
	memcpy(dst, file_buf + CE_HEADER_SIZE, text_sz + data_sz);

	memset(dst + text_sz + data_sz, 0, bss_sz);

	entry = CE_GET_ENTRY(hdr);

	/* 3. 设置进程表项 */
	p_proc = &proc_table[NR_TASKS + NR_PROCS + slot];
	memset(p_proc, 0, sizeof(PROCESS));

	setup_user_segments(p_proc, slot);
	init_user_ldt_descriptor(p_proc, slot);
	ldt_selector = (INDEX_LDT_FIRST + (NR_TASKS + NR_PROCS) + slot) << 3;
	p_proc->ldt_sel = ldt_selector;

	p_proc->regs.cs  = 0x07;	/* LDT[0] | RPL3 | SA_TIL */
	p_proc->regs.ds  = 0x0F;	/* LDT[1] | RPL3 | SA_TIL */
	p_proc->regs.es  = 0x0F;
	p_proc->regs.fs  = 0x0F;
	p_proc->regs.ss  = 0x0F;
	p_proc->regs.gs  = 0;
	p_proc->regs.eip = entry;
	/* 栈顶 = 槽位基址 + USER_PROC_SIZE/2, 段内偏移 = USER_PROC_SIZE/2 */
	p_proc->regs.esp = USER_PROC_SIZE / 2;
	p_proc->regs.eflags = 0x202;	/* IF=1, IOPL=0 (ring3) */
	p_proc->regs.eax = 0;
	p_proc->regs.ebx = 0;
	p_proc->regs.ecx = 0;
	p_proc->regs.edx = 0;
	p_proc->regs.esi = 0;
	p_proc->regs.edi = 0;
	p_proc->regs.ebp = 0;

	p_proc->pid = NR_TASKS + NR_PROCS + slot;
	p_proc->is_user_proc = 1;
	p_proc->exit_code = 0;
	p_proc->ticks = 5;
	p_proc->priority = 5;
	p_proc->nr_tty = 0;

	{
		int i;
		const char* name = (const char*)(hdr + CE_NAME_OFFSET);
		for (i = 0; i < 15 && name[i] != '\0'; i++) {
			p_proc->name[i] = name[i];
		}
		p_proc->name[i] = '\0';
	}
	/* 如果名称为空, 使用默认名 */
	if (p_proc->name[0] == '\0') {
		strcpy(p_proc->name, "user");
	}

	/* 4. 激活: 占用槽位, 切换到新进程 */
	user_proc_free_slots--;
	p_proc_ready = p_proc;

	/* 复位用户程序默认文本属性, 避免上一个程序 set_color 的颜色泄漏 */
	g_user_attr = 0x0F;
	/* 复位文件 IO 缓冲区大小 */
	g_io_size = 0;

	/* 创建控制台窗口 (仅在图形桌面模式下).
	 * 文本模式下用户程序输出直接到 shell 控制台, 无需控制台窗口. */
	if (g_video_mode != 0) {
		wm_create_user_console(slot, p_proc->name);
	}

	return slot;	/* 返回槽位号 (>=0 表示成功) */
}

/*======================================================================*
                      exec_user_program  (向后兼容包装)
 *======================================================================*/
/* 旧接口: 从根目录加载用户程序.
 * 保留以兼容 syscall_c.c (sys_spawn) 等旧调用点.
 * 新代码应直接使用 exec_user_program_in(dir_cluster, filename). */
PUBLIC int exec_user_program(const char* filename)
{
	return exec_user_program_in(0, filename);
}
