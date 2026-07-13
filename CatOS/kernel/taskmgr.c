/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               taskmgr.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS Task Manager — 第一个应用程序

  功能概述:
    查看 (View)   — 显示所有进程的 PID / 名称 / 优先级 / Ticks / 状态
    更新 (Update) — +/- 调整选中进程的优先级 (范围 1~50)
    删除 (Delete) — D 键将进程从调度器中移除 (设 priority=ticks=0)
    创建 (Create) — C 键恢复已删除的进程 (从备份恢复优先级)

  键盘操作:
    ↑ / ↓       移动选中行
    + 或 =      优先级 +1
    - 或 _      优先级 -1
    D           删除(停止)选中进程
    C           创建(恢复)已停止进程
    R           刷新显示
    Ctrl-C      安全退出

  设计说明:
    1. proc_table[] 大小固定 (NR_TASKS+NR_PROCS=4), 无法动态创建新进程;
       "创建"操作指恢复已停止的进程, "删除"指从调度中移除。
    2. PID 0 (task_tty) 是系统任务, 不可删除, 否则系统冻结。
    3. 通过设置 priority=0 使调度器跳过该进程 (schedule() 中
       `if (p->ticks > greatest_ticks)` 不满足 0>0, 故不会被选中)。
    4. 退出时清屏并将光标归零, shell 在左上角重新打印提示符。

  编码约定 (与 shell.c / tty.c 一致):
    - 所有字符串使用全局命名字符串数组 (放入 .data 段)
    - 内联汇编使用 32 位寄存器 (edi/esi/ecx)
    - update_hw_cursor 使用 "b" 约束固定 EBX, 避免 AL 冲突
    - push/pop (4字节) 而非 pushw/popw (2字节), 保持栈对齐
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "taskmgr.h"
#include "proto.h"
#include "gfx.h"


/*======================================================================
  屏幕常量 (与 shell.c / tty.c 保持一致)
  TM_SCR_W 使用动态 g_text_cols, 适配 1920x1080 (120列) 等高分辨率
======================================================================*/
#define TM_SCR_W    g_text_cols         /* 屏幕宽度 (字符, 动态) */
#define TM_SCR_H    25                  /* 界面高度 (行)   */
#define TM_BPC      2                   /* 每字符字节数    */
#define TM_ROW      (TM_SCR_W * TM_BPC) /* 每行字节数 */
#define TM_MAX      (TM_SCR_H * TM_ROW) /* 界面总字节 */

/* 界面垂直居中偏移 (清屏用全屏, 绘制用25行居中) */
static int tm_row_offset = 0;

/* 动态行布局 (基于 g_text_rows) */
static int tm_row_content_bot;
static int tm_row_sep_bottom;
static int tm_row_help;
static int tm_row_status;

/*======================================================================
  颜色定义 (VGA 文本模式属性字节)
======================================================================*/
#define COL_TITLE      0x0A   /* 绿色       — 标题     */
#define COL_HEADER     0x0E   /* 黄色       — 表头     */
#define COL_SEP        0x08   /* 灰色       — 分隔线   */
#define COL_DATA       0x07   /* 白字黑底   — 普通行   */
#define COL_SELECTED   0x1F   /* 白字蓝底   — 选中行   */
#define COL_RUNNING    0x0B   /* 青色       — RUNNING  */
#define COL_STOPPED    0x0C   /* 红色       — STOPPED  */
#define COL_HELP       0x03   /* 青色       — 帮助信息 */
#define COL_STATUS     0x07   /* 白色       — 状态行   */
#define COL_ERROR      0x0C   /* 红色       — 错误信息 */

/*======================================================================
  表格布局 (列号, 单位: 字符)
======================================================================*/
#define COL_PID_START     2
#define COL_NAME_START    8
#define COL_PRIO_START   24
#define COL_TICKS_START  36
#define COL_STATUS_START 46
#define COL_MARK_START   60

/* 字段宽度 */
#define W_PID     4
#define W_NAME    16
#define W_PRIO    4
#define W_TICKS   6
#define W_STATUS  8
#define W_MARK    3

/* 总进程数 (NR_TASKS + NR_PROCS = 4) */
#define TM_NR_PROCS  (NR_TASKS + NR_PROCS)

/*======================================================================
  页面定义 (Left/Right 方向键切换)
======================================================================*/
#define PAGE_PROCS  0   /* 进程列表页 */
#define PAGE_CPU    1   /* CPU 信息页 */
#define PAGE_RAM    2   /* 内存信息页 */
#define PAGE_COUNT  3

/*======================================================================
  行布局 (页面无关的公共框架)
    Row 0  : 标题
    Row 1  : 页面指示器 (1.Procs  2.CPU  3.RAM)
    Row 2  : 分隔线
    Row 3~21: 内容区域 (各页自定义)
    Row 22 : 分隔线
    Row 23 : 帮助/导航
    Row 24 : 状态行
======================================================================*/
#define ROW_TITLE        0
#define ROW_PAGEIND      1                            /* 页面指示器 */
#define ROW_SEP_TOP      2                            /* 顶部分隔线 */
#define ROW_HEADER       3                            /* 进程页表头 */
#define ROW_SEP1         4                            /* 表头下分隔线 */
#define ROW_DATA0        5                            /* 第一个数据行 */
#define ROW_CONTENT_TOP  3                            /* 内容区域起始行 */


/*======================================================================
  全局命名字符串变量 (放入 .data 段, 避免 .rodata 寻址问题)
  与 shell.c 采用相同策略
======================================================================*/
static char S_TITLE[]        = "=== CatOS Task Manager ===";
static char S_HDR_PID[]      = "PID";
static char S_HDR_NAME[]     = "NAME";
static char S_HDR_PRIO[]     = "PRIORITY";
static char S_HDR_TICKS[]    = "TICKS";
static char S_HDR_STATUS[]   = "STATUS";
static char S_RUNNING[]      = "RUNNING";
static char S_STOPPED[]      = "STOPPED";
static char S_MARK[]         = "<==";
static char S_HELP[]         = "[<-/->]Page [Up/Dn]Select [+/-]Prio [D]el [C]reate [R]efresh [Ctrl-C]Exit";
static char S_STATUS_READY[] = "Status: Ready";
static char S_STATUS_DEL[]   = "Status: Process stopped (removed from scheduler)";
static char S_STATUS_CRE[]   = "Status: Process resumed (re-added to scheduler)";
static char S_STATUS_UP[]    = "Status: Priority increased";
static char S_STATUS_DN[]    = "Status: Priority decreased";
static char S_STATUS_PROT[]  = "Status: ERROR - System task (PID 0) cannot be stopped";
static char S_STATUS_ALDR[]  = "Status: Process already running";
static char S_STATUS_ALST[]  = "Status: Process already stopped";

/* 页面指示器字符串 */
static char S_PAGE_TAG[]     = "Page: ";
static char S_PAGE_PROCS[]   = "Procs";
static char S_PAGE_CPU[]     = "CPU";
static char S_PAGE_RAM[]     = "RAM";

/* CPU 信息页字符串 */
static char S_CPU_TITLE[]    = "--- CPU Information ---";
static char S_CPU_VENDOR[]   = "Vendor:    ";
static char S_CPU_FAMILY[]   = "Family:    ";
static char S_CPU_MODEL[]    = "Model:     ";
static char S_CPU_STEPPING[] = "Stepping:  ";
static char S_CPU_FEATURES[] = "Features:";
static char S_CPU_F_FPU[]    = "FPU ";
static char S_CPU_F_TSC[]    = "TSC ";
static char S_CPU_F_MSR[]    = "MSR ";
static char S_CPU_F_MMX[]    = "MMX ";
static char S_CPU_F_SSE[]    = "SSE ";
static char S_CPU_F_SSE2[]   = "SSE2 ";
static char S_CPU_NONE[]     = "(none)";

/* 内存信息页字符串 */
static char S_RAM_TITLE[]    = "--- Memory Information ---";
static char S_RAM_CONV[]     = "Conventional: ";
static char S_RAM_EXT[]      = "Extended:     ";
static char S_RAM_TOTAL[]    = "Total:        ";
static char S_RAM_KB[]       = " KB";
static char S_RAM_MB[]       = " MB";
static char S_RAM_APPROX[]   = "(~";
static char S_RAM_NOTE[]     = "Conventional = low 640KB (BIOS 0x413)";
static char S_RAM_NOTE2[]    = "Extended = above 1MB (CMOS 0x30/0x31)";


/*======================================================================
  写单个字符到 VGA 显存 (统一封装, 支持垂直居中偏移)
======================================================================*/
static inline void tm_vm_write(int pos, char ch, unsigned char color)
{
	/* pos 是基于 80x25 虚拟屏幕的位置, 转换为实际屏幕位置 */
	int row = pos / (TM_SCR_W * TM_BPC);
	int col = (pos % (TM_SCR_W * TM_BPC)) / TM_BPC;
	int real_pos = ((row + tm_row_offset) * TM_SCR_W + col) * TM_BPC;
	vm_putc(real_pos, ch, color);
}

/*======================================================================
  更新 VGA 硬件光标位置 (闪烁下划线)
  使用 "b" 约束固定到 EBX, 避免 AL 被 movb 指令破坏
======================================================================*/
static inline void tm_update_hw_cursor(int byte_pos)
{
	if (g_video_mode != 0) {
		/* 图形模式: 使用统一的软件光标管理 */
		gfx_set_cursor(byte_pos);
	} else {
		/* 文本模式: VGA CRTC 寄存器 */
		int char_pos = byte_pos / 2;
		__asm__ __volatile__(
			"movw $0x3D4, %%dx \n\t"
			"movb $0x0E, %%al \n\t"
			"outb %%al, %%dx \n\t"
			"incw %%dx \n\t"
			"movb %%bh, %%al \n\t"
			"outb %%al, %%dx \n\t"
			"decw %%dx \n\t"
			"movb $0x0F, %%al \n\t"
			"outb %%al, %%dx \n\t"
			"incw %%dx \n\t"
			"movb %%bl, %%al \n\t"
			"outb %%al, %%dx"
			:
			: "b"(char_pos)
			: "eax", "edx", "memory"
		);
	}
}


/*======================================================================
  清屏: 清除整个屏幕, 计算动态布局
======================================================================*/
static void tm_clear_screen(void)
{
	int i;
	int total = TM_SCR_W * g_text_rows;
	for (i = 0; i < total; i++) {
		vm_putc(i * TM_BPC, ' ', 0x07);
	}
	/* 全屏显示 (不居中) */
	tm_row_offset = 0;
	/* 计算动态行布局: 底部3行固定 */
	tm_row_status = g_text_rows - 1;
	tm_row_help = g_text_rows - 2;
	tm_row_sep_bottom = g_text_rows - 3;
	tm_row_content_bot = g_text_rows - 4;
}

/*======================================================================
  在指定行、列起始位置写 N 个空格 (用于字段间填充)
  单次写入最终内容, 避免先清后写导致闪烁
======================================================================*/
static void tm_put_spaces(int row, int col, int width, unsigned char color)
{
	int pos = (row * TM_SCR_W + col) * TM_BPC;
	while (width > 0) {
		tm_vm_write(pos, ' ', color);
		pos += TM_BPC;
		width--;
	}
}

/*======================================================================
  在指定行、列位置写一个字符
======================================================================*/
static void tm_put_char_at(int row, int col, char ch, unsigned char color)
{
	tm_vm_write((row * TM_SCR_W + col) * TM_BPC, ch, color);
}

/*======================================================================
  在指定行、列起始位置写字符串 (不补空格, 写到 '\0' 为止)
======================================================================*/
static void tm_put_str_at(int row, int col, const char *s, unsigned char color)
{
	int pos = (row * TM_SCR_W + col) * TM_BPC;
	while (*s) {
		tm_vm_write(pos, *s, color);
		pos += TM_BPC;
		s++;
	}
}

/*======================================================================
  写固定宽度字符串 (左对齐, 右补空格到 width)
  用于表格单元格, 确保覆盖旧内容 (不留残留字符)
======================================================================*/
static void tm_put_field(int row, int col, int width, const char *s, unsigned char color)
{
	int pos = (row * TM_SCR_W + col) * TM_BPC;
	int w = 0;
	while (*s && w < width) {
		tm_vm_write(pos, *s, color);
		pos += TM_BPC;
		s++;
		w++;
	}
	while (w < width) {
		tm_vm_write(pos, ' ', color);
		pos += TM_BPC;
		w++;
	}
}

/*======================================================================
  写固定宽度整数 (右对齐, 左补空格到 width)
  用于 PID / PRIORITY / TICKS 等数值列
======================================================================*/
static void tm_put_int_field(int row, int col, int width, int value, unsigned char color)
{
	char buf[12];
	int i = 0, neg = 0, len, pad;
	int pos;

	if (value < 0) { neg = 1; value = -value; }
	if (value == 0) {
		buf[i++] = '0';
	} else {
		while (value > 0 && i < 11) {
			buf[i++] = (char)('0' + (value % 10));
			value /= 10;
		}
	}
	len = i + (neg ? 1 : 0);

	pos = (row * TM_SCR_W + col) * TM_BPC;
	pad = width - len;
	while (pad > 0) {
		tm_vm_write(pos, ' ', color);
		pos += TM_BPC;
		pad--;
	}
	if (neg) {
		tm_vm_write(pos, '-', color);
		pos += TM_BPC;
	}
	while (--i >= 0) {
		tm_vm_write(pos, buf[i], color);
		pos += TM_BPC;
	}
}

/*======================================================================
  画分隔线: 整行填充 '-' 字符
======================================================================*/
static void tm_draw_sep(int row)
{
	int col;
	for (col = 0; col < TM_SCR_W; col++) {
		tm_put_char_at(row, col, '-', COL_SEP);
	}
}

/*======================================================================
  字符串长度 (不依赖外部 strlen)
======================================================================*/
static int tm_strlen(const char *s)
{
	int n = 0;
	while (*s++) n++;
	return n;
}


/*======================================================================
  CPU / RAM 硬件信息获取 (与 shell.c sysinfo 同源)
  注: cpuid / in / out 在 IOPL=1 时可在 ring 1 中使用
======================================================================*/

/* CPUID EAX=0 获取厂商字符串 (12字节) */
static void tm_get_cpu_vendor(char *buf)
{
	int eax, ebx, ecx, edx;
	__asm__ __volatile__(
		"movl $0, %%eax \n\t"
		"cpuid          \n\t"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
	);
	buf[0]  = (char)(ebx & 0xFF);
	buf[1]  = (char)((ebx >> 8) & 0xFF);
	buf[2]  = (char)((ebx >> 16) & 0xFF);
	buf[3]  = (char)((ebx >> 24) & 0xFF);
	buf[4]  = (char)(edx & 0xFF);
	buf[5]  = (char)((edx >> 8) & 0xFF);
	buf[6]  = (char)((edx >> 16) & 0xFF);
	buf[7]  = (char)((edx >> 24) & 0xFF);
	buf[8]  = (char)(ecx & 0xFF);
	buf[9]  = (char)((ecx >> 8) & 0xFF);
	buf[10] = (char)((ecx >> 16) & 0xFF);
	buf[11] = (char)((ecx >> 24) & 0xFF);
	buf[12] = '\0';
}

/* CPUID EAX=1 获取 signature (EAX) 和 特性 (EDX)
 * EAX[3:0]=stepping, EAX[7:4]=model, EAX[11:8]=family
 * EDX bit0=FPU, bit4=TSC, bit5=MSR, bit23=MMX, bit25=SSE, bit26=SSE2
 * 注: ebx/ecx 用输出操作数而非 clobber, 避免 PIC 模式冲突 (与 shell.c 一致) */
static int tm_get_cpu_features(int *family, int *model, int *stepping)
{
	int eax, ebx, ecx, edx;
	__asm__ __volatile__(
		"movl $1, %%eax \n\t"
		"cpuid          \n\t"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
	);
	*stepping = eax & 0x0F;
	*model    = (eax >> 4) & 0x0F;
	*family   = (eax >> 8) & 0x0F;
	return edx;
}

/* CMOS 读取一个字节 */
static inline unsigned char tm_cmos_read(unsigned char reg)
{
	unsigned char val;
	__asm__ __volatile__(
		"outb %%al, $0x70 \n\t"
		"inb  $0x71, %%al"
		: "=a"(val)
		: "0"(reg)
	);
	return val;
}

/* 常规内存 (KB): BIOS 数据区 0x413, 低 640KB */
static inline int tm_get_conv_mem_kb(void)
{
	int conv_kb;
	__asm__ __volatile__(
		"movw 0x413, %w0"
		: "=a"(conv_kb)
	);
	return conv_kb;
}

/* 扩展内存 (KB): CMOS 0x30/0x31, 1MB 以上 (16位, 最大 65535KB ≈ 64MB) */
static inline int tm_get_ext_mem_kb(void)
{
	int ext_kb;
	ext_kb  = tm_cmos_read(0x30);
	ext_kb |= tm_cmos_read(0x31) << 8;
	return ext_kb;
}

/* 16MB 以上扩展内存 (以 64KB 为单位): CMOS 0x34/0x35
 * 当内存 > 64MB 时, 0x30/0x31 溢出饱和在 65535,
 * 必须读取 0x34/0x35 才能得到正确总量 */
static inline int tm_get_ext_above_16mb(void)
{
	int val;
	val  = tm_cmos_read(0x34);
	val |= tm_cmos_read(0x35) << 8;
	return val;  /* 单位: 64KB 块数 */
}

/* 总内存 (KB): 综合 0x413 + 0x30/0x31 + 0x34/0x35 */
static int tm_get_total_mem_kb(void)
{
	int conv_kb = tm_get_conv_mem_kb();
	int ext_kb  = tm_get_ext_mem_kb();
	int above16 = tm_get_ext_above_16mb();

	if (above16 > 0) {
		/* 内存 > 16MB: 使用 0x34/0x35 (640KB + 15MB + above16*64KB)
		 * 0x30/0x31 在 >64MB 时饱和不可靠, 用 0x34/0x35 更准确 */
		return 640 + 15 * 1024 + above16 * 64;
	}
	/* 内存 <= 16MB: 0x30/0x31 可靠 */
	return conv_kb + ext_kb;
}


/*======================================================================
  状态备份: 保存原始优先级, 用于 "创建" 操作恢复
  (BSS 段变量, 由 kernel.asm 的 _start 清零, 首次使用为 0)
======================================================================*/
static int tm_orig_priority[TM_NR_PROCS];  /* 原始优先级备份     */
static int tm_active[TM_NR_PROCS];          /* 1=活跃, 0=已停止   */
static int tm_selected = 0;                  /* 当前选中索引       */
static int tm_initialized = 0;               /* 是否已初始化       */
static int tm_current_page = PAGE_PROCS;     /* 当前页面 (PROCS/CPU/RAM) */

/* 首次调用时初始化备份 (读取 proc_table 中的初始优先级) */
static void tm_init_backup(void)
{
	int i;
	if (tm_initialized) return;
	for (i = 0; i < TM_NR_PROCS; i++) {
		tm_orig_priority[i] = proc_table[i].priority;
		tm_active[i] = 1;
	}
	tm_initialized = 1;
}


/*======================================================================
  绘制公共框架 (页面无关): 标题、页面指示器、顶/底分隔线、帮助
  进入 taskmgr 及切换页面时调用
======================================================================*/
static void tm_draw_static(void)
{
	int len;
	const char *p;

	/* 第 0 行: 标题 (居中显示, 绿色) */
	len = 0;
	p = S_TITLE;
	while (*p) { len++; p++; }
	tm_put_str_at(ROW_TITLE, (TM_SCR_W - len) / 2, S_TITLE, COL_TITLE);

	/* 第 1 行: 页面指示器 */
	tm_put_spaces(ROW_PAGEIND, 0, TM_SCR_W, COL_DATA);
	tm_put_str_at(ROW_PAGEIND, 2, S_PAGE_TAG, COL_HEADER);
	{
		const char *names[PAGE_COUNT];
		int i, col = 2 + tm_strlen(S_PAGE_TAG);
		names[0] = S_PAGE_PROCS;
		names[1] = S_PAGE_CPU;
		names[2] = S_PAGE_RAM;
		for (i = 0; i < PAGE_COUNT; i++) {
			unsigned char color = (i == tm_current_page) ? COL_SELECTED : COL_HEADER;
			tm_put_char_at(ROW_PAGEIND, col, '1' + i, color); col++;
			tm_put_char_at(ROW_PAGEIND, col, '.', color);     col++;
			tm_put_str_at(ROW_PAGEIND, col, names[i], color);
			col += tm_strlen(names[i]);
			col += 4;  /* 标签间距 */
		}
	}

	/* 顶/底分隔线 (灰色) */
	tm_draw_sep(ROW_SEP_TOP);
	tm_draw_sep(tm_row_sep_bottom);

	/* 帮助/导航信息 (青色) */
	tm_put_str_at(tm_row_help, 0, S_HELP, COL_HELP);
}

/*======================================================================
  清除内容区域 (ROW_CONTENT_TOP ~ tm_row_content_bot)
  切换页面时调用, 避免上一页残留字符
======================================================================*/
static void tm_clear_content(void)
{
	int row, col;
	for (row = ROW_CONTENT_TOP; row <= tm_row_content_bot; row++) {
		for (col = 0; col < TM_SCR_W; col++) {
			tm_put_char_at(row, col, ' ', COL_DATA);
		}
	}
}

/*======================================================================
  CPU 信息页
======================================================================*/
static void tm_draw_cpu_page(void)
{
	char vendor[16];
	int family, model, stepping, features;
	int row = ROW_CONTENT_TOP;

	tm_clear_content();

	/* 标题 */
	tm_put_str_at(row, 2, S_CPU_TITLE, COL_TITLE);
	row += 2;

	/* 厂商字符串 */
	tm_get_cpu_vendor(vendor);
	tm_put_str_at(row, 2, S_CPU_VENDOR, COL_HEADER);
	tm_put_str_at(row, 16, vendor, COL_DATA);
	row++;

	/* Signature */
	features = tm_get_cpu_features(&family, &model, &stepping);
	tm_put_str_at(row, 2, S_CPU_FAMILY, COL_HEADER);
	tm_put_int_field(row, 16, 4, family, COL_DATA);
	row++;
	tm_put_str_at(row, 2, S_CPU_MODEL, COL_HEADER);
	tm_put_int_field(row, 16, 4, model, COL_DATA);
	row++;
	tm_put_str_at(row, 2, S_CPU_STEPPING, COL_HEADER);
	tm_put_int_field(row, 16, 4, stepping, COL_DATA);
	row += 2;

	/* 特性标志 (EDX 各 bit) */
	tm_put_str_at(row, 2, S_CPU_FEATURES, COL_HEADER);
	row++;
	{
		int col = 4;
		int wrote = 0;
		if (features & (1 << 0))  { tm_put_str_at(row, col, S_CPU_F_FPU,  COL_RUNNING); col += tm_strlen(S_CPU_F_FPU);  wrote = 1; }
		if (features & (1 << 4))  { tm_put_str_at(row, col, S_CPU_F_TSC,  COL_RUNNING); col += tm_strlen(S_CPU_F_TSC);  wrote = 1; }
		if (features & (1 << 5))  { tm_put_str_at(row, col, S_CPU_F_MSR,  COL_RUNNING); col += tm_strlen(S_CPU_F_MSR);  wrote = 1; }
		if (features & (1 << 23)) { tm_put_str_at(row, col, S_CPU_F_MMX,  COL_RUNNING); col += tm_strlen(S_CPU_F_MMX);  wrote = 1; }
		if (features & (1 << 25)) { tm_put_str_at(row, col, S_CPU_F_SSE,  COL_RUNNING); col += tm_strlen(S_CPU_F_SSE);  wrote = 1; }
		if (features & (1 << 26)) { tm_put_str_at(row, col, S_CPU_F_SSE2, COL_RUNNING); col += tm_strlen(S_CPU_F_SSE2); wrote = 1; }
		if (!wrote) {
			tm_put_str_at(row, col, S_CPU_NONE, COL_STOPPED);
		}
	}
}

/*======================================================================
  内存信息页
======================================================================*/
static void tm_draw_ram_page(void)
{
	int conv_kb, ext_kb, above16, total_kb, total_mb;
	int row = ROW_CONTENT_TOP;

	tm_clear_content();

	/* 标题 */
	tm_put_str_at(row, 2, S_RAM_TITLE, COL_TITLE);
	row += 2;

	conv_kb   = tm_get_conv_mem_kb();
	ext_kb    = tm_get_ext_mem_kb();
	above16   = tm_get_ext_above_16mb();
	total_kb  = tm_get_total_mem_kb();
	total_mb  = total_kb / 1024;

	/* 常规内存 */
	tm_put_str_at(row, 2, S_RAM_CONV, COL_HEADER);
	tm_put_int_field(row, 17, 8, conv_kb, COL_DATA);
	tm_put_str_at(row, 25, S_RAM_KB, COL_DATA);
	row++;

	/* 扩展内存 (1MB以上, 0x30/0x31, 最大64MB) */
	tm_put_str_at(row, 2, S_RAM_EXT, COL_HEADER);
	tm_put_int_field(row, 17, 8, ext_kb, COL_DATA);
	tm_put_str_at(row, 25, S_RAM_KB, COL_DATA);
	row++;

	/* 总内存 (KB + MB 近似) */
	tm_put_str_at(row, 2, S_RAM_TOTAL, COL_HEADER);
	tm_put_int_field(row, 17, 8, total_kb, COL_DATA);
	tm_put_str_at(row, 25, S_RAM_KB, COL_DATA);
	tm_put_str_at(row, 32, S_RAM_APPROX, COL_SEP);
	tm_put_int_field(row, 34, 6, total_mb, COL_DATA);
	tm_put_str_at(row, 40, S_RAM_MB, COL_DATA);
	row += 2;

	/* 说明 */
	tm_put_str_at(row, 2, S_RAM_NOTE, COL_SEP);
	row++;
	tm_put_str_at(row, 2, S_RAM_NOTE2, COL_SEP);
}

/*======================================================================
  绘制当前页面内容 (切换页面 / 进入时调用)
======================================================================*/
static void tm_draw_rows(void);  /* 前向声明: tm_draw_page 在定义之前调用它 */

static void tm_draw_page(void)
{
	switch (tm_current_page) {
	case PAGE_PROCS:
		/* 进程页: 表头 + 分隔线 + 数据行 */
		tm_clear_content();
		tm_put_str_at(ROW_HEADER, COL_PID_START,    S_HDR_PID,    COL_HEADER);
		tm_put_str_at(ROW_HEADER, COL_NAME_START,   S_HDR_NAME,   COL_HEADER);
		tm_put_str_at(ROW_HEADER, COL_PRIO_START,   S_HDR_PRIO,   COL_HEADER);
		tm_put_str_at(ROW_HEADER, COL_TICKS_START,  S_HDR_TICKS,  COL_HEADER);
		tm_put_str_at(ROW_HEADER, COL_STATUS_START, S_HDR_STATUS, COL_HEADER);
		tm_draw_sep(ROW_SEP1);
		tm_draw_rows();
		break;
	case PAGE_CPU:
		tm_draw_cpu_page();
		break;
	case PAGE_RAM:
		tm_draw_ram_page();
		break;
	}
}

/*======================================================================
  绘制单个进程数据行 (单次全覆盖写入, 不闪烁)
  index: 进程在 proc_table 中的索引 (0 ~ TM_NR_PROCS-1)

  布局 (80列全覆盖, 每个单元格只写一次):
    [边距]PID[间距]NAME(16)[间距]PRIO[间距]TICKS[间距]STATUS[间距]MARK[边距]
    0-1   2-5  6-7  8-23   24-27 28-35  36-41 42-45  46-53  54-59  60-62 63-79
======================================================================*/
static void tm_draw_row(int index)
{
	PROCESS *p = &proc_table[index];
	int row = ROW_DATA0 + index;
	unsigned char bg = (index == tm_selected) ? COL_SELECTED : COL_DATA;
	unsigned char status_color;
	const char *status_str;
	int col = 0;

	/* 左边距 (col 0-1) */
	tm_put_spaces(row, col, COL_PID_START - col, bg);
	col = COL_PID_START;

	/* PID (右对齐, W_PID=4) */
	tm_put_int_field(row, col, W_PID, (int)p->pid, bg);
	col += W_PID;

	/* 间距 (col 6-7) */
	tm_put_spaces(row, col, COL_NAME_START - col, bg);
	col = COL_NAME_START;

	/* NAME (左对齐, W_NAME=16) */
	tm_put_field(row, col, W_NAME, p->name, bg);
	col += W_NAME;

	/* 间距 (col 24-23 之间无间距, COL_PRIO_START=24=COL_NAME_START+W_NAME) */
	tm_put_spaces(row, col, COL_PRIO_START - col, bg);
	col = COL_PRIO_START;

	/* PRIORITY (右对齐, W_PRIO=4) */
	tm_put_int_field(row, col, W_PRIO, p->priority, bg);
	col += W_PRIO;

	/* 间距 (col 28-35) */
	tm_put_spaces(row, col, COL_TICKS_START - col, bg);
	col = COL_TICKS_START;

	/* TICKS (右对齐, W_TICKS=6, 实时变化) */
	tm_put_int_field(row, col, W_TICKS, p->ticks, bg);
	col += W_TICKS;

	/* 间距 (col 42-45) */
	tm_put_spaces(row, col, COL_STATUS_START - col, bg);
	col = COL_STATUS_START;

	/* STATUS: 活跃=青色, 停止=红色; 选中行统一用蓝底白字 */
	status_str = tm_active[index] ? S_RUNNING : S_STOPPED;
	status_color = tm_active[index] ? COL_RUNNING : COL_STOPPED;
	if (index == tm_selected) status_color = COL_SELECTED;
	tm_put_field(row, col, W_STATUS, status_str, status_color);
	col += W_STATUS;

	/* 间距 (col 54-59) */
	tm_put_spaces(row, col, COL_MARK_START - col, bg);
	col = COL_MARK_START;

	/* 选中标记: 选中行显示 "<==", 非选中行显示空格 (W_MARK=3) */
	if (index == tm_selected) {
		tm_put_field(row, col, W_MARK, S_MARK, bg);
	} else {
		tm_put_spaces(row, col, W_MARK, bg);
	}
	col += W_MARK;

	/* 右边距 (col 63-79) */
	tm_put_spaces(row, col, TM_SCR_W - col, bg);
}

/*======================================================================
  绘制所有进程数据行 (刷新整个表格区域)
======================================================================*/
static void tm_draw_rows(void)
{
	int i;
	for (i = 0; i < TM_NR_PROCS; i++) {
		tm_draw_row(i);
	}
}

/*======================================================================
  绘制状态行 (底部提示信息)
  用固定宽度字段覆盖整行 (80列), 避免先清后写导致闪烁
======================================================================*/
static void tm_draw_status(const char *msg, unsigned char color)
{
	tm_put_field(tm_row_status, 0, TM_SCR_W, msg, color);
}


/*======================================================================
  操作: 删除(停止)选中进程
  设置 priority=0, ticks=0, 调度器自然跳过该进程
  PID 0 (task_tty) 受保护, 不可删除 (否则系统冻结)
  返回: 1=状态已改变需重绘, 0=无变化
======================================================================*/
static int tm_do_delete(void)
{
	int idx = tm_selected;

	/* 保护系统任务 (task_tty, PID 0) */
	if (proc_table[idx].pid == 0) {
		tm_draw_status(S_STATUS_PROT, COL_ERROR);
		return 0;
	}
	if (!tm_active[idx]) {
		tm_draw_status(S_STATUS_ALST, COL_STATUS);
		return 0;
	}

	/* 停止进程: priority=0 使调度器重置后仍为 0, ticks=0 立即生效 */
	proc_table[idx].priority = 0;
	proc_table[idx].ticks = 0;
	tm_active[idx] = 0;

	tm_draw_status(S_STATUS_DEL, COL_RUNNING);
	return 1;
}

/*======================================================================
  操作: 创建(恢复)已停止的进程
  从备份恢复原始优先级, 重新加入调度
  返回: 1=状态已改变, 0=无变化
======================================================================*/
static int tm_do_create(void)
{
	int idx = tm_selected;

	if (tm_active[idx]) {
		tm_draw_status(S_STATUS_ALDR, COL_STATUS);
		return 0;
	}

	/* 恢复优先级, 设置 ticks 使其立即参与调度 */
	proc_table[idx].priority = tm_orig_priority[idx];
	proc_table[idx].ticks = tm_orig_priority[idx];
	tm_active[idx] = 1;

	tm_draw_status(S_STATUS_CRE, COL_RUNNING);
	return 1;
}

/*======================================================================
  操作: 调整优先级
  delta: +1 增加, -1 减少
  范围限制: 1 ~ 50 (最小 1 保证可被调度, 最大 50 防止霸占 CPU)
  返回: 1=状态已改变, 0=无变化
======================================================================*/
static int tm_do_adjust(int delta)
{
	int idx = tm_selected;
	int new_prio;

	if (!tm_active[idx]) {
		tm_draw_status(S_STATUS_ALST, COL_STATUS);
		return 0;
	}

	new_prio = proc_table[idx].priority + delta;
	if (new_prio < 1)  new_prio = 1;
	if (new_prio > 50) new_prio = 50;

	if (new_prio == proc_table[idx].priority) {
		return 0;  /* 已到边界, 无变化 */
	}

	proc_table[idx].priority = new_prio;
	tm_orig_priority[idx] = new_prio;  /* 同步更新备份 */
	/* ticks 若超过新优先级则截断, 让效果立即可见 */
	if (proc_table[idx].ticks > new_prio) {
		proc_table[idx].ticks = new_prio;
	}

	tm_draw_status(delta > 0 ? S_STATUS_UP : S_STATUS_DN, COL_RUNNING);
	return 1;
}


/*======================================================================
  taskmgr_run — 任务管理器主入口

  由 shell 命令 "taskmgr" 调用, 接管键盘和显示,
  直到用户按下 Ctrl-C 才返回。退出时清屏, 光标归零。

  主循环说明:
    - keyboard_read_simple() 非阻塞, 无键时返回 0
    - 时钟中断持续触发, 调度器正常运行, ticks 实时更新
    - 周期性刷新 (计数器控制) 更新 ticks 显示
    - 注意: 不能使用 hlt (进程运行在 ring 1, hlt 是 ring 0 特权指令)
======================================================================*/
PUBLIC void taskmgr_run(int *p_cursor)
{
	t_32 key;
	int refresh_cnt = 0;
	int need_redraw;
	int page_changed;

	/* 初始化备份 (仅首次调用) */
	tm_init_backup();
	/* 重置选中到第一个进程, 默认进程页 */
	tm_selected = 0;
	tm_current_page = PAGE_PROCS;

	/* ==== 清屏, 进入全屏界面 ==== */
	tm_clear_screen();
	tm_draw_static();
	tm_draw_page();
	tm_draw_status(S_STATUS_READY, COL_STATUS);
	/* 光标移到状态行, 减少视觉干扰 */
	tm_update_hw_cursor(tm_row_status * TM_ROW);

	/* ==== 主循环 ==== */
	while (1) {
		key = 0;
		keyboard_read_simple(&key);
		need_redraw = 0;
		page_changed = 0;

		if (key != 0) {
			/* ==== Ctrl-C: 安全退出 (最高优先级检查) ====
			 * Ctrl-C 的 key 值: 'c' | FLAG_CTRL_L = 0x0863 (左Ctrl)
			 *                或 'c' | FLAG_CTRL_R = 0x1063 (右Ctrl)
			 * FLAG_EXT (0x0100) 不置位 (普通字符 + Ctrl 修饰)
			 * 同时检查 'c' 和 'C' 以增强健壮性 */
			if (!(key & FLAG_EXT) &&
			    ((key & 0xFF) == 'c' || (key & 0xFF) == 'C') &&
			    ((key & FLAG_CTRL_L) || (key & FLAG_CTRL_R))) {
				break;
			}

			if (key & FLAG_EXT) {
				/* ==== 扩展键 (方向键等) ==== */
				int raw = key & MASK_RAW;
				if (raw == LEFT) {
					/* 上一页 (循环) */
					if (tm_current_page == 0) tm_current_page = PAGE_COUNT - 1;
					else tm_current_page--;
					page_changed = 1;
				} else if (raw == RIGHT) {
					/* 下一页 (循环) */
					if (tm_current_page == PAGE_COUNT - 1) tm_current_page = 0;
					else tm_current_page++;
					page_changed = 1;
				} else if (tm_current_page == PAGE_PROCS) {
					/* 进程页: Up/Down 移动选中行 */
					if (raw == UP) {
						if (tm_selected > 0) {
							tm_selected--;
							need_redraw = 1;
						}
					} else if (raw == DOWN) {
						if (tm_selected < TM_NR_PROCS - 1) {
							tm_selected++;
							need_redraw = 1;
						}
					}
				}
				/* 其他扩展键 (Enter/Tab/Backspace/F1~F12 等) 忽略 */
			} else {
				/* ==== 普通字符键 (仅在进程页响应操作键) ==== */
				char ch = (char)(key & 0xFF);

				/* 忽略带 Ctrl/Alt 修饰的键 (Ctrl-C 已处理, 避免误操作) */
				if (!((key & FLAG_CTRL_L) || (key & FLAG_CTRL_R) ||
				      (key & FLAG_ALT_L)  || (key & FLAG_ALT_R))) {
					/* 'r' 刷新在所有页面都可用, 其他操作键仅在进程页 */
					if (ch == 'r' || ch == 'R') {
						tm_draw_status(S_STATUS_READY, COL_STATUS);
						if (tm_current_page == PAGE_PROCS) {
							need_redraw = 1;
						} else {
							/* CPU/RAM 页: 重新获取硬件信息刷新 */
							page_changed = 1;
						}
					} else if (tm_current_page == PAGE_PROCS) {
						switch (ch) {
						case '+':
						case '=':
							need_redraw = tm_do_adjust(1);
							break;
						case '-':
						case '_':
							need_redraw = tm_do_adjust(-1);
							break;
						case 'd':
						case 'D':
							need_redraw = tm_do_delete();
							break;
						case 'c':
						case 'C':
							/* 注意: Ctrl-C 已在上面处理, 这里是普通 C 键 */
							need_redraw = tm_do_create();
							break;
						default:
							break;
						}
					}
				}
			}

			if (page_changed) {
				/* 切换页面: 重绘页面指示器 + 新页面内容 */
				tm_draw_static();
				tm_draw_page();
			} else if (need_redraw) {
				tm_draw_rows();
			}
		}

		/* ==== 周期性刷新 (仅进程页更新 ticks) ====
		 * CPU/RAM 信息基本静态, 无需频繁刷新
		 * 无 hlt, 循环极快; 用大计数器限制刷新频率约 2~5 次/秒
		 * 闪屏已通过单次全覆盖写入消除, 此处仅控制 ticks 更新频率 */
		if (++refresh_cnt >= 500000) {
			refresh_cnt = 0;
			if (tm_current_page == PAGE_PROCS) {
				tm_draw_rows();
			}
		}
	}

	/* ==== 退出清理: 清屏, 光标归零 ====
	 * 清屏后 shell 在左上角重新打印提示符
	 * 所有资源 (键盘缓冲区由内核管理, 无需手动释放) */
	tm_clear_screen();
	*p_cursor = 0;
	tm_update_hw_cursor(0);
}
