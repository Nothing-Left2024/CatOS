/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               installer.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS Interactive Installer — Windows XP style

  功能:
    - 全屏交互式安装向导 (类似 Windows XP 安装程序)
    - 多步骤页面: 欢迎 → 硬盘信息 → 确认 → 安装进度 → 完成/失败
    - ESC 可随时退出 (安装进度页除外)
    - 单次写入整行, 避免先清后写导致闪烁

  页面流程:
    WELCOME  → ENTER 继续 / ESC 退出
    DISKINFO → ENTER 继续 / ESC 返回
    CONFIRM  → Y 确认 / ESC 取消
    PROGRESS → 自动执行三步安装 (不可中断)
    COMPLETE → ENTER 返回 shell
    FAILED   → ENTER 返回 shell
    NOHD     → ENTER 返回 shell (无硬盘)

  编码约定 (与 shell.c / taskmgr.c 一致):
    - 所有字符串使用全局命名字符串数组 (放入 .data 段)
    - 内联汇编使用 32 位寄存器
    - 不使用 hlt (进程运行在 ring 1)
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "hd.h"
#include "fat.h"
#include "keyboard.h"
#include "proto.h"
#include "gfx.h"

/* 嵌入的引导二进制 (kernel.asm 中 incbin, 供安装使用) */
extern char _hdboot_bin[];
extern char _loader_bin[];
extern char _mbr_bin[];

/* 示例文件内容 (安装时写入硬盘 FAT32, 让文件管理器有内容显示) */
static char S_SAMPLE_README[] =
	"Welcome to CatOS!\r\n"
	"This is a FAT32 hard disk installed by setup.\r\n"
	"Use 'dir' to list files, 'type <name>' to read.\r\n"
	"Double-click files in the desktop file manager.\r\n";
static char S_SAMPLE_HELLO[] =
	"/* Hello World in C */\r\n"
	"/* CatOS desktop example file */\r\n";
static char S_SAMPLE_NOTES[] =
	"CatOS Development Notes\r\n"
	"=======================\r\n"
	"- Protected mode x86 OS\r\n"
	"- FAT32 file system\r\n"
	"- IDE/ATA PIO driver\r\n"
	"- Window manager with mouse\r\n"
	"- User programs (.ce format)\r\n";


/*======================================================================
  常量 (INST_SCR_W 使用动态 g_text_cols, 适配高分辨率)
======================================================================*/
#define INST_SCR_W	g_text_cols
#define INST_SCR_H	25
#define INST_BPC	2
#define INST_ROW	(INST_SCR_W * INST_BPC)
#define INST_MAX	(INST_SCR_H * INST_ROW)

/* 界面垂直居中偏移 (清屏用全屏, 绘制用25行居中) */
static int inst_row_offset = 0;

/* 动态行布局 (基于 g_text_rows) */
static int g_row_sep;
static int g_row_help;
static int g_row_status;
static int g_bar_row;

#ifndef KERNEL_FILE_SIZE
#define KERNEL_FILE_SIZE	40960
#endif
/* kernel.bin 加载到 0x10000 后, InitKernel 复制到 0x30400 (执行地址).
 * 由于 kernel 较大时源/目的重叠 (0x30400~0x386A0), 0x10000 处的原始副本被破坏.
 * setup 必须从 0x30400 读取完整的 kernel 数据写入硬盘. */
#define KERNEL_FILE_ADDR	0x30400

/* 颜色 (VGA 文本模式属性) */
#define COL_TITLE	0x1F	/* 白字蓝底 — 标题栏 */
#define COL_TEXT	0x07	/* 白字黑底 — 正文 */
#define COL_BRIGHT	0x0F	/* 亮白 — 强调 */
#define COL_YELLOW	0x0E	/* 黄色 — 警告/活动 */
#define COL_RED		0x0C	/* 红色 — 错误/危险 */
#define COL_GREEN	0x0A	/* 绿色 — 成功 */
#define COL_CYAN	0x0B	/* 青色 — 提示 */
#define COL_GRAY	0x08	/* 灰色 — 待处理 */
#define COL_BAR_FILL	0x0B	/* 进度条填充 */
#define COL_BAR_EMPTY	0x08	/* 进度条空白 */

/* 页面定义 */
enum {
	PAGE_WELCOME,
	PAGE_DISKINFO,
	PAGE_CONFIRM,
	PAGE_PROGRESS,
	PAGE_COMPLETE,
	PAGE_FAILED,
	PAGE_NOHD,
};

/* 行布局 */
#define ROW_TITLE	0
#define ROW_SUBTITLE	1
#define ROW_CONTENT0	3

/* 进度条参数 */
#define BAR_COL		20
#define BAR_WIDTH	40


/*======================================================================
  全局命名字符串变量 (放入 .data 段)
======================================================================*/
static char S_TITLE[]		= " CatOS Setup ";
static char S_SUB_WELCOME[]	= "Welcome to CatOS Installation";
static char S_SUB_DISK[]	= "Disk Information";
static char S_SUB_CONFIRM[]	= "Confirm Installation";
static char S_SUB_PROGRESS[]	= "Installing...";
static char S_SUB_COMPLETE[]	= "Installation Complete";
static char S_SUB_FAILED[]	= "Installation Failed";
static char S_SUB_NOHD[]	= "No Hard Disk Found";

/* 通用 */
static char S_SEP[]		= "--------------------------------------------------------------------------------";
static char S_HELP_CONTINUE[]	= "[ENTER] Continue    [ESC] Exit";
static char S_HELP_BACK[]	= "[ENTER] Continue    [ESC] Back";
static char S_HELP_CONFIRM[]	= "[Y] Confirm    [ESC] Cancel";
static char S_HELP_RETURN[]	= "[ENTER] Return to Shell";
static char S_HELP_WAIT[]	= "Please wait... Do not turn off your computer.";
static char S_STATUS_READY[]	= "Status: Ready";
static char S_STATUS_WORKING[]	= "Status: Working...";

/* 欢迎页 */
static char S_WELCOME_1[]	= "Welcome to CatOS Setup.";
static char S_WELCOME_2[]	= "This wizard will install CatOS on your hard disk.";
static char S_WELCOME_3[]	= "The installation includes:";
static char S_WELCOME_4[]	= "  - FAT32 file system";
static char S_WELCOME_5[]	= "  - Boot sector (VBR + MBR code)";
static char S_WELCOME_6[]	= "  - Loader (boot loader)";
static char S_WELCOME_7[]	= "  - Kernel (operating system)";
static char S_WELCOME_WARN[]	= "WARNING: All existing data on the disk will be lost!";

/* 硬盘信息页 */
static char S_DISK_HDR[]	= "Detected hard disk:";
static char S_DISK_MODEL[]	= "  Model:    ";
static char S_DISK_SIZE[]	= "  Size:     ";
static char S_DISK_SECTORS[]	= "  Sectors:  ";
static char S_DISK_MB[]		= " MB";
static char S_DISK_NOTE[]	= "CatOS will be installed on this disk.";
static char S_DISK_WARN[]	= "WARNING: All data will be lost!";

/* 确认页 */
static char S_CONFIRM_1[]	= "You are about to install CatOS on the following disk:";
static char S_CONFIRM_2[]	= "All data will be lost. This action cannot be undone.";
static char S_CONFIRM_3[]	= "Press Y to confirm, or ESC to cancel.";

/* 进度页 */
static char S_PROG_HDR[]	= "Installing CatOS. Please wait...";
static char S_PROG_STEP1[]	= "Step 1/3: Formatting FAT32";
static char S_PROG_STEP2[]	= "Step 2/3: Writing boot code + loader";
static char S_PROG_STEP3[]	= "Step 3/3: Writing kernel";
static char S_PROG_LABEL[]	= "Progress: ";

/* 步骤标记 */
static char S_MARK_PENDING[]	= "[ ]";
static char S_MARK_ACTIVE[]	= "[~]";
static char S_MARK_DONE[]	= "[v]";
static char S_MARK_FAILED[]	= "[X]";
static char S_STATUS_DONE[]	= "Done";
static char S_STATUS_WORK[]	= "Working...";
static char S_STATUS_WAIT[]	= "Pending";
static char S_STATUS_ERR[]	= "FAILED";

/* 完成页 */
static char S_COMPLETE_1[]	= "CatOS has been successfully installed!";
static char S_COMPLETE_2[]	= "Sectors written: ";
static char S_COMPLETE_3[]	= "To start CatOS:";
static char S_COMPLETE_4[]	= "  1. Remove the floppy disk";
static char S_COMPLETE_5[]	= "  2. Restart your computer";
static char S_COMPLETE_OK[]	= "Installation verified. Boot code OK.";

/* 失败页 */
static char S_FAILED_1[]	= "Installation failed at step ";
static char S_FAILED_2[]	= "Please check your hard disk and try again.";
static char S_FAIL_ERR[]	= "HD error code: ";
static char S_FAIL_LBA[]	= "Kernel LBA: ";
static char S_FAIL_TOTSEC[]	= "Disk sectors: ";
static char S_FAIL_KSECS[]	= "Kernel sectors: ";
static char S_FAIL_KSIZE[]	= "KERNEL_FILE_SIZE: ";
static char S_ERR_DESC_R[]	= " (hd not ready)";
static char S_ERR_DESC_D[]	= " (DRQ timeout)";
static char S_ERR_DESC_W[]	= " (write not complete)";
static char S_ERR_DESC_E[]	= " (disk ERR flag)";

/* 无硬盘页 */
static char S_NOHD_1[]		= "No hard disk was detected.";
static char S_NOHD_2[]		= "Make sure a hard disk is connected and try again.";


/*======================================================================
  诊断信息 (供 draw_failed 显示, inst_do_install 设置)
======================================================================*/
static int g_err_code    = 0;	/* hd_write_sectors/hd_read_sector 返回的错误码 */
static int g_err_kern_lba= 0;	/* 步骤3的 kernel_lba */
static int g_err_disk_sec= 0;	/* 硬盘总扇区数 */
static int g_err_kern_sec= 0;	/* kernel_sectors */
static int g_err_kern_sz = 0;	/* KERNEL_FILE_SIZE */


/*======================================================================
  写单个字符到 VGA 显存 (统一封装)
  支持垂直居中偏移
======================================================================*/
static inline void inst_vm_write(int pos, char ch, unsigned char color)
{
	/* pos 是基于 80x25 虚拟屏幕的位置, 转换为实际屏幕位置 */
	int row = pos / (INST_SCR_W * INST_BPC);
	int col = (pos % (INST_SCR_W * INST_BPC)) / INST_BPC;
	int real_pos = ((row + inst_row_offset) * INST_SCR_W + col) * INST_BPC;
	vm_putc(real_pos, ch, color);
}


/*======================================================================
  清屏 (清除整个屏幕)
======================================================================*/
static void inst_clear_screen(void)
{
	int i;
	int total = INST_SCR_W * g_text_rows;
	for (i = 0; i < total; i++) {
		vm_putc(i * INST_BPC, ' ', 0x07);
	}
	/* 全屏显示 (不居中) */
	inst_row_offset = 0;
	/* 计算动态行布局: 底部3行固定 */
	g_row_status = g_text_rows - 1;
	g_row_help = g_text_rows - 2;
	g_row_sep = g_text_rows - 3;
	g_bar_row = ROW_CONTENT0 + (g_row_sep - ROW_CONTENT0) / 2;
}


/*======================================================================
  在指定行、列位置写字符串 (不补空格)
======================================================================*/
static void inst_put_str_at(int row, int col, const char *s, unsigned char color)
{
	int pos = (row * INST_SCR_W + col) * INST_BPC;
	while (*s) {
		inst_vm_write(pos, *s, color);
		pos += INST_BPC;
		s++;
	}
}


/*======================================================================
  写整行: 字符串 + 填充空格到行尾 (单次写入, 无闪烁)
  用于绘制完整内容行, 确保旧行内容被完全覆盖
======================================================================*/
static void inst_put_full_row(int row, const char *s, unsigned char color)
{
	int pos = row * INST_SCR_W * INST_BPC;
	int col = 0;
	while (*s && col < INST_SCR_W) {
		inst_vm_write(pos, *s, color);
		pos += INST_BPC;
		s++;
		col++;
	}
	while (col < INST_SCR_W) {
		inst_vm_write(pos, ' ', color);
		pos += INST_BPC;
		col++;
	}
}


/*======================================================================
  填充整行背景色 (标题栏等)
======================================================================*/
static void inst_fill_row_bg(int row, unsigned char color)
{
	int pos = row * INST_SCR_W * INST_BPC;
	int col;
	for (col = 0; col < INST_SCR_W; col++) {
		inst_vm_write(pos, ' ', color);
		pos += INST_BPC;
	}
}


/*======================================================================
  在指定行写居中字符串
======================================================================*/
static void inst_put_centered(int row, const char *s, unsigned char color)
{
	int len = 0;
	const char *p = s;
	while (*p) { len++; p++; }
	if (len > INST_SCR_W) len = INST_SCR_W;
	int col = (INST_SCR_W - len) / 2;
	inst_put_str_at(row, col, s, color);
}


/*======================================================================
  在指定位置写整数 (左对齐)
======================================================================*/
static void inst_put_int_at(int row, int col, int value, unsigned char color)
{
	char buf[12];
	int i = 0, neg = 0;
	int pos;

	if (value < 0) { neg = 1; value = -value; }
	if (value == 0) {
		buf[i++] = '0';
	} else {
		while (value > 0 && i < 11) {
			buf[i++] = (char)('0' + value % 10);
			value /= 10;
		}
	}

	pos = (row * INST_SCR_W + col) * INST_BPC;
	if (neg) {
		inst_vm_write(pos, '-', color);
		pos += INST_BPC;
	}
	while (i > 0) {
		inst_vm_write(pos, buf[--i], color);
		pos += INST_BPC;
	}
}


/*======================================================================
  绘制分隔线行 (80 个 '-')
======================================================================*/
static void inst_draw_sep(int row, unsigned char color)
{
	int pos = row * INST_SCR_W * INST_BPC;
	int col;
	for (col = 0; col < INST_SCR_W; col++) {
		inst_vm_write(pos, '-', color);
		pos += INST_BPC;
	}
}


/*======================================================================
  绘制标题栏 (蓝底白字, 居中标题)
======================================================================*/
static void inst_draw_title(const char *subtitle)
{
	/* 标题栏: 蓝底白字, 全宽 */
	inst_fill_row_bg(ROW_TITLE, COL_TITLE);
	inst_put_centered(ROW_TITLE, S_TITLE, COL_TITLE);

	/* 副标题 */
	inst_put_full_row(ROW_SUBTITLE, subtitle, COL_CYAN);

	/* 分隔线 */
	inst_draw_sep(g_row_sep, COL_GRAY);

	/* 清空内容区 (行 2~21) */
	{
		int r;
		for (r = 2; r < g_row_sep; r++) {
			inst_put_full_row(r, "", COL_TEXT);
		}
	}
}


/*======================================================================
  绘制帮助行和状态行
======================================================================*/
static void inst_draw_help(const char *help)
{
	inst_put_full_row(g_row_help, help, COL_CYAN);
}

static void inst_draw_status(const char *status, unsigned char color)
{
	inst_put_full_row(g_row_status, status, color);
}


/*======================================================================
  绘制进度条
  progress: 0~100
======================================================================*/
static void inst_draw_progress_bar(int progress)
{
	int filled;
	int pos;
	int col;

	if (progress < 0) progress = 0;
	if (progress > 100) progress = 100;

	filled = (progress * BAR_WIDTH) / 100;
	pos = (g_bar_row * INST_SCR_W + BAR_COL) * INST_BPC;

	/* 左边界 '[' */
	inst_vm_write(pos, '[', COL_BRIGHT);
	pos += INST_BPC;

	/* 填充部分 */
	for (col = 0; col < BAR_WIDTH; col++) {
		if (col < filled) {
			inst_vm_write(pos, 0xDB, COL_BAR_FILL);
		} else {
			inst_vm_write(pos, 0xB0, COL_BAR_EMPTY);
		}
		pos += INST_BPC;
	}

	/* 右边界 ']' */
	inst_vm_write(pos, ']', COL_BRIGHT);
	pos += INST_BPC;

	/* 百分比 */
	{
		int pct_col = BAR_COL + BAR_WIDTH + 3;
		inst_put_int_at(g_bar_row, pct_col, progress, COL_BRIGHT);
		inst_put_str_at(g_bar_row, pct_col + 4, "%", COL_BRIGHT);
	}
}


/*======================================================================
  绘制步骤行
  row:     行号
  label:   步骤描述
  state:   0=pending, 1=active, 2=done, 3=failed
======================================================================*/
static void inst_draw_step(int row, const char *label, int state)
{
	const char *mark;
	const char *stxt;
	unsigned char mcolor;
	unsigned char lcolor;
	unsigned char scolor;

	switch (state) {
	case 1:	/* active */
		mark = S_MARK_ACTIVE;  mcolor = COL_YELLOW;
		lcolor = COL_BRIGHT;
		stxt = S_STATUS_WORK;  scolor = COL_YELLOW;
		break;
	case 2:	/* done */
		mark = S_MARK_DONE;    mcolor = COL_GREEN;
		lcolor = COL_GREEN;
		stxt = S_STATUS_DONE;  scolor = COL_GREEN;
		break;
	case 3:	/* failed */
		mark = S_MARK_FAILED;  mcolor = COL_RED;
		lcolor = COL_RED;
		stxt = S_STATUS_ERR;   scolor = COL_RED;
		break;
	default: /* pending */
		mark = S_MARK_PENDING; mcolor = COL_GRAY;
		lcolor = COL_GRAY;
		stxt = S_STATUS_WAIT;  scolor = COL_GRAY;
		break;
	}

	/* 整行: mark + label + 空格填充 + status text */
	{
		int pos = row * INST_SCR_W * INST_BPC;
		int col = 0;
		const char *s;

		/* 前导空格 */
		inst_vm_write(pos, ' ', COL_TEXT); pos += INST_BPC; col++;

		/* 标记 [x] */
		s = mark;
		while (*s && col < INST_SCR_W) {
			inst_vm_write(pos, *s, mcolor); pos += INST_BPC; s++; col++;
		}

		/* 空格 */
		inst_vm_write(pos, ' ', COL_TEXT); pos += INST_BPC; col++;

		/* 步骤描述 */
		s = label;
		while (*s && col < INST_SCR_W) {
			inst_vm_write(pos, *s, lcolor); pos += INST_BPC; s++; col++;
		}

		/* 填充空格到 status 位置 (col 60) */
		while (col < 60) {
			inst_vm_write(pos, ' ', COL_TEXT); pos += INST_BPC; col++;
		}

		/* 状态文字 */
		s = stxt;
		while (*s && col < INST_SCR_W) {
			inst_vm_write(pos, *s, scolor); pos += INST_BPC; s++; col++;
		}

		/* 填充到行尾 */
		while (col < INST_SCR_W) {
			inst_vm_write(pos, ' ', COL_TEXT); pos += INST_BPC; col++;
		}
	}
}


/*======================================================================
  页面绘制: 欢迎
======================================================================*/
static void draw_welcome(void)
{
	inst_draw_title(S_SUB_WELCOME);

	inst_put_str_at(ROW_CONTENT0,     2, S_WELCOME_1,    COL_BRIGHT);
	inst_put_str_at(ROW_CONTENT0 + 2, 2, S_WELCOME_2,    COL_TEXT);
	inst_put_str_at(ROW_CONTENT0 + 3, 2, S_WELCOME_3,    COL_TEXT);
	inst_put_str_at(ROW_CONTENT0 + 4, 4, S_WELCOME_4,    COL_CYAN);
	inst_put_str_at(ROW_CONTENT0 + 5, 4, S_WELCOME_5,    COL_CYAN);
	inst_put_str_at(ROW_CONTENT0 + 6, 4, S_WELCOME_6,    COL_CYAN);
	inst_put_str_at(ROW_CONTENT0 + 7, 4, S_WELCOME_7,    COL_CYAN);
	inst_put_str_at(ROW_CONTENT0 + 9, 2, S_WELCOME_WARN, COL_YELLOW);

	inst_draw_help(S_HELP_CONTINUE);
	inst_draw_status(S_STATUS_READY, COL_TEXT);
}


/*======================================================================
  页面绘制: 硬盘信息
======================================================================*/
static void draw_diskinfo(HD_INFO *hdi)
{
	inst_draw_title(S_SUB_DISK);

	inst_put_str_at(ROW_CONTENT0, 2, S_DISK_HDR, COL_BRIGHT);

	inst_put_str_at(ROW_CONTENT0 + 2, 2, S_DISK_MODEL, COL_TEXT);
	inst_put_str_at(ROW_CONTENT0 + 2, 13, hdi->model, COL_BRIGHT);

	inst_put_str_at(ROW_CONTENT0 + 3, 2, S_DISK_SIZE, COL_TEXT);
	inst_put_int_at(ROW_CONTENT0 + 3, 13, hdi->capacity_kb / 1024, COL_BRIGHT);
	inst_put_str_at(ROW_CONTENT0 + 3, 17, S_DISK_MB, COL_TEXT);

	inst_put_str_at(ROW_CONTENT0 + 4, 2, S_DISK_SECTORS, COL_TEXT);
	inst_put_int_at(ROW_CONTENT0 + 4, 13, (int)hdi->lba_sectors, COL_BRIGHT);

	inst_put_str_at(ROW_CONTENT0 + 6, 2, S_DISK_NOTE, COL_TEXT);
	inst_put_str_at(ROW_CONTENT0 + 8, 2, S_DISK_WARN, COL_YELLOW);

	inst_draw_help(S_HELP_BACK);
	inst_draw_status(S_STATUS_READY, COL_TEXT);
}


/*======================================================================
  页面绘制: 确认
======================================================================*/
static void draw_confirm(HD_INFO *hdi)
{
	inst_draw_title(S_SUB_CONFIRM);

	inst_put_str_at(ROW_CONTENT0, 2, S_CONFIRM_1, COL_TEXT);

	inst_put_str_at(ROW_CONTENT0 + 2, 4, S_DISK_MODEL, COL_TEXT);
	inst_put_str_at(ROW_CONTENT0 + 2, 15, hdi->model, COL_BRIGHT);

	inst_put_str_at(ROW_CONTENT0 + 3, 4, S_DISK_SIZE, COL_TEXT);
	inst_put_int_at(ROW_CONTENT0 + 3, 15, hdi->capacity_kb / 1024, COL_BRIGHT);
	inst_put_str_at(ROW_CONTENT0 + 3, 19, S_DISK_MB, COL_TEXT);

	inst_put_str_at(ROW_CONTENT0 + 5, 2, S_CONFIRM_2, COL_RED);
	inst_put_str_at(ROW_CONTENT0 + 7, 2, S_CONFIRM_3, COL_YELLOW);

	inst_draw_help(S_HELP_CONFIRM);
	inst_draw_status(S_STATUS_READY, COL_TEXT);
}


/*======================================================================
  页面绘制: 进度 (初始状态)
======================================================================*/
static void draw_progress_init(void)
{
	inst_draw_title(S_SUB_PROGRESS);

	inst_put_str_at(ROW_CONTENT0, 2, S_PROG_HDR, COL_BRIGHT);

	/* 三个步骤, 全部 pending */
	inst_draw_step(ROW_CONTENT0 + 2, S_PROG_STEP1, 0);
	inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 0);
	inst_draw_step(ROW_CONTENT0 + 4, S_PROG_STEP3, 0);

	/* 进度标签 */
	inst_put_str_at(g_bar_row, BAR_COL - 10, S_PROG_LABEL, COL_TEXT);
	inst_draw_progress_bar(0);

	inst_draw_help(S_HELP_WAIT);
	inst_draw_status(S_STATUS_WORKING, COL_YELLOW);
}


/*======================================================================
  页面绘制: 完成
======================================================================*/
static void draw_complete(int total_sectors)
{
	inst_draw_title(S_SUB_COMPLETE);

	inst_put_str_at(ROW_CONTENT0, 2, S_COMPLETE_1, COL_GREEN);

	inst_put_str_at(ROW_CONTENT0 + 2, 2, S_COMPLETE_2, COL_TEXT);
	inst_put_int_at(ROW_CONTENT0 + 2, 19, total_sectors, COL_BRIGHT);

	inst_put_str_at(ROW_CONTENT0 + 4, 2, S_COMPLETE_OK, COL_GREEN);

	inst_put_str_at(ROW_CONTENT0 + 6, 2, S_COMPLETE_3, COL_TEXT);
	inst_put_str_at(ROW_CONTENT0 + 7, 4, S_COMPLETE_4, COL_CYAN);
	inst_put_str_at(ROW_CONTENT0 + 8, 4, S_COMPLETE_5, COL_CYAN);

	inst_draw_help(S_HELP_RETURN);
	inst_draw_status(S_STATUS_READY, COL_TEXT);
}


/*======================================================================
  页面绘制: 失败
======================================================================*/
static void draw_failed(int step)
{
	/* S_FAILED_1 = "Installation failed at step " 长度 28 */
	inst_draw_title(S_SUB_FAILED);

	inst_put_str_at(ROW_CONTENT0, 2, S_FAILED_1, COL_RED);
	inst_put_int_at(ROW_CONTENT0, 2 + 28, step, COL_RED);
	inst_put_str_at(ROW_CONTENT0, 2 + 28 + 1, ".", COL_RED);

	/* 诊断信息 */
	inst_put_str_at(ROW_CONTENT0 + 2, 2, S_FAIL_ERR, COL_YELLOW);
	inst_put_int_at(ROW_CONTENT0 + 2, 16, g_err_code, COL_YELLOW);
	/* 错误描述 (避免使用字符串字面量, 直接在各 case 中输出) */
	switch (g_err_code) {
	case -1: inst_put_str_at(ROW_CONTENT0 + 2, 20, S_ERR_DESC_R, COL_YELLOW); break;
	case -2: inst_put_str_at(ROW_CONTENT0 + 2, 20, S_ERR_DESC_D, COL_YELLOW); break;
	case -3: inst_put_str_at(ROW_CONTENT0 + 2, 20, S_ERR_DESC_W, COL_YELLOW); break;
	case -4: inst_put_str_at(ROW_CONTENT0 + 2, 20, S_ERR_DESC_E, COL_YELLOW); break;
	default: break;
	}

	inst_put_str_at(ROW_CONTENT0 + 3, 2, S_FAIL_LBA, COL_YELLOW);
	inst_put_int_at(ROW_CONTENT0 + 3, 16, g_err_kern_lba, COL_YELLOW);

	inst_put_str_at(ROW_CONTENT0 + 4, 2, S_FAIL_TOTSEC, COL_YELLOW);
	inst_put_int_at(ROW_CONTENT0 + 4, 16, g_err_disk_sec, COL_YELLOW);

	inst_put_str_at(ROW_CONTENT0 + 5, 2, S_FAIL_KSECS, COL_YELLOW);
	inst_put_int_at(ROW_CONTENT0 + 5, 16, g_err_kern_sec, COL_YELLOW);

	inst_put_str_at(ROW_CONTENT0 + 6, 2, S_FAIL_KSIZE, COL_YELLOW);
	inst_put_int_at(ROW_CONTENT0 + 6, 20, g_err_kern_sz, COL_YELLOW);

	inst_put_str_at(ROW_CONTENT0 + 8, 2, S_FAILED_2, COL_TEXT);

	inst_draw_help(S_HELP_RETURN);
	inst_draw_status(S_STATUS_READY, COL_TEXT);
}


/*======================================================================
  页面绘制: 无硬盘
======================================================================*/
static void draw_nohd(void)
{
	inst_draw_title(S_SUB_NOHD);

	inst_put_str_at(ROW_CONTENT0, 2, S_NOHD_1, COL_RED);
	inst_put_str_at(ROW_CONTENT0 + 2, 2, S_NOHD_2, COL_TEXT);

	inst_draw_help(S_HELP_RETURN);
	inst_draw_status(S_STATUS_READY, COL_TEXT);
}


/*======================================================================
  执行安装 (三步)
  返回: 0=成功, 正数=失败步骤号
  成功时 *total_sectors_out 写入已写扇区数
======================================================================*/
static int inst_do_install(HD_INFO *hdi, int *total_sectors_out)
{
	int kernel_sectors;
	int kernel_lba;
	int total = 0;
	int ret;
	int i;
	static t_8 vbr_buf[512];
	static t_8 mbr_buf[512];

	kernel_sectors = (KERNEL_FILE_SIZE + 511) / 512;
	g_err_kern_sz  = KERNEL_FILE_SIZE;
	g_err_disk_sec = (int)hdi->lba_sectors;
	g_err_kern_sec = kernel_sectors;

	/* ==== 步骤 1: 格式化 FAT32 ==== */
	inst_draw_step(ROW_CONTENT0 + 2, S_PROG_STEP1, 1);	/* active */
	inst_draw_progress_bar(10);

	ret = fat_format(hdi->lba_sectors, 32);
	if (ret != 0) {
		g_err_code = ret;
		inst_draw_step(ROW_CONTENT0 + 2, S_PROG_STEP1, 3);	/* failed */
		return 1;
	}

	inst_draw_step(ROW_CONTENT0 + 2, S_PROG_STEP1, 2);	/* done */
	inst_draw_progress_bar(33);

	/* ==== 步骤 2: 写引导代码 + loader ==== */
	inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 1);	/* active */
	inst_draw_progress_bar(43);

	/* 2a: 写 MBR 引导代码到扇区 0 (保留 fat_format 写入的分区表) */
	ret = hd_read_sector(0, mbr_buf);
	if (ret != 0) {
		g_err_code = ret;
		inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 3);
		return 2;
	}
	/* 仅覆盖引导代码区 [0..0x1BD], 保留分区表 [0x1BE..0x1FD] 和 0xAA55 */
	for (i = 0; i < 0x1BE; i++) mbr_buf[i] = _mbr_bin[i];
	mbr_buf[510] = 0x55; mbr_buf[511] = 0xAA;
	ret = hd_write_sector(0, mbr_buf);
	if (ret != 0) {
		g_err_code = ret;
		inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 3);
		return 2;
	}

	/* 2b: 读取 VBR (扇区 PART_START, 由 fat_format 写入的 BPB) */
	ret = hd_read_sector(PART_START, vbr_buf);
	if (ret != 0) {
		g_err_code = ret;
		inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 3);
		return 2;
	}

	/* 用 hdboot 引导代码覆盖前 3 字节 (JMP+NOP) 和偏移 90~509 */
	/* 偏移 3~10: OEM name */
	vbr_buf[0] = _hdboot_bin[0];
	vbr_buf[1] = _hdboot_bin[1];
	vbr_buf[2] = _hdboot_bin[2];
	for (i = 3; i <= 10; i++) vbr_buf[i] = _hdboot_bin[i];
	/* 偏移 11~89: BPB 不动 */
	for (i = 90; i <= 509; i++) vbr_buf[i] = _hdboot_bin[i];

	ret = hd_write_sector(PART_START, vbr_buf);
	if (ret != 0) {
		g_err_code = ret;
		inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 3);
		return 2;
	}

	/* 2c: 写 loader 到扇区 2~33 (位于 MBR 间隙, 不影响分区) */
	ret = hd_write_sectors(2, 32, _loader_bin);
	if (ret != 0) {
		g_err_code = ret;
		inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 3);
		return 2;
	}

	inst_draw_step(ROW_CONTENT0 + 3, S_PROG_STEP2, 2);	/* done */
	inst_draw_progress_bar(66);

	/* ==== 步骤 3: 写 kernel.bin 到硬盘末尾 ==== */
	inst_draw_step(ROW_CONTENT0 + 4, S_PROG_STEP3, 1);	/* active */
	inst_draw_progress_bar(76);

	kernel_lba = (int)hdi->lba_sectors - kernel_sectors - 1;
	g_err_kern_lba = kernel_lba;

	ret = hd_write_sectors(kernel_lba, kernel_sectors, (void*)KERNEL_FILE_ADDR);
	if (ret != 0) {
		g_err_code = ret;
		inst_draw_step(ROW_CONTENT0 + 4, S_PROG_STEP3, 3);
		return 3;
	}

	/* 把 kernel_lba (4字节@440) 和 kernel_sectors (2字节@444) 写入 VBR (扇区 PART_START) */
	{
		static t_8 vbr2[512];
		hd_read_sector(PART_START, vbr2);
		vbr2[440] = (t_8)(kernel_lba & 0xFF);
		vbr2[441] = (t_8)((kernel_lba >> 8) & 0xFF);
		vbr2[442] = (t_8)((kernel_lba >> 16) & 0xFF);
		vbr2[443] = (t_8)((kernel_lba >> 24) & 0xFF);
		vbr2[444] = (t_8)(kernel_sectors & 0xFF);
		vbr2[445] = (t_8)((kernel_sectors >> 8) & 0xFF);
		hd_write_sector(PART_START, vbr2);
	}

	inst_draw_step(ROW_CONTENT0 + 4, S_PROG_STEP3, 2);	/* done */
	inst_draw_progress_bar(100);

	/* ==== 步骤 4: 写入示例文件到 FAT32 (让文件管理器有内容) ==== */
	/* fat_format 已重置 fat_initialized 并重新 init, 现在可以写文件 */
	fat_write_file("README  TXT", S_SAMPLE_README,
		sizeof(S_SAMPLE_README) - 1);
	fat_write_file("HELLO   C  ", S_SAMPLE_HELLO,
		sizeof(S_SAMPLE_HELLO) - 1);
	fat_write_file("NOTES   TXT", S_SAMPLE_NOTES,
		sizeof(S_SAMPLE_NOTES) - 1);

	total = 1 + 32 + kernel_sectors;
	*total_sectors_out = total;

	return 0;
}


/*======================================================================
  installer_run — 安装向导主入口

  由 shell 命令 "setup" 调用, 接管键盘和显示。
  ESC 可随时退出 (进度页除外), 退出时清屏, 光标归零。
======================================================================*/
PUBLIC void installer_run(int *p_cursor)
{
	t_32 key;
	int current_page = PAGE_WELCOME;
	HD_INFO hdi;
	int total_sectors = 0;
	int fail_step = 0;
	int has_disk = 0;

	/* 清屏, 进入全屏界面 */
	inst_clear_screen();
	draw_welcome();

	while (1) {
		key = 0;
		keyboard_read_simple(&key);

		if (key == 0) {
			/* 无按键, 短暂延迟 (counter-based, 非 hlt) */
			volatile int d;
			for (d = 0; d < 5000; d++) {}
			continue;
		}

		/* ==== ESC: 退出 (进度页除外) ==== */
		if (key == ESC) {
			if (current_page == PAGE_PROGRESS) continue;
			break;
		}

		/* ==== ENTER: 页面推进 ==== */
		if (key == ENTER) {
			switch (current_page) {
			case PAGE_WELCOME:
				/* 识别硬盘 */
				hd_init();
				if (hd_identify(&hdi) != 0) {
					current_page = PAGE_NOHD;
					inst_clear_screen();
					draw_nohd();
				} else {
					has_disk = 1;
					current_page = PAGE_DISKINFO;
					inst_clear_screen();
					draw_diskinfo(&hdi);
				}
				break;

			case PAGE_DISKINFO:
				current_page = PAGE_CONFIRM;
				inst_clear_screen();
				draw_confirm(&hdi);
				break;

			case PAGE_COMPLETE:
			case PAGE_FAILED:
			case PAGE_NOHD:
				goto exit_installer;

			default:
				break;
			}
			continue;
		}

		/* ==== Y 键: 确认安装 ==== */
		if (current_page == PAGE_CONFIRM) {
			char ch = (char)(key & 0xFF);
			if (ch == 'y' || ch == 'Y') {
				current_page = PAGE_PROGRESS;
				inst_clear_screen();
				draw_progress_init();

				/* 执行安装 */
				{
					int ret = inst_do_install(&hdi, &total_sectors);
					if (ret == 0) {
						current_page = PAGE_COMPLETE;
						inst_clear_screen();
						draw_complete(total_sectors);
					} else {
						fail_step = ret;
						current_page = PAGE_FAILED;
						inst_clear_screen();
						draw_failed(fail_step);
					}
				}
				continue;
			}
		}
	}

exit_installer:
	/* 清屏, 光标归零, 返回 shell */
	inst_clear_screen();
	*p_cursor = 0;
}
