/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               shell.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS Shell
  ★ 所有固定字符串定义为全局命名数组(非字符串字面量)
  ★ 避免 MinGW GCC 对 .rodata 匿名字符串池的寻址问题
  ★ 硬件光标同步更新
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include <stddef.h>
#include "string.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "keyboard.h"
#include "shell.h"
#include "taskmgr.h"
#include "hd.h"
#include "fat.h"
#include "editor.h"
#include "proto.h"
#include "gfx.h"
#include "wm.h"

/* kernel.bin 文件大小 (build.sh 通过 -DKERNEL_FILE_SIZE 传入实际值) */
#ifndef KERNEL_FILE_SIZE
#define KERNEL_FILE_SIZE 40960
#endif

/* 内核在内存中的加载地址 (loader.asm 中 KernelEntryPointPhyAddr) */
#define KERNEL_LOAD_ADDR 0x30400

/* kernel.bin 在内存中的位置 (执行地址, InitKernel 复制后数据完整).
 * 注: 0x10000 处的原始副本在 kernel 较大时会被 InitKernel 覆盖, 不可用. */
#define KERNEL_FILE_ADDR  0x30400

/* 嵌入的引导二进制 (kernel.asm 中 incbin, 供 setup 命令使用) */
extern char _hdboot_bin[];
extern char _hdboot_bin_end[];
extern char _loader_bin[];
extern char _loader_bin_end[];


/* =====================================================================
   全局命名字符串变量 (非const, 放入.data段而非.rodata)
   裸机环境下 .data 段的寻址比 .rodata 更可靠
===================================================================== */
static char S_HELP_HDR[]   = "=== CatOS Commands ===";
static char S_HELP_1[]     = "  help    - Show this help";
static char S_HELP_2[]     = "  ver     - Version info";
static char S_HELP_3[]     = "  about   - About CatOS";
static char S_HELP_4[]     = "  clear   - Clear screen";
static char S_HELP_5[]     = "  echo    - Print text";
static char S_HELP_6[]     = "  reboot   - Reboot system";
static char S_HELP_7[]     = "  sysinfo  - System info";
static char S_HELP_8[]     = "  shutdown - Power off";
static char S_HELP_9[]     = "  taskmgr  - Task manager";
static char S_HELP_10[]    = "  diskinfo - Disk & FAT12/16/32 info";
static char S_HELP_11[]    = "  dir      - List files";
static char S_HELP_12[]    = "  type     - Show file content";
static char S_HELP_13[]    = "  format   - Format disk (format 1 32)";
static char S_HELP_14[]    = "  touch    - Create empty file";
static char S_HELP_15[]    = "  edit     - Edit file";
static char S_HELP_16[]    = "  setup    - Interactive installer";
static char S_HELP_17[]    = "  rd       - Delete file";
static char S_HELP_CD[]    = "  cd       - Change directory";
static char S_HELP_MKDIR[] = "  mkdir    - Make directory";
static char S_VER[]        = "CatOS v0.14 Release 3";
static char S_EDITION_FP[] = " [Floppy Edition]";
static char S_EDITION_HD[] = " [Disk Edition]";
static char S_ABOUT_1[]    = "-- CatOS v0.14 Release 3 --";
static char S_ABOUT_2[]    = "Protected Mode OS Kernel";
static char S_TITLE[]      = "CatOS v0.14 Release 3";
static char S_PROMPT[]     = "CatOS> ";
static char S_UNKNOWN[]    = "Unknown command: ";

/* 命令名比较字符串 (必须用全局数组放入.data段, 不能用字符串字面量)
 * 字符串字面量会被 GCC 放入 .rodata 段, 该裸机环境下 .rodata 寻址不可靠,
 * 导致 sh_streq() 从 .rodata 读到错误数据而始终返回 0 */
static char S_CMD_HELP[]    = "help";
static char S_CMD_VER[]     = "ver";
static char S_CMD_VERSION[] = "version";
static char S_CMD_ABOUT[]   = "about";
static char S_CMD_CLEAR[]   = "clear";
static char S_CMD_CLS[]     = "cls";
static char S_CMD_REBOOT[]  = "reboot";
static char S_CMD_SHUTDOWN[]= "shutdown";
static char S_CMD_SYSINFO[] = "sysinfo";
static char S_CMD_TASKMGR[] = "taskmgr";
static char S_CMD_DISKINFO[] = "diskinfo";
static char S_CMD_DIR[]     = "dir";
static char S_CMD_LS[]      = "ls";
static char S_CMD_TYPE[]    = "type";
static char S_CMD_FORMAT[] = "format";
static char S_CMD_TOUCH[]  = "touch";
static char S_CMD_EDIT[]   = "edit";
static char S_CMD_SETUP[]  = "setup";
static char S_CMD_RD[]     = "rd";
static char S_CMD_CAT[]    = "cat";
static char S_CMD_CD[]     = "cd";
static char S_CMD_MKDIR[]  = "mkdir";

/* cd/mkdir 显示字符串 */
static char S_CD_NF[]      = "Directory not found: ";
static char S_CD_ROOT[]    = "/";
static char S_MKDIR_USE[]  = "Usage: mkdir <name>";
static char S_MKDIR_OK[]   = "Directory created: ";
static char S_MKDIR_EX[]   = "Already exists: ";
static char S_MKDIR_FAIL[] = "mkdir failed";
static char S_MKDIR_FULL[] = "Disk full";

/* 当前工作目录簇号 (0=根目录) */
PRIVATE t_32 sh_cwd = 0;

/* 当前工作目录路径 (如 "/" 或 "/test/"), 始终以 / 开头, 以 / 结尾 */
PRIVATE char sh_cwd_path[64] = "/";

/* 获取当前工作目录簇号 (供 editor.c 等模块调用) */
PUBLIC t_32 shell_get_cwd(void)
{
	return sh_cwd;
}

/* 获取当前工作目录路径字符串 (供 tty.c 提示符使用) */
PUBLIC const char *shell_get_cwd_path(void)
{
	return sh_cwd_path;
}

/* sysinfo 显示字符串 */
static char SI_HDR[]        = "--- System Information ---";
static char SI_CPU[]        = "CPU: ";
static char SI_MEM[]        = "Memory: ";
static char SI_MEM_KB[]     = " KB";
static char SI_TIME[]       = "Time: ";
static char SI_SEP[]        = ":";
static char SI_UNKNOWN[]    = "(unknown)";

/* diskinfo/dir/type 显示字符串 */
static char DI_HDR[]        = "--- Hard Disk Information ---";
static char DI_MODEL[]      = "Model: ";
static char DI_SECTORS[]    = "Sectors: ";
static char DI_CAPACITY[]   = "Capacity: ";
static char DI_MB[]         = " MB";
static char DI_CHS[]        = "CHS: ";
static char DI_SLASH[]      = "/";
static char DI_NOHD[]       = "(no hard disk found)";
static char DI_FS_HDR[]     = "--- File System ---";
static char DI_FS_TYPE[]    = "Type: FAT";
static char DI_FS_TOTAL[]   = "Total clusters: ";
static char DI_FS_SEC[]     = "Sectors: ";
static char DI_FS_VOL[]     = "Volume: ";
static char DI_FS_NONE[]    = "(no FAT partition found)";
static char DI_FS_SPC[]     = "Sectors/Cluster: ";
static char DI_FS_CLUSSZ[]  = "Cluster Size: ";
static char DI_FS_FATSZ[]   = "FAT Size: ";
static char DI_FS_NFATS[]   = "FATs: ";
static char DI_FS_RSVD[]    = "Reserved: ";
static char DI_FS_ROOT[]    = "Root: ";
static char DI_FS_FSI[]     = "FSInfo: sector ";
static char DI_FS_FREE[]    = "Free Hint: ";
static char DI_FS_PART[]    = "Partition: LBA ";
static char DI_SECS_UNIT[]  = " sectors";
static char DI_BYTES_UNIT[] = " bytes";
static char DI_ENTRIES[]    = " entries";
static char DI_CLUSTER[]    = "cluster ";
static char DI_ROOT_SECS[]  = " (";
static char DI_ROOT_SECS2[] = " secs)";
static char DIR_HDR[]       = "--- Root Directory ---";
static char DIR_DIR[]       = "<DIR>";
static char DIR_BYTES[]     = " bytes";
static char DIR_EMPTY[]     = "(empty)";
static char TYPE_NOTFOUND[] = "File not found: ";
static char TYPE_ERROR[]    = "Read error";

/* format/touch 显示字符串 */
static char FMT_HDR[]       = "--- Format Hard Disk ---";
static char FMT_MODEL[]     = "Disk: ";
static char FMT_CAPACITY[]  = "Size: ";
static char FMT_MB[]        = " MB (";
static char FMT_SECTORS[]   = " sectors)";
static char FMT_CONFIRM[]   = "Type 'format y' to confirm: ALL DATA WILL BE LOST!";
static char FMT_OK[]        = "Format complete. FAT";
static char FMT_OK2[]       = " filesystem created.";
static char FMT_RUNSETUP[]  = "Run 'setup 1' to install CatOS.";
static char FMT_FAIL[]      = "Format failed!";
static char FMT_NOHD[]      = "No hard disk found!";
static char FMT_ABORT[]     = "Aborted.";
static char FMT_DISK_HDR[]  = "Disk #  Model / Size";
static char FMT_DISK_NUM[]  = "  [1]   ";
static char FMT_TYPE_HDR[]  = "Select FAT type:";
static char FMT_TYPE_12[]   = "  12 - FAT12 (floppy, <16MB)";
static char FMT_TYPE_16[]   = "  16 - FAT16 (16MB~2GB)";
static char FMT_TYPE_32[]   = "  32 - FAT32 (>=2GB, recommended)";
static char FMT_TYPE_AUTO[] = "   0 - Auto (recommended)";
static char FMT_USAGE[]     = "Usage: format <disk#> <type>";
static char FMT_USAGE2[]    = "  e.g. 'format 1 32' = disk 1, FAT32";
static char FMT_BADTYPE[]   = "Bad FAT type. Use 0/12/16/32";
static char FMT_BADDISK[]   = "Bad disk number. Use 1";
static char FMT_CONFIRM2[]  = "Type 'format 1 32 y' to confirm.";

/* setup 命令字符串 */
static char SETUP_HDR[]     = "--- CatOS Setup ---";
static char SETUP_DISK_HDR[]= "Disk #  Model / Size";
static char SETUP_DISK_NUM[]= "  [1]   ";
static char SETUP_USAGE[]   = "Usage: setup <disk#>";
static char SETUP_USAGE2[]  = "  e.g. 'setup 1' = install to disk 1";
static char SETUP_BADDISK[] = "Bad disk number. Use 1";
static char SETUP_NOHD[]    = "No hard disk found!";
static char SETUP_STEP1[]   = "[1/3] Writing MBR + loader...";
static char SETUP_STEP2[]   = "[2/3] Writing kernel.bin...";
static char SETUP_STEP3[]   = "[3/3] Formatting FAT32...";
static char SETUP_OK[]      = "Installation complete!";
static char SETUP_OK2[]     = "Sectors written: ";
static char SETUP_BYTES[]   = "";
static char SETUP_FAIL[]    = "Setup failed at step ";
static char SETUP_VERIFY[]  = "MBR + loader + kernel installed.";
static char SETUP_NOVERIFY[]= "ERROR: Write failed!";
static char SETUP_REBOOT[]  = "Reboot from hard disk to start CatOS.";
static char SETUP_NODISK[]  = "setup: only available from floppy boot.";
static char TOUCH_OK[]      = "File created: ";
static char TOUCH_EXIST[]   = "File already exists: ";
static char TOUCH_FAIL[]    = "Failed to create file.";
static char TOUCH_NONAME[]  = "Usage: touch <filename>";
static char TOUCH_ERR1[]    = "ERR: FAT not initialized.";
static char TOUCH_ERR2[]    = "ERR: read root dir failed.";
static char TOUCH_ERR3[]    = "ERR: no free dir entry.";
static char TOUCH_ERR4[]    = "ERR: re-read sector failed.";
static char TOUCH_ERR_W1[]  = "ERR: hd not ready (BSY/DRDY).";
static char TOUCH_ERR_W2[]  = "ERR: DRQ timeout (no data req).";
static char TOUCH_ERR_W3[]  = "ERR: write not complete (BSY).";
static char TOUCH_ERR_W4[]  = "ERR: disk error flag set.";
static char TOUCH_ERR_SEC[] = "  Sector LBA: ";
static char TOUCH_ERR_CODE[]= "  HD err code: ";
static char TOUCH_ERR_R1[]  = " (hd not ready)";
static char TOUCH_ERR_R2[]  = " (DRQ timeout)";
static char TOUCH_ERR_R3[]  = " (disk ERR flag)";
static char TOUCH_ERR_FAT[] = "  FAT type/data/clus: ";
static char TYPE_USAGE[]    = "Usage: type <filename>";

/* rd 命令字符串 */
static char RD_OK[]         = "File deleted: ";
static char RD_NONAME[]     = "Usage: rd <filename>";
static char RD_NOTFOUND[]   = "File not found: ";
static char RD_ERR1[]       = "ERR: FAT not initialized.";
static char RD_ERR2[]       = "ERR: read root dir failed.";
static char RD_ERR3[]       = "ERR: re-read sector failed.";
static char RD_ERR4[]       = "ERR: write sector failed.";

/* run 命令字符串 */
static char S_CMD_RUN[]     = "run";
static char S_RUN_NONAME[]  = "Usage: run <filename>";
static char S_RUN_NOTFOUND[]= "File not found: ";
static char S_RUN_BADFMT[]  = "Bad executable format (not CE)";
static char S_RUN_TOOLARGE[]= "Program too large (max 124KB)";
static char S_RUN_BUSY[]    = "A program is already running";
static char S_RUN_CORRUPT[] = "Corrupted executable";
static char S_RUN_START[]   = "Running: ";
static char S_RUN_EXITED[]  = "Program exited (code ";
static char S_RUN_CLOSE[]   = ")";
static char S_RUN_SLOT[]    = " (slot ";
static char S_HELP_18[]     = "  run     - Run executable (run f & = background)";
static char S_CMD_PS[]      = "ps";
static char S_HELP_19[]     = "  ps      - List running processes";
static char S_HELP_20[]     = "  cat     - Start graphical desktop";

PUBLIC void shell_parse_and_execute(char* cmdline, int *p_cursor);
PRIVATE char cmdline_buf[SHELL_CMD_MAX_LEN];
PRIVATE int  cmdline_pos = 0;

/* 屏幕常量 */
#define SH_BPC			2
#define SH_ROW			(g_text_cols * 2)
#define SH_MAX			(g_text_cols * g_text_rows * 2)
/* 注意: SH_ROW 和 SH_MAX 基于 g_text_cols/g_text_rows 动态计算
   文本模式: g_text_cols=80, g_text_rows=25 → SH_ROW=160, SH_MAX=4000
   图形模式: g_text_cols=40, g_text_rows=25 → SH_ROW=80, SH_MAX=2000 */

/* =====================================================================
   写单个字符到显存 (统一封装, 便于后续图形化适配)
===================================================================== */
static inline void sh_vm_write(int pos, char ch, unsigned char color)
{
	vm_putc(pos, ch, color);
}

/* =====================================================================
   更新 VGA 硬件光标位置
===================================================================== */
static inline void sh_update_hw_cursor(int byte_pos)
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

/* =====================================================================
   软件滚屏: 将显存内容上移一行, 底行清空
   使用 rep movsw 进行快速块拷贝 (GS段内操作)
===================================================================== */
static void sh_do_scroll_up(void)
{
	if (g_video_mode != 0) {
		/* 图形模式: 调用统一的滚屏函数 */
		gfx_scroll_up(1);
	} else {
		/* 文本模式: rep movsw 块拷贝 */
		__asm__ __volatile__(
			"push %%ds             \n\t"
			"push %%es             \n\t"
			"movw %%gs, %%ax       \n\t"
			"movw %%ax, %%ds       \n\t"   /* DS = GS = Video段 (源段) */
			"movw %%ax, %%es       \n\t"   /* ES = GS = Video段 (目标段) */
			"xorl %%edi, %%edi     \n\t"   /* EDI = 0 (目标: 第0行) */
			"movl $160, %%esi      \n\t"   /* ESI = 160 (源: 第1行) */
			"movl $1920, %%ecx     \n\t"   /* 24行×80字 = 1920字 */
			"rep movsw             \n\t"   /* 块拷贝 */
			"pop %%es              \n\t"
			"pop %%ds"
			:
			:
			: "eax", "esi", "edi", "ecx", "memory"
		);

		/* 清空第24行 (最后一行) */
		{
			int i;
			int base = 24 * 160;  /* 文本模式: 25行 × 160字节/行 */
			for (i = 0; i < 80; i++) {
				sh_vm_write(base + i * SH_BPC, ' ', 0x07);
			}
		}
	}
}

/* 检查光标是否超出屏幕底部, 如果超出则滚屏 */
static void sh_scroll_if_needed(int *p_cursor)
{
	int row_bytes = g_text_cols * 2;
	int max_bytes = g_text_cols * g_text_rows * 2;
	while (*p_cursor >= max_bytes) {
		sh_do_scroll_up();
		*p_cursor -= row_bytes;
	}
	if (*p_cursor < 0) *p_cursor = 0;
}

/* =====================================================================
   写一行文字: 接收全局命名数组指针, 逐字符直接输出
   超出屏幕底部时自动滚屏
===================================================================== */
static void sh_put_line(int *p_cursor, const char *s, unsigned char color)
{
	int c = *p_cursor;
	int row_bytes = g_text_cols * 2;
	int max_bytes = g_text_cols * g_text_rows * 2;
	while (*s) {
		if (c >= max_bytes) {
			*p_cursor = c;
			sh_scroll_if_needed(p_cursor);
			c = *p_cursor;
		}
		sh_vm_write(c, *s, color);
		c += SH_BPC;
		s++;
	}
	*p_cursor = c;
	sh_update_hw_cursor(c);
}

/* 换行, 超出底部时自动滚屏 */
static void sh_newline(int *p_cursor)
{
	int row_bytes = g_text_cols * 2;
	int max_bytes = g_text_cols * g_text_rows * 2;
	int c = (*p_cursor / row_bytes + 1) * row_bytes;
	if (c >= max_bytes) {
		*p_cursor = c;
		sh_scroll_if_needed(p_cursor);
	} else {
		*p_cursor = c;
	}
	sh_update_hw_cursor(*p_cursor);
}


/*======================================================================*
                        硬件信息获取函数
 *======================================================================*/

/* 从 CMOS RTC 读取一个字节 */
static inline unsigned char cmos_read(unsigned char reg)
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

/* 将 BCD 码转十进制 */
static inline int bcd2dec(unsigned char bcd)
{
	return (bcd >> 4) * 10 + (bcd & 0x0F);
}

/* 获取 CPU 厂商字符串 (12字节) via CPUID EAX=0 */
static void get_cpu_vendor(char *buf)
{
	int eax, ebx, ecx, edx;
	__asm__ __volatile__(
		"movl $0, %%eax   \n\t"
		"cpuid            \n\t"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
	);
	buf[0]  = (char)(ebx & 0xFF);        buf[1]  = (char)((ebx >> 8) & 0xFF);
	buf[2]  = (char)((ebx >> 16) & 0xFF); buf[3]  = (char)((ebx >> 24) & 0xFF);
	buf[4]  = (char)(edx & 0xFF);        buf[5]  = (char)((edx >> 8) & 0xFF);
	buf[6]  = (char)((edx >> 16) & 0xFF); buf[7]  = (char)((edx >> 24) & 0xFF);
	buf[8]  = (char)(ecx & 0xFF);        buf[9]  = (char)((ecx >> 8) & 0xFF);
	buf[10] = (char)((ecx >> 16) & 0xFF);buf[11] = (char)((ecx >> 24) & 0xFF);
	buf[12] = '\0';
}

/* 获取内存大小(KB): 常规内存(0x413) + 扩展内存(CMOS)
 * 0x413 = 常规内存(最大640KB)
 * CMOS 0x30/0x31 = 1MB以上扩展内存 (16位, 最大65535KB ≈ 64MB)
 * CMOS 0x34/0x35 = 16MB以上扩展内存 (以64KB为单位, 支持>64MB) */
static inline int get_mem_kb(void)
{
	int conv_kb, ext_kb, above16;
	__asm__ __volatile__(
		"movw 0x413, %w0"
		: "=a"(conv_kb)
	);
	ext_kb  = cmos_read(0x30);
	ext_kb |= cmos_read(0x31) << 8;
	above16  = cmos_read(0x34);
	above16 |= cmos_read(0x35) << 8;

	if (above16 > 0) {
		/* 内存 > 16MB: 0x30/0x31 在>64MB时饱和, 用 0x34/0x35 更准确 */
		return 640 + 15 * 1024 + above16 * 64;
	}
	return conv_kb + ext_kb;
}

/* 输出十进制数字到屏幕 */
static void sh_put_int(int *p_cursor, int value, unsigned char color)
{
	char digits[12];
	int i = 0, neg = 0;
	if (value < 0) { neg = 1; value = -value; }
	if (value == 0) { digits[i++] = '0'; }
	else {
		while (value > 0 && i < 11) { digits[i++] = '0' + (value % 10); value /= 10; }
	}
	if (neg) {
		if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
		sh_vm_write(*p_cursor, '-', color);
		*p_cursor += SH_BPC;
	}
	while (--i >= 0) {
		if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
		sh_vm_write(*p_cursor, digits[i], color);
		*p_cursor += SH_BPC;
	}
	sh_update_hw_cursor(*p_cursor);
}


/*======================================================================*
                        dir 命令回调
 *======================================================================*/
/* dir 回调上下文 (全局变量, 供回调函数访问) */
PRIVATE int *sh_dir_p_cursor = 0;
PRIVATE int  sh_dir_count = 0;

/* dir 回调: 显示每个目录条目 */
PRIVATE void sh_dir_cb(FAT_DIR_ENTRY *de)
{
	int *p_cursor = sh_dir_p_cursor;
	int i;

	if ((t_8)de->name[0] == 0x00) return;
	if ((t_8)de->name[0] == 0xE5) return;
	if (de->attr == 0x0F) return;       /* LFN */
	if (de->attr & 0x08) return;        /* 卷标 */
	/* 跳过 "." 和 ".." */
	if (de->name[0] == '.' && (de->name[1] == ' ' || de->name[1] == '.')) return;

	sh_dir_count++;

	/* 文件名 (小写) */
	for (i = 0; i < 8; i++) {
		char c = de->name[i];
		if (c == ' ') break;
		if (c >= 'A' && c <= 'Z') c += 32;
		if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
		sh_vm_write(*p_cursor, c, 0x0F);
		*p_cursor += SH_BPC;
	}
	/* 扩展名 */
	if (de->ext[0] != ' ') {
		if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
		sh_vm_write(*p_cursor, '.', 0x07);
		*p_cursor += SH_BPC;
		for (i = 0; i < 3; i++) {
			char c = de->ext[i];
			if (c == ' ') break;
			if (c >= 'A' && c <= 'Z') c += 32;
			if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
			sh_vm_write(*p_cursor, c, 0x0F);
			*p_cursor += SH_BPC;
		}
	}
	/* 填充 */
	{
		int pad;
		for (pad = 0; pad < 4; pad++) {
			if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
			sh_vm_write(*p_cursor, ' ', 0x07);
			*p_cursor += SH_BPC;
		}
	}
	/* <DIR> 或 文件大小 */
	if (de->attr & 0x10) {
		sh_put_line(p_cursor, DIR_DIR, 0x0B);
	} else {
		sh_put_int(p_cursor, (int)de->file_size, 0x0F);
		sh_put_line(p_cursor, DIR_BYTES, 0x07);
	}
	sh_newline(p_cursor);
}


/*======================================================================*
                        路径维护 (sh_cwd_path)
 *======================================================================*/
/* 重置路径为根目录 */
PRIVATE void sh_path_reset(void)
{
	sh_cwd_path[0] = '/';
	sh_cwd_path[1] = '\0';
}

/* 进入子目录: 在路径末尾追加 name + "/" */
PRIVATE void sh_path_enter(const char *name)
{
	int i = 0;
	/* 找到当前末尾 */
	while (sh_cwd_path[i] && i < 60) i++;
	/* 追加 name */
	while (*name && i < 60) {
		sh_cwd_path[i++] = *name++;
	}
	/* 确保以 / 结尾 */
	if (i == 0 || sh_cwd_path[i - 1] != '/') {
		if (i < 63) sh_cwd_path[i++] = '/';
	}
	sh_cwd_path[i] = '\0';
}

/* 回到父目录: 去掉路径最后一段 */
PRIVATE void sh_path_parent(void)
{
	int i = 0;
	/* 找到末尾 */
	while (sh_cwd_path[i]) i++;
	/* 去掉末尾的 '/' */
	if (i > 1 && sh_cwd_path[i - 1] == '/') i--;
	/* 去掉最后一段目录名 */
	while (i > 1 && sh_cwd_path[i - 1] != '/') i--;
	/* 确保至少有根目录的 '/' */
	if (i == 0) i = 1;
	sh_cwd_path[i] = '\0';
	if (i == 1 && sh_cwd_path[0] != '/') {
		sh_cwd_path[0] = '/';
		sh_cwd_path[1] = '\0';
	}
}


/*======================================================================*
                        shell_parse_and_execute
 *======================================================================*/
/* 内联字符串相等比较 (不依赖外部strcmp, 避免符号/调用约定问题) */
static int sh_streq(const char *a, const char *b)
{
	while (*a && *b && *a == *b) { a++; b++; }
	return (*a == *b);
}

PUBLIC void shell_parse_and_execute(char* cmdline, int *p_cursor)
{
	char* p = cmdline;

	while (*p == ' ' || *p == '\t') p++;

	/* ===== help ===== */
	if (sh_streq(p, S_CMD_HELP)) {
		sh_put_line(p_cursor, S_HELP_HDR, 0x0F);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_1,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_2,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_3,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_4,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_5,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_6,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_7,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_8,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_9,   0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_10,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_11,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_12,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_13,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_14,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_15,  0x07);  sh_newline(p_cursor);
		{
			t_8 boot_drive = *((volatile t_8*)0x500);
			if (!(boot_drive & 0x80)) {
				sh_put_line(p_cursor, S_HELP_16,  0x0E);  sh_newline(p_cursor);
			}
		}
		sh_put_line(p_cursor, S_HELP_17,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_CD,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_MKDIR, 0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_18,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_19,  0x07);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_HELP_20,  0x0B);  sh_newline(p_cursor);
		return;
	}

	/* ===== ver ===== */
	if (sh_streq(p, S_CMD_VER) || sh_streq(p, S_CMD_VERSION)) {
		t_8 boot_drive = *((volatile t_8*)0x500);
		sh_put_line(p_cursor, S_VER, 0x0B);
		sh_put_line(p_cursor, (boot_drive & 0x80) ? S_EDITION_HD : S_EDITION_FP, 0x0B);
		sh_newline(p_cursor);
		return;
	}

	/* ===== about ===== */
	if (sh_streq(p, S_CMD_ABOUT)) {
		sh_put_line(p_cursor, S_ABOUT_1, 0x0B);  sh_newline(p_cursor);
		sh_put_line(p_cursor, S_ABOUT_2, 0x0B);  sh_newline(p_cursor);
		return;
	}

	/* ===== clear / cls ===== */
	if (sh_streq(p, S_CMD_CLEAR) || sh_streq(p, S_CMD_CLS)) {
		t_8 boot_drive;
		int i;
		if (g_video_mode != 0) {
			gfx_clear_screen(0);
		} else {
			for (i = 0; i < 80 * 25; i++) {
				sh_vm_write(i * SH_BPC, ' ', 0x07);
			}
		}
		*p_cursor = 0;
		sh_put_line(p_cursor, S_TITLE, 0x0A);
		boot_drive = *((volatile t_8*)0x500);
		sh_put_line(p_cursor, (boot_drive & 0x80) ? S_EDITION_HD : S_EDITION_FP, 0x0B);
		sh_newline(p_cursor);
		sh_newline(p_cursor);
		return;
	}

	/* ===== echo ===== */
	if (p[0]=='e' && p[1]=='c' && p[2]=='h' && p[3]=='o' && p[4]==' ') {
		sh_put_line(p_cursor, p + 5, 0x0F);
		sh_newline(p_cursor);
		return;
	}

	/* ===== reboot ===== */
	if (sh_streq(p, S_CMD_REBOOT)) {
		disable_int();
		/* 键盘控制器 8042 复位: 等待输入缓冲区空 (bit1=0), 然后发送 0xFE 脉冲复位线
		 * 注: 不能用 lidt+三重故障作后备 (lidt 是 ring 0 特权指令, 进程在 ring 1 会 #GP) */
		while (in_byte(0x64) & 0x02) { }   /* bit1=1 表示输入缓冲区满, 等待其变空 */
		out_byte(0x64, 0xFE);
		while (1) { }                       /* 等待 CPU 复位 */
	}

	/* ===== shutdown (ACPI S5) ===== */
	if (sh_streq(p, S_CMD_SHUTDOWN)) {
		/* ACPI PM1a_CNT_BLK 是 16位寄存器, 写入 SLP_TYP + SLP_EN(0x2000) 触发 S5 关机
		 * 不同虚拟机使用不同端口和 SLP_TYP:
		 *   QEMU(新):   port 0x604,  SLP_TYP=0  → 0x2000
		 *   QEMU(旧)/Bochs: port 0xB004, SLP_TYP=0 → 0x2000
		 *   VirtualBox: port 0x4004, SLP_TYP=0  → 0x2000
		 *   VMware:     port 0x6004, SLP_TYP=5  → 0x3400
		 * ★ Bochs 需要 32 位写 (out 0xB004, eax), 8 位写无效;
		 *   QEMU 也可用 32 位写到 0x604.
		 * 用内联汇编 out dword 保证 16 位寄存器被整体写入. */
		disable_int();
		/* Bochs / QEMU-旧: 32位写 0xB004 = 0x2000 */
		__asm__ __volatile__("outl %0, %1" : : "a"(0x2000), "d"((unsigned short)0xB004));
		/* QEMU 新: 32位写 0x604 = 0x2000 */
		__asm__ __volatile__("outl %0, %1" : : "a"(0x2000), "d"((unsigned short)0x604));
		/* VirtualBox: 32位写 0x4004 = 0x2000 */
		__asm__ __volatile__("outl %0, %1" : : "a"(0x2000), "d"((unsigned short)0x4004));
		/* VMware: 32位写 0x6004 = 0x3400 (SLP_TYP=5) */
		__asm__ __volatile__("outl %0, %1" : : "a"(0x3400), "d"((unsigned short)0x6004));
		/* 兜底: 用 8 位写 (某些 BIOS 只认 8 位写) */
		out_byte(0x6004, 0x00); out_byte(0x6005, 0x34);   /* VMware */
		out_byte(0x604,  0x00); out_byte(0x605,  0x20);   /* QEMU 新 */
		out_byte(0xB004, 0x00); out_byte(0xB005,  0x20);   /* QEMU 旧 / Bochs */
		out_byte(0x4004, 0x00); out_byte(0x4005,  0x20);   /* VirtualBox */
		sh_put_line(p_cursor, SI_UNKNOWN, 0x04); sh_newline(p_cursor);
		return;
	}

	/* ===== sysinfo (CPU / Memory / Time) ===== */
	if (sh_streq(p, S_CMD_SYSINFO)) {
		char cpu_vendor[16];
		int mem_kb;
		int sec, min, hour;

		sh_put_line(p_cursor, SI_HDR, 0x0B);  sh_newline(p_cursor);

		/* --- CPU 信息 --- */
		get_cpu_vendor(cpu_vendor);
		sh_put_line(p_cursor, SI_CPU, 0x07);
		{
			const char *v = cpu_vendor;
			while (*v) {
				if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
				sh_vm_write(*p_cursor, *v, 0x0F);
				*p_cursor += SH_BPC; v++;
			}
			sh_update_hw_cursor(*p_cursor);
		}
		sh_newline(p_cursor);

		/* --- 内存信息 --- */
		mem_kb = get_mem_kb();
		sh_put_line(p_cursor, SI_MEM, 0x07);
		sh_put_int(p_cursor, mem_kb, 0x0F);
		sh_put_line(p_cursor, SI_MEM_KB, 0x07);
		sh_newline(p_cursor);

		/* --- 当前时间 (CMOS RTC) --- */
		sec  = bcd2dec(cmos_read(0x00));
		min  = bcd2dec(cmos_read(0x02));
		hour = bcd2dec(cmos_read(0x04));
		sh_put_line(p_cursor, SI_TIME, 0x07);
		if (hour < 10) {
			if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
			sh_vm_write(*p_cursor, '0', 0x0F); *p_cursor += SH_BPC;
		}
		sh_put_int(p_cursor, hour, 0x0F);
		{
			if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
			sh_vm_write(*p_cursor, ':', 0x07); *p_cursor += SH_BPC;
		}
		if (min < 10) {
			if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
			sh_vm_write(*p_cursor, '0', 0x0F); *p_cursor += SH_BPC;
		}
		sh_put_int(p_cursor, min, 0x0F);
		{
			if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
			sh_vm_write(*p_cursor, ':', 0x07); *p_cursor += SH_BPC;
		}
		if (sec < 10) {
			if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
			sh_vm_write(*p_cursor, '0', 0x0F); *p_cursor += SH_BPC;
		}
		sh_put_int(p_cursor, sec, 0x0F);
		sh_update_hw_cursor(*p_cursor);
		sh_newline(p_cursor);

		return;
	}

	/* ===== diskinfo (硬盘信息 + 文件系统信息) ===== */
	if (sh_streq(p, S_CMD_DISKINFO)) {
		HD_INFO hd;
		FAT_INFO *fi;

		sh_put_line(p_cursor, DI_HDR, 0x0B);  sh_newline(p_cursor);

		/* 硬盘识别 */
		hd_init();
		if (hd_identify(&hd) == 0) {
			sh_put_line(p_cursor, DI_MODEL, 0x07);
			{
				int i;
				/* 去除型号末尾空格 */
				int len = 40;
				while (len > 0 && hd.model[len-1] == ' ') len--;
				for (i = 0; i < len; i++) {
					if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
					sh_vm_write(*p_cursor, hd.model[i], 0x0F);
					*p_cursor += SH_BPC;
				}
				sh_update_hw_cursor(*p_cursor);
			}
			sh_newline(p_cursor);

			sh_put_line(p_cursor, DI_SECTORS, 0x07);
			sh_put_int(p_cursor, (int)hd.lba_sectors, 0x0F);
			sh_newline(p_cursor);

			sh_put_line(p_cursor, DI_CAPACITY, 0x07);
			sh_put_int(p_cursor, (int)hd.capacity_kb / 1024, 0x0F);
			sh_put_line(p_cursor, DI_MB, 0x07);
			sh_newline(p_cursor);

			sh_put_line(p_cursor, DI_CHS, 0x07);
			sh_put_int(p_cursor, (int)hd.cylinders, 0x0F);
			sh_put_line(p_cursor, DI_SLASH, 0x07);
			sh_put_int(p_cursor, (int)hd.heads, 0x0F);
			sh_put_line(p_cursor, DI_SLASH, 0x07);
			sh_put_int(p_cursor, (int)hd.sectors_per_track, 0x0F);
			sh_newline(p_cursor);
		} else {
			sh_put_line(p_cursor, DI_NOHD, 0x0C);
			sh_newline(p_cursor);
		}

		/* 文件系统信息 */
	sh_put_line(p_cursor, DI_FS_HDR, 0x0B);  sh_newline(p_cursor);
	if (fat_init() == 0) {
		fi = fat_get_info();

		/* Type: FAT12 / FAT16 / FAT32 */
		sh_put_line(p_cursor, DI_FS_TYPE, 0x07);
		{
			char ft[4];
			ft[0] = '0' + (fi->fat_type / 10);
			ft[1] = '0' + (fi->fat_type % 10);
			ft[2] = '\0';
			int i;
			for (i = 0; ft[i]; i++) {
				if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
				sh_vm_write(*p_cursor, ft[i], 0x0F);
				*p_cursor += SH_BPC;
			}
			sh_update_hw_cursor(*p_cursor);
		}
		sh_newline(p_cursor);

		/* Volume label */
		sh_put_line(p_cursor, DI_FS_VOL, 0x07);
		{
			int i;
			for (i = 0; i < 11 && fi->volume_label[i] && fi->volume_label[i] != ' '; i++) {
				if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
				sh_vm_write(*p_cursor, fi->volume_label[i], 0x0F);
				*p_cursor += SH_BPC;
			}
			sh_update_hw_cursor(*p_cursor);
		}
		sh_newline(p_cursor);

		/* Total sectors */
		sh_put_line(p_cursor, DI_FS_SEC, 0x07);
		sh_put_int(p_cursor, (int)fi->total_sectors, 0x0F);
		sh_put_line(p_cursor, DI_SECS_UNIT, 0x07);
		sh_newline(p_cursor);

		/* Total clusters */
		sh_put_line(p_cursor, DI_FS_TOTAL, 0x07);
		sh_put_int(p_cursor, (int)fi->total_clusters, 0x0F);
		sh_newline(p_cursor);

		/* Sectors per cluster */
		sh_put_line(p_cursor, DI_FS_SPC, 0x07);
		sh_put_int(p_cursor, (int)fi->sectors_per_cluster, 0x0F);
		sh_newline(p_cursor);

		/* Cluster size */
		sh_put_line(p_cursor, DI_FS_CLUSSZ, 0x07);
		sh_put_int(p_cursor, (int)(fi->sectors_per_cluster * fi->bytes_per_sector), 0x0F);
		sh_put_line(p_cursor, DI_BYTES_UNIT, 0x07);
		sh_newline(p_cursor);

		/* FAT size */
		sh_put_line(p_cursor, DI_FS_FATSZ, 0x07);
		sh_put_int(p_cursor, (int)fi->fat_size, 0x0F);
		sh_put_line(p_cursor, DI_SECS_UNIT, 0x07);
		sh_newline(p_cursor);

		/* Number of FATs */
		sh_put_line(p_cursor, DI_FS_NFATS, 0x07);
		sh_put_int(p_cursor, (int)fi->num_fats, 0x0F);
		sh_newline(p_cursor);

		/* Reserved sectors */
		sh_put_line(p_cursor, DI_FS_RSVD, 0x07);
		sh_put_int(p_cursor, (int)fi->reserved_sectors, 0x0F);
		sh_newline(p_cursor);

		/* Root directory info: FAT12/16=entries+sectors, FAT32=cluster */
		sh_put_line(p_cursor, DI_FS_ROOT, 0x07);
		if (fi->fat_type == 32) {
			sh_put_line(p_cursor, DI_CLUSTER, 0x0F);
			sh_put_int(p_cursor, (int)fi->root_cluster, 0x0F);
		} else {
			sh_put_int(p_cursor, (int)fi->root_entry_count, 0x0F);
			sh_put_line(p_cursor, DI_ENTRIES, 0x07);
			sh_put_line(p_cursor, DI_ROOT_SECS, 0x07);
			sh_put_int(p_cursor, (int)fi->root_dir_sectors, 0x0F);
			sh_put_line(p_cursor, DI_ROOT_SECS2, 0x07);
		}
		sh_newline(p_cursor);

		/* FAT32 专属: FSInfo 和 Free Hint */
		if (fi->fat_type == 32) {
			sh_put_line(p_cursor, DI_FS_FSI, 0x07);
			sh_put_int(p_cursor, (int)fi->fsinfo_sector, 0x0F);
			sh_newline(p_cursor);

			sh_put_line(p_cursor, DI_FS_FREE, 0x07);
			if (fi->free_count_hint == 0xFFFFFFFF) {
				sh_put_line(p_cursor, SI_UNKNOWN, 0x0F);
			} else {
				sh_put_int(p_cursor, (int)fi->free_count_hint, 0x0F);
			}
			sh_newline(p_cursor);
		}

		/* Partition start LBA */
		sh_put_line(p_cursor, DI_FS_PART, 0x07);
		sh_put_int(p_cursor, (int)fi->partition_start, 0x0F);
		sh_newline(p_cursor);
	} else {
		sh_put_line(p_cursor, DI_FS_NONE, 0x0C);
		sh_newline(p_cursor);
	}

	return;
	}

	/* ===== dir / ls (列出当前目录) ===== */
	if (sh_streq(p, S_CMD_DIR) || sh_streq(p, S_CMD_LS)) {
		/* 初始化文件系统 */
		hd_init();
		if (fat_init() != 0) {
			sh_put_line(p_cursor, DI_FS_NONE, 0x0C);
			sh_newline(p_cursor);
			return;
		}

		sh_put_line(p_cursor, DIR_HDR, 0x0B);  sh_newline(p_cursor);
		{
			sh_dir_p_cursor = p_cursor;
			sh_dir_count = 0;
			fat_list_dir(sh_cwd, sh_dir_cb);
			if (sh_dir_count == 0) {
				sh_put_line(p_cursor, DIR_EMPTY, 0x08);
				sh_newline(p_cursor);
			}
		}
		return;
	}

	/* ===== type <filename> (显示文件内容) ===== */
	if (sh_streq(p, S_CMD_TYPE)) {
		/* type 后面必须跟文件名 */
		char *fname = p;
		while (*fname == ' ') fname++;
		/* 跳过 "type" 这4个字符 */
		fname = p + 4;
		while (*fname == ' ') fname++;

		if (*fname == '\0') {
			sh_put_line(p_cursor, TYPE_USAGE, 0x0C);
			sh_newline(p_cursor);
			return;
		}

		hd_init();
		if (fat_init() != 0) {
			sh_put_line(p_cursor, DI_FS_NONE, 0x0C);
			sh_newline(p_cursor);
			return;
		}

		{
			/* 文件缓冲区 (4KB, 足够显示小文本文件) */
			static t_8 file_buf[4096];
			int n;
			int i;

			n = fat_read_file_in(sh_cwd, fname, file_buf, sizeof(file_buf));
			if (n < 0) {
				sh_put_line(p_cursor, TYPE_NOTFOUND, 0x0C);
				/* 显示文件名 */
				while (*fname) {
					if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
					sh_vm_write(*p_cursor, *fname, 0x0C);
					*p_cursor += SH_BPC;
					fname++;
				}
				sh_update_hw_cursor(*p_cursor);
				sh_newline(p_cursor);
				return;
			}

			/* 显示文件内容 */
			for (i = 0; i < n; i++) {
				char c = (char)file_buf[i];
				if (c == '\r') continue;      /* 跳过 CR */
				if (c == '\t') c = ' ';       /* Tab -> 空格 */
				if (c == '\n') {
					sh_newline(p_cursor);
					continue;
				}
				if ((t_8)c < 0x20 && c != '\n') continue; /* 跳过不可打印字符 */
				if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
				sh_vm_write(*p_cursor, c, 0x0F);
				*p_cursor += SH_BPC;
			}
			sh_update_hw_cursor(*p_cursor);
			sh_newline(p_cursor);
		}
		return;
	}

	/* ===== taskmgr (启动任务管理器应用程序) ===== */
	if (sh_streq(p, S_CMD_TASKMGR)) {
		/* 调用 taskmgr_run, 接管键盘和显示
		 * 直到用户按下 Ctrl-C 才返回
		 * 返回后 *p_cursor 被置为 0 (清屏), shell 重新打印提示符 */
		taskmgr_run(p_cursor);
		return;
	}

	/* ===== format (格式化硬盘) ===== */
	/* format            - 显示硬盘列表 + FAT 类型选择 + 用法 */
	/* format 1 32       - 显示确认提示 */
	/* format 1 32 y     - 执行格式化 */
	if (p[0]=='f' && p[1]=='o' && p[2]=='r' && p[3]=='m' && p[4]=='a' && p[5]=='t') {
		HD_INFO hdi;

		/* === format (无参数): 显示硬盘列表 + 用法 === */
		if (p[6] == '\0' || (p[6] == ' ' && p[7] == '\0')) {
			sh_put_line(p_cursor, FMT_HDR, 0x0E);  sh_newline(p_cursor);

			if (hd_identify(&hdi) != 0) {
				sh_put_line(p_cursor, FMT_NOHD, 0x0C);  sh_newline(p_cursor);
				return;
			}

			/* 硬盘列表 */
			sh_put_line(p_cursor, FMT_DISK_HDR, 0x0B);  sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_DISK_NUM, 0x0F);
			sh_put_line(p_cursor, hdi.model, 0x0F);
			sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_CAPACITY, 0x07);
			sh_put_int(p_cursor, hdi.capacity_kb / 1024, 0x0F);
			sh_put_line(p_cursor, FMT_MB, 0x07);
			sh_put_int(p_cursor, (int)hdi.lba_sectors, 0x0F);
			sh_put_line(p_cursor, FMT_SECTORS, 0x07);
			sh_newline(p_cursor);
			sh_newline(p_cursor);

			/* FAT 类型选择 */
			sh_put_line(p_cursor, FMT_TYPE_HDR, 0x0B);  sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_TYPE_AUTO, 0x07);  sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_TYPE_12, 0x07);    sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_TYPE_16, 0x07);    sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_TYPE_32, 0x0A);    sh_newline(p_cursor);
			sh_newline(p_cursor);

			/* 用法 */
			sh_put_line(p_cursor, FMT_USAGE, 0x0E);  sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_USAGE2, 0x07);  sh_newline(p_cursor);
			return;
		}

		/* === format 1 32 (显示确认提示) === */
		/* === format 1 32 y (执行格式化) === */
		if (p[6] == ' ' && p[7] >= '1' && p[7] <= '9') {
			int disk_num = p[7] - '0';
			int fat_type = -1;
			int has_type = 0;
			int confirm = 0;
			char *args = &p[8];

			/* 解析第二个参数 (FAT 类型) */
			if (args[0] == ' ') {
				args++;
				/* 解析数字 */
				if (args[0] >= '0' && args[0] <= '9') {
					fat_type = 0;
					while (*args >= '0' && *args <= '9') {
						fat_type = fat_type * 10 + (*args - '0');
						args++;
					}
					has_type = 1;
					/* 检查是否有 'y' 确认 */
					if (args[0] == ' ') {
						args++;
						if (args[0] == 'y' && args[1] == '\0') {
							confirm = 1;
						}
					} else if (args[0] == '\0') {
						/* 仅显示确认提示 */
					}
				}
			}

			/* 校验硬盘号 (目前只支持 1 个硬盘) */
			if (disk_num != 1) {
				sh_put_line(p_cursor, FMT_BADDISK, 0x0C);  sh_newline(p_cursor);
				return;
			}

			/* 校验 FAT 类型 */
			if (!has_type || (fat_type != 0 && fat_type != 12 && fat_type != 16 && fat_type != 32)) {
				sh_put_line(p_cursor, FMT_BADTYPE, 0x0C);  sh_newline(p_cursor);
				return;
			}

			if (!confirm) {
				/* 显示确认提示 */
				if (hd_identify(&hdi) != 0) {
					sh_put_line(p_cursor, FMT_NOHD, 0x0C);  sh_newline(p_cursor);
					return;
				}
				sh_put_line(p_cursor, FMT_HDR, 0x0E);  sh_newline(p_cursor);
				sh_put_line(p_cursor, FMT_DISK_NUM, 0x0F);
				sh_put_line(p_cursor, hdi.model, 0x0F);
				sh_newline(p_cursor);
				sh_put_line(p_cursor, FMT_CAPACITY, 0x07);
				sh_put_int(p_cursor, hdi.capacity_kb / 1024, 0x0F);
				sh_put_line(p_cursor, FMT_MB, 0x07);
				sh_put_int(p_cursor, (int)hdi.lba_sectors, 0x0F);
				sh_put_line(p_cursor, FMT_SECTORS, 0x07);
				sh_newline(p_cursor);
				sh_newline(p_cursor);

				sh_put_line(p_cursor, FMT_TYPE_HDR, 0x0B);
				/* 显示选择的类型 */
				{
					char ft[4];
					ft[0] = '0' + (fat_type / 10);
					ft[1] = '0' + (fat_type % 10);
					ft[2] = '\0';
					int i;
					for (i = 0; ft[i]; i++) {
						if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
						sh_vm_write(*p_cursor, ft[i], 0x0A);
						*p_cursor += SH_BPC;
					}
					sh_update_hw_cursor(*p_cursor);
				}
				sh_newline(p_cursor);
				sh_newline(p_cursor);

				/* 提示确认命令 */
				sh_put_line(p_cursor, FMT_CONFIRM2, 0x0E);
				/* 显示具体的确认命令 */
				{
					/* "format 1 XX y" */
					static char fmt_cmd_start[] = "  format 1 ";
					char ft[4];
					int i;
					ft[0] = '0' + (fat_type / 10);
					ft[1] = '0' + (fat_type % 10);
					ft[2] = ' ';
					ft[3] = 'y';

					sh_put_line(p_cursor, fmt_cmd_start, 0x0F);
					for (i = 0; i < 4; i++) {
						if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
						sh_vm_write(*p_cursor, ft[i], 0x0F);
						*p_cursor += SH_BPC;
					}
					sh_update_hw_cursor(*p_cursor);
				}
				sh_newline(p_cursor);
				return;
			}

			/* === 执行格式化 === */
			if (hd_identify(&hdi) != 0) {
				sh_put_line(p_cursor, FMT_NOHD, 0x0C);  sh_newline(p_cursor);
				return;
			}
			if (fat_format(hdi.lba_sectors, fat_type) == 0) {
				FAT_INFO *nfi = fat_get_info();
				sh_put_line(p_cursor, FMT_OK, 0x0A);
				if (nfi != 0) {
					/* 动态显示 FAT 类型 (12/16/32) */
					char ft[4];
					ft[0] = '0' + (nfi->fat_type / 10);
					ft[1] = '0' + (nfi->fat_type % 10);
					ft[2] = '\0';
					{
						int i;
						for (i = 0; ft[i]; i++) {
							if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
							sh_vm_write(*p_cursor, ft[i], 0x0A);
							*p_cursor += SH_BPC;
						}
						sh_update_hw_cursor(*p_cursor);
					}
				}
				sh_put_line(p_cursor, FMT_OK2, 0x0A);
			sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_RUNSETUP, 0x0E);  sh_newline(p_cursor);
			} else {
				sh_put_line(p_cursor, FMT_FAIL, 0x0C);  sh_newline(p_cursor);
			}
			return;
		}

		/* 兼容旧格式: format y */
		if (p[6] == ' ' && p[7] == 'y' && p[8] == '\0') {
			HD_INFO hdi2;
			if (hd_identify(&hdi2) != 0) {
				sh_put_line(p_cursor, FMT_NOHD, 0x0C);  sh_newline(p_cursor);
				return;
			}
			if (fat_format(hdi2.lba_sectors, 0) == 0) {
				FAT_INFO *nfi = fat_get_info();
				sh_put_line(p_cursor, FMT_OK, 0x0A);
				if (nfi != 0) {
					char ft[4];
					ft[0] = '0' + (nfi->fat_type / 10);
					ft[1] = '0' + (nfi->fat_type % 10);
					ft[2] = '\0';
					{
						int i;
						for (i = 0; ft[i]; i++) {
							if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
							sh_vm_write(*p_cursor, ft[i], 0x0A);
							*p_cursor += SH_BPC;
						}
						sh_update_hw_cursor(*p_cursor);
					}
				}
				sh_put_line(p_cursor, FMT_OK2, 0x0A);
			sh_newline(p_cursor);
			sh_put_line(p_cursor, FMT_RUNSETUP, 0x0E);  sh_newline(p_cursor);
			} else {
				sh_put_line(p_cursor, FMT_FAIL, 0x0C);  sh_newline(p_cursor);
			}
			return;
		}

		/* 未知 format 参数 */
		sh_put_line(p_cursor, FMT_USAGE, 0x0E);  sh_newline(p_cursor);
		sh_put_line(p_cursor, FMT_USAGE2, 0x07);  sh_newline(p_cursor);
		return;
	}

	/* ===== setup (交互式安装向导) ===== */
	if (p[0]=='s' && p[1]=='e' && p[2]=='t' && p[3]=='u' && p[4]=='p') {
		/* Disk edition 不提供 setup 命令 (系统已安装到硬盘) */
		{
			t_8 boot_drive = *((volatile t_8*)0x500);
			if (boot_drive & 0x80) {
				sh_put_line(p_cursor, SETUP_NODISK, 0x0C);  sh_newline(p_cursor);
				return;
			}
		}
		/* 启动交互式安装向导 (全屏 UI, 类似 Windows XP) */
		installer_run(p_cursor);
		return;
	}

	/* ===== touch (创建空文件) ===== */
	if (p[0]=='t' && p[1]=='o' && p[2]=='u' && p[3]=='c' && p[4]=='h' && p[5]==' ') {
		char *fname = p + 6;
		int ret;
		if (*fname == '\0') {
			sh_put_line(p_cursor, TOUCH_NONAME, 0x0E);  sh_newline(p_cursor);
			return;
		}
		if (fat_init() != 0) {
			sh_put_line(p_cursor, DI_FS_NONE, 0x0C);  sh_newline(p_cursor);
			return;
		}
		ret = fat_touch_in(sh_cwd, fname);
		if (ret == 0) {
			sh_put_line(p_cursor, TOUCH_OK, 0x0A);
			sh_put_line(p_cursor, fname, 0x0F);
			sh_newline(p_cursor);
		} else if (ret == 1) {
			sh_put_line(p_cursor, TOUCH_EXIST, 0x0E);
			sh_put_line(p_cursor, fname, 0x0F);
			sh_newline(p_cursor);
		} else {
			/* 显示具体错误原因 */
			switch (ret) {
				case -1:  sh_put_line(p_cursor, TOUCH_ERR1, 0x0C); break;
				case -2:  sh_put_line(p_cursor, TOUCH_ERR2, 0x0C); break;
				case -3:  sh_put_line(p_cursor, TOUCH_ERR3, 0x0C); break;
				case -4:  sh_put_line(p_cursor, TOUCH_ERR4, 0x0C); break;
				case -10: sh_put_line(p_cursor, TOUCH_ERR_W1, 0x0C); break;
				case -11: sh_put_line(p_cursor, TOUCH_ERR_W2, 0x0C); break;
				case -12: sh_put_line(p_cursor, TOUCH_ERR_W3, 0x0C); break;
				case -13: sh_put_line(p_cursor, TOUCH_ERR_W4, 0x0C); break;
				default:  sh_put_line(p_cursor, TOUCH_FAIL, 0x0C); break;
			}
			/* 读取失败 (-2, -4) 或写入失败 (-10~-13): 显示扇区地址和 HD 错误码 */
			if (ret == -2 || ret == -4 || ret <= -10) {
				FAT_INFO *fi = fat_get_info();
				sh_put_line(p_cursor, TOUCH_ERR_SEC, 0x0E);
				sh_put_int(p_cursor, (int)fat_err_sec, 0x0E);
				sh_put_line(p_cursor, TOUCH_ERR_CODE, 0x0E);
				sh_put_int(p_cursor, fat_err_code, 0x0E);
				/* 读取失败的 HD 错误码: -1=not ready, -2=DRQ timeout, -3=ERR flag */
				if (ret == -2 || ret == -4) {
					if (fat_err_code == -1) sh_put_line(p_cursor, TOUCH_ERR_R1, 0x0C);
					else if (fat_err_code == -2) sh_put_line(p_cursor, TOUCH_ERR_R2, 0x0C);
					else if (fat_err_code == -3) sh_put_line(p_cursor, TOUCH_ERR_R3, 0x0C);
				}
				/* 显示 FAT 关键参数 */
				if (fi) {
					sh_newline(p_cursor);
					sh_put_line(p_cursor, TOUCH_ERR_FAT, 0x0B);
					sh_put_int(p_cursor, fi->fat_type, 0x0F);
					sh_put_line(p_cursor, "/", 0x0F);
					sh_put_int(p_cursor, (int)fi->data_start, 0x0F);
					sh_put_line(p_cursor, "/", 0x0F);
					sh_put_int(p_cursor, (int)fi->root_cluster, 0x0F);
				}
			}
			sh_newline(p_cursor);
		}
		return;
	}

	/* ===== rd (删除文件) ===== */
	if (p[0]=='r' && p[1]=='d' && p[2]==' ') {
		char *fname = p + 3;
		int ret;
		if (*fname == '\0') {
			sh_put_line(p_cursor, RD_NONAME, 0x0E);  sh_newline(p_cursor);
			return;
		}
		if (fat_init() != 0) {
			sh_put_line(p_cursor, DI_FS_NONE, 0x0C);  sh_newline(p_cursor);
			return;
		}
		ret = fat_delete_in(sh_cwd, fname);
		if (ret == 0) {
			sh_put_line(p_cursor, RD_OK, 0x0A);
			sh_put_line(p_cursor, fname, 0x0F);
			sh_newline(p_cursor);
		} else if (ret == 1) {
			sh_put_line(p_cursor, RD_NOTFOUND, 0x0C);
			sh_put_line(p_cursor, fname, 0x0F);
			sh_newline(p_cursor);
		} else {
			switch (ret) {
				case -1:  sh_put_line(p_cursor, RD_ERR1, 0x0C); break;
				case -2:  sh_put_line(p_cursor, RD_ERR2, 0x0C); break;
				case -3:  sh_put_line(p_cursor, RD_ERR3, 0x0C); break;
				case -4:  sh_put_line(p_cursor, RD_ERR4, 0x0C); break;
				default:  sh_put_line(p_cursor, RD_ERR4, 0x0C); break;
			}
			if (ret <= -2) {
				sh_put_line(p_cursor, TOUCH_ERR_SEC, 0x0E);
				sh_put_int(p_cursor, (int)fat_err_sec, 0x0E);
				sh_put_line(p_cursor, TOUCH_ERR_CODE, 0x0E);
				sh_put_int(p_cursor, fat_err_code, 0x0E);
			}
			sh_newline(p_cursor);
		}
		return;
	}

	/* ===== cd (切换当前目录) =====
	 * cd           → 回到根目录
	 * cd \ / cd /  → 回到根目录
	 * cd .         → 当前目录 (不变)
	 * cd ..        → 父目录 (根目录时不变)
	 * cd <name>    → 进入指定子目录
	 */
	if (p[0]=='c' && p[1]=='d' && (p[2]=='\0' || p[2]==' ')) {
		char *arg;
		/* cd 无参数 → 回到根目录 */
		if (p[2] == '\0') {
			sh_cwd = 0;
			sh_path_reset();
			sh_put_line(p_cursor, S_CD_ROOT, 0x0B);
			sh_newline(p_cursor);
			return;
		}
		arg = p + 3;
		while (*arg == ' ') arg++;
		/* cd (空参数) → 回到根目录 */
		if (*arg == '\0') {
			sh_cwd = 0;
			sh_path_reset();
			sh_put_line(p_cursor, S_CD_ROOT, 0x0B);
			sh_newline(p_cursor);
			return;
		}
		/* cd \ 或 cd / → 根目录 */
		if (*arg == '\\' || *arg == '/') {
			sh_cwd = 0;
			sh_path_reset();
			sh_put_line(p_cursor, S_CD_ROOT, 0x0B);
			sh_newline(p_cursor);
			return;
		}
		/* cd . → 当前目录 (不变) */
		if (arg[0] == '.' && arg[1] == '\0') {
			return;
		}
		/* cd .. → 父目录 */
		if (arg[0] == '.' && arg[1] == '.' && arg[2] == '\0') {
			if (sh_cwd == 0) return;  /* 已在根目录 */
			hd_init();
			if (fat_init() != 0) {
				sh_put_line(p_cursor, DI_FS_NONE, 0x0C);  sh_newline(p_cursor);
				return;
			}
			{
				t_32 parent = fat_find_subdir(sh_cwd, "..");
				/* parent>=2: 父目录簇; parent==0: 父目录是根目录 */
				sh_cwd = (parent >= 2) ? parent : 0;
				sh_path_parent();
			}
			return;
		}
		/* cd <name> → 进入子目录 */
		hd_init();
		if (fat_init() != 0) {
			sh_put_line(p_cursor, DI_FS_NONE, 0x0C);  sh_newline(p_cursor);
			return;
		}
		{
			t_32 target = fat_find_subdir(sh_cwd, arg);
			if (target >= 2) {
				sh_cwd = target;
				sh_path_enter(arg);
			} else {
				/* 0=未找到/非目录, 0xFFFFFFFF=读盘错误 */
				sh_put_line(p_cursor, S_CD_NF, 0x0C);
				while (*arg) {
					if (*p_cursor >= SH_MAX) sh_scroll_if_needed(p_cursor);
					sh_vm_write(*p_cursor, *arg, 0x0C);
					*p_cursor += SH_BPC;
					arg++;
				}
				sh_update_hw_cursor(*p_cursor);
				sh_newline(p_cursor);
			}
		}
		return;
	}

	/* ===== mkdir (创建子目录) ===== */
	if (p[0]=='m' && p[1]=='k' && p[2]=='d' && p[3]=='i' && p[4]=='r' &&
	    (p[5]==' ' || p[5]=='\0')) {
		char *arg;
		if (p[5] == '\0') {
			sh_put_line(p_cursor, S_MKDIR_USE, 0x0E);  sh_newline(p_cursor);
			return;
		}
		arg = p + 6;
		while (*arg == ' ') arg++;
		if (*arg == '\0') {
			sh_put_line(p_cursor, S_MKDIR_USE, 0x0E);  sh_newline(p_cursor);
			return;
		}
		hd_init();
		if (fat_init() != 0) {
			sh_put_line(p_cursor, DI_FS_NONE, 0x0C);  sh_newline(p_cursor);
			return;
		}
		{
			int ret = fat_mkdir_in(sh_cwd, arg);
			if (ret == 0) {
				sh_put_line(p_cursor, S_MKDIR_OK, 0x0A);
				sh_put_line(p_cursor, arg, 0x0F);
				sh_newline(p_cursor);
			} else if (ret == 1) {
				sh_put_line(p_cursor, S_MKDIR_EX, 0x0E);
				sh_put_line(p_cursor, arg, 0x0F);
				sh_newline(p_cursor);
			} else if (ret == -5) {
				sh_put_line(p_cursor, S_MKDIR_FULL, 0x0C);
				sh_newline(p_cursor);
			} else {
				sh_put_line(p_cursor, S_MKDIR_FAIL, 0x0C);
				sh_newline(p_cursor);
			}
		}
		return;
	}

	/* ===== run (运行可执行文件) =====
	 * run <file>     - 前台阻塞: 等待程序退出后返回 shell
	 * run <file> &   - 后台非阻塞: 启动后立即返回 shell, 程序并发运行
	 */
	if (p[0]=='r' && p[1]=='u' && p[2]=='n' && p[3]==' ') {
		char *fname = p + 4;
		char *amp;
		int ret;
		int background = 0;
		int exit_code;
		PROCESS* child;
		if (*fname == '\0') {
			sh_put_line(p_cursor, S_RUN_NONAME, 0x0E);  sh_newline(p_cursor);
			return;
		}
		/* 检查末尾的 '&' (后台标记) */
		amp = fname;
		while (*amp != '\0') amp++;
		if (amp > fname) amp--;
		while (amp > fname && (*amp == ' ' || *amp == '\t')) amp--;
		if (*amp == '&') {
			background = 1;
			*amp = '\0';
			/* 再去掉末尾空格 */
			amp--;
			while (amp >= fname && (*amp == ' ' || *amp == '\t')) {
				*amp = '\0';
				amp--;
			}
		}
		if (*fname == '\0') {
			sh_put_line(p_cursor, S_RUN_NONAME, 0x0E);  sh_newline(p_cursor);
			return;
		}
		if (fat_init() != 0) {
			sh_put_line(p_cursor, DI_FS_NONE, 0x0C);  sh_newline(p_cursor);
			return;
		}
		/* 检查是否还有空闲槽位 */
		if (user_proc_free_slots <= 0) {
			sh_put_line(p_cursor, S_RUN_BUSY, 0x0C);  sh_newline(p_cursor);
			return;
		}
		/* 调用加载器 (返回槽位号 >=0 表示成功), 在当前目录下查找 */
		ret = exec_user_program_in(sh_cwd, fname);
		if (ret < 0) {
			switch (ret) {
				case -1:
					sh_put_line(p_cursor, S_RUN_NOTFOUND, 0x0C);
					sh_put_line(p_cursor, fname, 0x0F);
					sh_newline(p_cursor);
					break;
				case -2:
				sh_put_line(p_cursor, S_RUN_BADFMT, 0x0C);  sh_newline(p_cursor);
				break;
			case -21:
				{
					extern int exec_debug_n;
					sh_put_line(p_cursor, "Bad format: file too small, read ", 0x0C);
					sh_put_int(p_cursor, exec_debug_n, 0x0E);
					sh_put_line(p_cursor, " bytes", 0x0C);
					sh_newline(p_cursor);
				}
				break;
			case -22:
				sh_put_line(p_cursor, "Bad format: wrong magic", 0x0C);  sh_newline(p_cursor);
				break;
			case -23:
				sh_put_line(p_cursor, "Bad format: wrong version", 0x0C);  sh_newline(p_cursor);
				break;
			case -3:
				sh_put_line(p_cursor, S_RUN_TOOLARGE, 0x0C);  sh_newline(p_cursor);
				break;
			case -4:
				sh_put_line(p_cursor, S_RUN_BUSY, 0x0C);  sh_newline(p_cursor);
				break;
			case -5:
				sh_put_line(p_cursor, S_RUN_CORRUPT, 0x0C);  sh_newline(p_cursor);
				break;
			default:
				sh_put_line(p_cursor, S_RUN_BADFMT, 0x0C);  sh_newline(p_cursor);
				break;
		}
			return;
		}
		/* 显示启动信息 */
		sh_put_line(p_cursor, S_RUN_START, 0x0A);
		sh_put_line(p_cursor, fname, 0x0F);
		sh_put_line(p_cursor, S_RUN_SLOT, 0x0A);
		sh_put_int(p_cursor, ret, 0x0E);
		sh_put_line(p_cursor, S_RUN_CLOSE, 0x0A);
		if (background) {
			sh_put_line(p_cursor, " [bg]", 0x0B);
		}
		sh_newline(p_cursor);

		if (!background) {
			/* 前台阻塞: 等待该槽位的程序退出.
			 * 用 child 指针精确跟踪, 支持多程序并发时只等自己的子进程 */
			child = &proc_table[NR_TASKS + NR_PROCS + ret];
			/* 恢复 p_proc_ready=TTY (shell), 让 shell 执行等待循环 */
			p_proc_ready = &proc_table[0];
			while (child->is_user_proc) {
				/* 图形模式: 调用 wm_step 继续处理窗口事件 (刷新/键盘/鼠标),
				 * 避免 wm_run 主循环卡死导致子进程窗口无法交互.
				 * 文本模式: 空循环, 由调度器在时钟中断时切换到子进程. */
				if (g_video_mode != 0) {
					wm_step();
				}
			}
			/* 同步光标位置 */
			if (g_video_mode != 0) {
				int gpos = gfx_get_cursor_pos();
				if (gpos >= 0 && gpos < SH_MAX) {
					*p_cursor = gpos;
				}
			}
			/* 程序已退出, 显示退出码 */
			exit_code = child->exit_code;
			sh_put_line(p_cursor, S_RUN_EXITED, 0x0B);
			sh_put_int(p_cursor, exit_code, 0x0B);
			sh_put_line(p_cursor, S_RUN_CLOSE, 0x0B);
			sh_newline(p_cursor);
		}
		return;
	}

	/* ===== ps (列出运行中的进程) ===== */
	if (p[0]=='p' && p[1]=='s' && (p[2]=='\0' || p[2]==' ')) {
		int i;
		sh_put_line(p_cursor, "PID  NAME            PRI  STATE", 0x0B);  sh_newline(p_cursor);
		for (i = 0; i < NR_TASKS + NR_PROCS + NR_USER_PROCS; i++) {
			PROCESS* pr = &proc_table[i];
			if (pr->name[0] == 0) continue;
			sh_put_int(p_cursor, pr->pid, 0x0F);
			sh_put_line(p_cursor, (pr->pid < 10) ? "   " : "  ", 0x07);
			sh_put_line(p_cursor, pr->name, 0x0F);
			/* 对齐 */
			{
				int nl;
				for (nl = strlen(pr->name); nl < 15; nl++)
					sh_put_line(p_cursor, " ", 0x07);
			}
			sh_put_int(p_cursor, pr->priority, 0x0E);
			sh_put_line(p_cursor, (pr->priority < 10) ? "    " : "   ", 0x07);
			if (pr->is_user_proc)
				sh_put_line(p_cursor, "RUN", 0x0A);
			else if (pr->priority > 0)
				sh_put_line(p_cursor, "run", 0x0B);
			else
				sh_put_line(p_cursor, "idle", 0x08);
			sh_newline(p_cursor);
		}
		return;
	}

	/* ===== cat (进入图形桌面) ===== */
	if (p[0]=='c' && p[1]=='a' && p[2]=='t' && (p[3]=='\0' || p[3]==' ')) {
		if (g_video_mode == 0) {
			sh_put_line(p_cursor, "GUI requires graphics mode", 0x0C);
			sh_newline(p_cursor);
			return;
		}
		/* 进入窗口管理器 (ESC 退出返回 shell) */
		wm_run();
		/* wm_run 返回后, 清屏并重置 shell 显示 */
		gfx_clear_screen(0);
		*p_cursor = 0;
		sh_put_line(p_cursor, S_TITLE, 0x0A);
		sh_newline(p_cursor);
		return;
	}

	/* ===== edit (编辑文件) ===== */
	if (p[0]=='e' && p[1]=='d' && p[2]=='i' && p[3]=='t' && p[4]==' ') {
		char *fname = p + 5;
		if (*fname == '\0') {
			sh_put_line(p_cursor, TOUCH_NONAME, 0x0E);  sh_newline(p_cursor);
			return;
		}
		editor_run(p_cursor, fname);
		return;
	}

	/* ===== unknown command ===== */
	{
		int c = *p_cursor;
		const char *u = (const char *)S_UNKNOWN;
		while (*u) {
			if (c >= SH_MAX) {
				*p_cursor = c;
				sh_scroll_if_needed(p_cursor);
				c = *p_cursor;
			}
			sh_vm_write(c, *u, 0x04);
			c += SH_BPC;
			u++;
		}
		while (*p) {
			if (c >= SH_MAX) {
				*p_cursor = c;
				sh_scroll_if_needed(p_cursor);
				c = *p_cursor;
			}
			sh_vm_write(c, *p, 0x04);
			c += SH_BPC;
			p++;
		}
		*p_cursor = c;
		sh_update_hw_cursor(c);
		sh_newline(p_cursor);
	}
}


/*======================================================================*
                           shell_run (旧接口)
 *======================================================================*/
PUBLIC void shell_run(TTY* p_tty)
{
	char ch;
	while (p_tty->inbuf_count > 0) {
		ch = *(p_tty->p_inbuf_tail) & 0xFF;
		p_tty->p_inbuf_tail++;
		if (p_tty->p_inbuf_tail == p_tty->in_buf + TTY_IN_BYTES)
			p_tty->p_inbuf_tail = p_tty->in_buf;
		p_tty->inbuf_count--;
		if (ch == '\n' || ch == '\r') {
			cmdline_buf[cmdline_pos] = '\0';
			if (cmdline_pos > 0) {
				shell_parse_and_execute(cmdline_buf, NULL);
			}
			cmdline_pos = 0;
		} else if (ch == '\b') {
			if (cmdline_pos > 0) cmdline_pos--;
		} else {
			if (cmdline_pos < SHELL_CMD_MAX_LEN - 1)
				cmdline_buf[cmdline_pos++] = ch;
		}
	}
}
