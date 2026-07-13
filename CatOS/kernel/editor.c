/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               editor.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS 文本编辑器

  功能:
    打开文件编辑, 支持插入/删除/光标移动
    Ctrl-S 保存, Ctrl-W 另存为, Ctrl-X 退出

  操作:
    方向键     移动光标
    Home/End   行首/行尾
    Backspace  删除光标前字符
    Delete     删除光标处字符
    Enter      插入换行
    Ctrl-S     保存文件
    Ctrl-W     另存为 (输入新文件名)
    Ctrl-X     退出 (如有修改则提示保存)

  编码约定 (与 shell.c / taskmgr.c 一致):
    - 所有字符串使用全局命名字符串数组 (放入 .data 段)
    - 内联汇编使用 32 位寄存器
    - update_hw_cursor 使用 "b" 约束固定 EBX
    - 不使用 hlt (ring 1 会 #GP)
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
#include "editor.h"
#include "fat.h"
#include "hd.h"
#include "proto.h"
#include "gfx.h"
#include "shell.h"


/*======================================================================
  屏幕常量 (ED_SCR_W 使用动态 g_text_cols, 适配高分辨率)
======================================================================*/
#define ED_SCR_W      g_text_cols
#define ED_SCR_H      25
#define ED_BPC        2
#define ED_ROW        (ED_SCR_W * ED_BPC)
#define ED_MAX        (ED_SCR_H * ED_ROW)

/* 界面垂直居中偏移 (清屏用全屏, 绘制用25行居中) */
static int ed_row_offset = 0;

/* 动态行布局 (基于 g_text_rows) */
static int ed_num_rows;
static int ed_row_sep;
static int ed_row_status;

/* 编辑器布局 */
#define ED_ROW_TITLE  0
#define ED_ROW_BODY0  1              /* 正文起始行 */

/* 编辑缓冲区大小 */
#define ED_BUF_SIZE   8192

/* 编辑器状态 */
#define ED_STATE_EDIT   0   /* 正常编辑 */
#define ED_STATE_SAVEAS 1   /* 另存为输入 */
#define ED_STATE_QUIT   2   /* 退出确认 */

/*======================================================================
  颜色定义
======================================================================*/
#define COL_TITLE      0x1F   /* 白字蓝底 — 标题栏 */
#define COL_BODY       0x07   /* 白字黑底 — 正文 */
#define COL_SEP        0x08   /* 灰色 — 分隔线 */
#define COL_STATUS     0x17   /* 白字蓝底 — 状态栏 */
#define COL_MODIFIED   0x0C   /* 红色 — 修改标记 */
#define COL_SAVEAS     0x0E   /* 黄色 — 另存为提示 */
#define COL_ERROR      0x0C   /* 红色 — 错误 */

/*======================================================================
  全局命名字符串变量 (放入 .data 段)
======================================================================*/
static char S_TITLE[]       = " CatOS Editor - ";
static char S_MODIFIED[]    = " [Modified]";
static char S_SEP[]         = "--------------------------------------------------------------------------------";
static char S_STATUS[]      = " [Ctrl-S]Save [Ctrl-W]SaveAs [Ctrl-X]Exit | Ln ";
static char S_COMMA[]       = ", Col ";
static char S_SAVEAS[]      = " Save as: ";
static char S_QUIT_ASK[]    = " Save before exit? (y=save n=discard esc=cancel)";
static char S_SAVED[]       = " Saved. ";
static char S_SAVE_ERR[]    = " Save failed! ";
static char S_NEW_FILE[]    = " (New File)";
static char S_NO_FS[]       = " No filesystem! ";


/*======================================================================
  编辑器状态变量 (BSS 段, 启动时被清零)
======================================================================*/
PRIVATE char  ed_buf[ED_BUF_SIZE];     /* 文本缓冲区 */
PRIVATE int   ed_len;                  /* 当前文本长度 */
PRIVATE int   ed_cursor;               /* 光标位置 (0~ed_len) */
PRIVATE int   ed_scroll_line;          /* 第一可见行号 (0-based) */
PRIVATE int   ed_left_col;             /* 水平滚动偏移 */
PRIVATE int   ed_modified;             /* 修改标记 */
PRIVATE char  ed_filename[64];         /* 当前文件名 */
PRIVATE int   ed_state;                /* 编辑器状态 */
PRIVATE char  ed_saveas_buf[64];       /* 另存为输入缓冲 */
PRIVATE int   ed_saveas_len;           /* 另存为输入长度 */
PRIVATE char  ed_status_msg[64];       /* 状态栏消息 */
PRIVATE int   ed_status_color;         /* 状态栏消息颜色 */


/*======================================================================
  写单个字符到 VGA 显存 (统一封装, 支持垂直居中偏移)
======================================================================*/
static inline void ed_vm_write(int pos, char ch, unsigned char color)
{
	/* pos 是基于 80x25 虚拟屏幕的位置, 转换为实际屏幕位置 */
	int row = pos / (ED_SCR_W * ED_BPC);
	int col = (pos % (ED_SCR_W * ED_BPC)) / ED_BPC;
	int real_pos = ((row + ed_row_offset) * ED_SCR_W + col) * ED_BPC;
	vm_putc(real_pos, ch, color);
}

/*======================================================================
  更新 VGA 硬件光标位置
======================================================================*/
static inline void ed_update_hw_cursor(int byte_pos)
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
static void ed_clear_screen(void)
{
	int i;
	int total = ED_SCR_W * g_text_rows;
	for (i = 0; i < total; i++) {
		vm_putc(i * ED_BPC, ' ', COL_BODY);
	}
	/* 全屏显示 (不居中) */
	ed_row_offset = 0;
	/* 计算动态行布局: 底部2行固定 (分隔线 + 状态栏) */
	ed_row_status = g_text_rows - 1;
	ed_row_sep = g_text_rows - 2;
	ed_num_rows = ed_row_sep - ED_ROW_BODY0;
}

/*======================================================================
  在指定行列写入字符串 (不越界)
======================================================================*/
static void ed_write_str(int row, int col, const char *s, unsigned char color)
{
	int pos = (row * ED_SCR_W + col) * ED_BPC;
	while (*s && col < ED_SCR_W) {
		ed_vm_write(pos, *s, color);
		pos += ED_BPC;
		col++;
		s++;
	}
}

/*======================================================================
  在指定行列写入字符串, 最多 maxcol 列
======================================================================*/
static void ed_write_str_len(int row, int col, const char *s, int len, unsigned char color)
{
	int pos = (row * ED_SCR_W + col) * ED_BPC;
	while (len > 0 && col < ED_SCR_W && *s) {
		ed_vm_write(pos, *s, color);
		pos += ED_BPC;
		col++;
		len--;
		s++;
	}
}

/*======================================================================
  用空格填充到行尾
======================================================================*/
static void ed_fill_row(int row, int col, unsigned char color)
{
	int pos = (row * ED_SCR_W + col) * ED_BPC;
	while (col < ED_SCR_W) {
		ed_vm_write(pos, ' ', color);
		pos += ED_BPC;
		col++;
	}
}

/*======================================================================
  输出整数到指定位置
======================================================================*/
static void ed_write_int(int row, int col, int value, unsigned char color)
{
	char digits[12];
	int i = 0;
	if (value < 0) { value = -value; }
	if (value == 0) { digits[i++] = '0'; }
	else {
		while (value > 0 && i < 11) {
			digits[i++] = '0' + (value % 10);
			value /= 10;
		}
	}
	/* 从后往前写 */
	{
		int pos = (row * ED_SCR_W + col) * ED_BPC;
		int c = col;
		while (--i >= 0 && c < ED_SCR_W) {
			ed_vm_write(pos, digits[i], color);
			pos += ED_BPC;
			c++;
		}
	}
}


/*======================================================================
  行号 <-> 缓冲区偏移 转换
======================================================================*/

/* 返回第 line 行起始处的缓冲区偏移 (line 从 0 开始) */
static int line_to_offset(int line)
{
	int l = 0, i;
	if (line <= 0) return 0;
	for (i = 0; i < ed_len; i++) {
		if (ed_buf[i] == '\n') {
			l++;
			if (l == line) return i + 1;
		}
	}
	return ed_len;
}

/* 返回光标所在行号和列号 */
static void cursor_to_linecol(int *line, int *col)
{
	int l = 0, i;
	for (i = 0; i < ed_cursor; i++) {
		if (ed_buf[i] == '\n') l++;
	}
	*line = l;
	/* 找行首 */
	{
		int ls = ed_cursor;
		while (ls > 0 && ed_buf[ls - 1] != '\n') ls--;
		*col = ed_cursor - ls;
	}
}

/* 返回指定行的长度 (不含 '\n') */
static int line_length(int line)
{
	int off = line_to_offset(line);
	int len = 0;
	while (off + len < ed_len && ed_buf[off + len] != '\n') len++;
	return len;
}


/*======================================================================
  确保光标在可见区域内, 必要时调整滚动
======================================================================*/
static void ensure_cursor_visible(void)
{
	int cur_line, cur_col;
	cursor_to_linecol(&cur_line, &cur_col);

	/* 垂直滚动 */
	if (cur_line < ed_scroll_line) {
		ed_scroll_line = cur_line;
	} else if (cur_line >= ed_scroll_line + ed_num_rows) {
		ed_scroll_line = cur_line - ed_num_rows + 1;
	}

	/* 水平滚动 */
	if (cur_col < ed_left_col) {
		ed_left_col = cur_col;
	} else if (cur_col >= ed_left_col + ED_SCR_W) {
		ed_left_col = cur_col - ED_SCR_W + 1;
	}
}


/*======================================================================
  绘制标题栏 (行 0)
======================================================================*/
static void draw_title(void)
{
	int col = 0;
	int pos = 0;

	/* 蓝底 */
	ed_fill_row(ED_ROW_TITLE, 0, COL_TITLE);

	/* " CatOS Editor - " */
	ed_write_str(ED_ROW_TITLE, 0, S_TITLE, COL_TITLE);
	col = 0;
	while (S_TITLE[col]) col++;

	/* 文件名 */
	ed_write_str(ED_ROW_TITLE, col, ed_filename, COL_TITLE);
	while (ed_filename[col - (sizeof(S_TITLE)-1)] && col < ED_SCR_W) col++;
	{
		int fn_len = 0;
		while (ed_filename[fn_len]) fn_len++;
		col = (sizeof(S_TITLE) - 1) + fn_len;
	}

	/* 修改标记 */
	if (ed_modified) {
		ed_write_str(ED_ROW_TITLE, col, S_MODIFIED, COL_MODIFIED);
	}
}


/*======================================================================
  绘制正文区域 (行 1~22)
======================================================================*/
static void draw_body(void)
{
	int vis_line;
	int buf_off = line_to_offset(ed_scroll_line);

	for (vis_line = 0; vis_line < ed_num_rows; vis_line++) {
		int row = ED_ROW_BODY0 + vis_line;
		int col = 0;
		int pos = row * ED_SCR_W * ED_BPC;

		/* 跳过水平滚动偏移的字符 */
		int skip = ed_left_col;
		int line_col = 0;  /* 当前行内字符位置 */

		while (buf_off < ed_len && ed_buf[buf_off] != '\n') {
			if (line_col >= ed_left_col && col < ED_SCR_W) {
				char ch = ed_buf[buf_off];
				if (ch == '\t') {
					/* Tab 显示为 4 个空格 */
					int tab_stop = (line_col + 4) & ~3;
					while (line_col < tab_stop && col < ED_SCR_W) {
						ed_vm_write(pos, ' ', COL_BODY);
						pos += ED_BPC;
						col++;
						line_col++;
					}
					buf_off++;
					continue;
				} else {
					ed_vm_write(pos, ch, COL_BODY);
					pos += ED_BPC;
					col++;
				}
			}
			line_col++;
			buf_off++;
		}

		/* 跳过换行符 */
		if (buf_off < ed_len && ed_buf[buf_off] == '\n') {
			buf_off++;
		}

		/* 填充行尾空格 */
		while (col < ED_SCR_W) {
			ed_vm_write(pos, ' ', COL_BODY);
			pos += ED_BPC;
			col++;
		}
	}
}


/*======================================================================
  绘制状态栏 (行 24)
======================================================================*/
static void draw_status(void)
{
	int col;

	/* 蓝底 */
	ed_fill_row(ed_row_status, 0, COL_STATUS);

	if (ed_state == ED_STATE_SAVEAS) {
		/* 另存为模式 */
		ed_write_str(ed_row_status, 0, S_SAVEAS, COL_SAVEAS);
		col = sizeof(S_SAVEAS) - 1;
		ed_write_str_len(ed_row_status, col, ed_saveas_buf, ed_saveas_len, 0x07);
		col += ed_saveas_len;
		/* 光标闪烁位置 */
		if (col < ED_SCR_W) {
			ed_vm_write((ed_row_status * ED_SCR_W + col) * ED_BPC, '_', 0x07);
		}
		return;
	}

	if (ed_state == ED_STATE_QUIT) {
		ed_write_str(ed_row_status, 0, S_QUIT_ASK, COL_SAVEAS);
		return;
	}

	/* 正常状态: 显示快捷键 + 行列号 */
	{
		int cur_line, cur_col;
		cursor_to_linecol(&cur_line, &cur_col);

		ed_write_str(ed_row_status, 0, S_STATUS, COL_STATUS);
		col = 0;
		while (S_STATUS[col]) col++;

		ed_write_int(ed_row_status, col, cur_line + 1, COL_STATUS);
		{
			/* 计算行号的字符宽度 */
			int tmp = cur_line + 1, w = 0;
			if (tmp == 0) w = 1;
			else while (tmp > 0) { w++; tmp /= 10; }
			col += w;
		}

		ed_write_str(ed_row_status, col, S_COMMA, COL_STATUS);
		col += sizeof(S_COMMA) - 1;

		ed_write_int(ed_row_status, col, cur_col + 1, COL_STATUS);
		{
			int tmp = cur_col + 1, w = 0;
			if (tmp == 0) w = 1;
			else while (tmp > 0) { w++; tmp /= 10; }
			col += w;
		}

		/* 状态消息 */
		if (ed_status_msg[0]) {
			ed_write_str(ed_row_status, ED_SCR_W - 20, ed_status_msg, ed_status_color);
		}
	}
}


/*======================================================================
  绘制分隔线 (行 23)
======================================================================*/
static void draw_sep(void)
{
	int col;
	for (col = 0; col < ED_SCR_W; col++) {
		ed_vm_write((ed_row_sep * ED_SCR_W + col) * ED_BPC, '-', COL_SEP);
	}
}


/*======================================================================
  全部重绘
======================================================================*/
static void draw_all(void)
{
	draw_title();
	draw_body();
	draw_sep();
	draw_status();

	/* 更新硬件光标 */
	{
		int cur_line, cur_col;
		cursor_to_linecol(&cur_line, &cur_col);
		int screen_row = ED_ROW_BODY0 + (cur_line - ed_scroll_line);
		int screen_col = cur_col - ed_left_col;

		if (screen_row >= ED_ROW_BODY0 && screen_row < ED_ROW_BODY0 + ed_num_rows &&
		    screen_col >= 0 && screen_col < ED_SCR_W) {
			ed_update_hw_cursor((screen_row * ED_SCR_W + screen_col) * ED_BPC);
		}
	}
}


/*======================================================================
  加载文件到编辑缓冲区
======================================================================*/
static int load_file(void)
{
	int n;

	ed_len = 0;
	ed_buf[0] = '\0';

	if (fat_init() != 0) {
		ed_status_msg[0] = '\0';
		return -1;
	}

	n = fat_read_file_in(shell_get_cwd(), ed_filename, ed_buf, ED_BUF_SIZE - 1);
	if (n < 0) {
		/* 文件不存在, 创建新文件 */
		ed_len = 0;
		ed_buf[0] = '\0';
		return 1;  /* 新文件 */
	}
	ed_len = n;
	ed_buf[ed_len] = '\0';
	return 0;
}


/*======================================================================
  保存文件
======================================================================*/
static int save_file(const char *filename)
{
	if (fat_init() != 0) {
		return -1;
	}
	return fat_write_file_in(shell_get_cwd(), filename, ed_buf, ed_len);
}


/*======================================================================
  光标操作
======================================================================*/

/* 向左移动 */
static void cursor_left(void)
{
	if (ed_cursor > 0) ed_cursor--;
}

/* 向右移动 */
static void cursor_right(void)
{
	if (ed_cursor < ed_len) ed_cursor++;
}

/* 向上移动 */
static void cursor_up(void)
{
	int cur_line, cur_col;
	cursor_to_linecol(&cur_line, &cur_col);
	if (cur_line == 0) { ed_cursor = 0; return; }

	/* 移到上一行的相同列 */
	{
		int prev_len = line_length(cur_line - 1);
		int target_col = (cur_col > prev_len) ? prev_len : cur_col;
		ed_cursor = line_to_offset(cur_line - 1) + target_col;
	}
}

/* 向下移动 */
static void cursor_down(void)
{
	int cur_line, cur_col;
	int total_lines = 0, i;
	cursor_to_linecol(&cur_line, &cur_col);

	/* 计算总行数 */
	total_lines = 0;
	for (i = 0; i < ed_len; i++) {
		if (ed_buf[i] == '\n') total_lines++;
	}
	if (ed_len > 0 && ed_buf[ed_len - 1] != '\n') total_lines++;

	if (cur_line >= total_lines - 1) {
		ed_cursor = ed_len;
		return;
	}

	{
		int next_len = line_length(cur_line + 1);
		int target_col = (cur_col > next_len) ? next_len : cur_col;
		ed_cursor = line_to_offset(cur_line + 1) + target_col;
	}
}

/* 移到行首 */
static void cursor_home(void)
{
	while (ed_cursor > 0 && ed_buf[ed_cursor - 1] != '\n') ed_cursor--;
}

/* 移到行尾 */
static void cursor_end(void)
{
	while (ed_cursor < ed_len && ed_buf[ed_cursor] != '\n') ed_cursor++;
}


/*======================================================================
  插入/删除操作
======================================================================*/

/* 在光标处插入字符 */
static int insert_char(char ch)
{
	int i;
	if (ed_len >= ED_BUF_SIZE - 1) return -1;  /* 缓冲区满 */

	/* 后移 */
	for (i = ed_len; i > ed_cursor; i--) {
		ed_buf[i] = ed_buf[i - 1];
	}
	ed_buf[ed_cursor] = ch;
	ed_cursor++;
	ed_len++;
	ed_buf[ed_len] = '\0';
	ed_modified = 1;
	return 0;
}

/* 删除光标前一个字符 */
static void delete_before(void)
{
	int i;
	if (ed_cursor <= 0) return;
	/* 前移 */
	for (i = ed_cursor; i < ed_len; i++) {
		ed_buf[i - 1] = ed_buf[i];
	}
	ed_cursor--;
	ed_len--;
	ed_buf[ed_len] = '\0';
	ed_modified = 1;
}

/* 删除光标处字符 */
static void delete_after(void)
{
	int i;
	if (ed_cursor >= ed_len) return;
	for (i = ed_cursor; i < ed_len - 1; i++) {
		ed_buf[i] = ed_buf[i + 1];
	}
	ed_len--;
	ed_buf[ed_len] = '\0';
	ed_modified = 1;
}


/*======================================================================
  设置状态栏消息
======================================================================*/
static void set_status(const char *msg, int color)
{
	int i;
	for (i = 0; i < 63 && msg[i]; i++) ed_status_msg[i] = msg[i];
	ed_status_msg[i] = '\0';
	ed_status_color = color;
}


/*======================================================================
  主入口: editor_run
======================================================================*/
PUBLIC void editor_run(int *p_cursor, const char *filename)
{
	int i;
	int need_redraw;
	t_32 key;
	int load_result;

	/* 初始化编辑器状态 */
	ed_len = 0;
	ed_cursor = 0;
	ed_scroll_line = 0;
	ed_left_col = 0;
	ed_modified = 0;
	ed_state = ED_STATE_EDIT;
	ed_saveas_len = 0;
	ed_saveas_buf[0] = '\0';
	ed_status_msg[0] = '\0';
	ed_status_color = COL_STATUS;

	/* 拷贝文件名 */
	for (i = 0; i < 63 && filename[i]; i++) ed_filename[i] = filename[i];
	ed_filename[i] = '\0';

	/* 加载文件 */
	load_result = load_file();
	if (load_result < 0) {
		set_status(S_NO_FS, COL_ERROR);
	} else if (load_result > 0) {
		set_status(S_NEW_FILE, COL_SAVEAS);
	}

	/* 清屏并绘制初始界面 */
	ed_clear_screen();
	draw_all();

	/* 主循环 */
	while (1) {
		key = 0;
		keyboard_read_simple(&key);
		need_redraw = 0;

		if (key == 0) continue;

		/* ===== 另存为模式 ===== */
		if (ed_state == ED_STATE_SAVEAS) {
			if (key == ESC) {
				ed_state = ED_STATE_EDIT;
				ed_status_msg[0] = '\0';
				need_redraw = 1;
			} else if (key == ENTER) {
				/* 确认另存为 */
				ed_saveas_buf[ed_saveas_len] = '\0';
				if (ed_saveas_len > 0) {
					/* 更新文件名 */
					for (i = 0; i < 63 && ed_saveas_buf[i]; i++)
						ed_filename[i] = ed_saveas_buf[i];
					ed_filename[i] = '\0';

					if (save_file(ed_filename) == 0) {
						ed_modified = 0;
						set_status(S_SAVED, 0x0A);
					} else {
						set_status(S_SAVE_ERR, COL_ERROR);
					}
				}
				ed_state = ED_STATE_EDIT;
				need_redraw = 1;
			} else if (key == BACKSPACE) {
				if (ed_saveas_len > 0) {
					ed_saveas_len--;
					ed_saveas_buf[ed_saveas_len] = '\0';
				}
				need_redraw = 1;
			} else if (!(key & FLAG_EXT) && !(key & (FLAG_CTRL_L | FLAG_CTRL_R))) {
				char ch = key & 0xFF;
				if (ch >= ' ' && ch <= '~' && ed_saveas_len < 63) {
					ed_saveas_buf[ed_saveas_len++] = ch;
					ed_saveas_buf[ed_saveas_len] = '\0';
					need_redraw = 1;
				}
			}
		}
		/* ===== 退出确认模式 ===== */
		else if (ed_state == ED_STATE_QUIT) {
			if (key == ESC) {
				ed_state = ED_STATE_EDIT;
				ed_status_msg[0] = '\0';
				need_redraw = 1;
			} else if (!(key & FLAG_EXT) && !(key & (FLAG_CTRL_L | FLAG_CTRL_R))) {
				char ch = key & 0xFF;
				if (ch == 'y' || ch == 'Y') {
					/* 保存后退出 */
					if (save_file(ed_filename) == 0) {
						break;  /* 退出编辑器 */
					} else {
						set_status(S_SAVE_ERR, COL_ERROR);
						ed_state = ED_STATE_EDIT;
						need_redraw = 1;
					}
				} else if (ch == 'n' || ch == 'N') {
					break;  /* 不保存退出 */
				}
			}
		}
		/* ===== 正常编辑模式 ===== */
		else {
			/* Ctrl 组合键 */
			if (!(key & FLAG_EXT) && (key & (FLAG_CTRL_L | FLAG_CTRL_R))) {
				char ch = key & 0xFF;
				if (ch == 's' || ch == 'S') {
					/* Ctrl-S: 保存 */
					if (save_file(ed_filename) == 0) {
						ed_modified = 0;
						set_status(S_SAVED, 0x0A);
					} else {
						set_status(S_SAVE_ERR, COL_ERROR);
					}
					need_redraw = 1;
				} else if (ch == 'w' || ch == 'W') {
					/* Ctrl-W: 另存为 */
					ed_state = ED_STATE_SAVEAS;
					ed_saveas_len = 0;
					ed_saveas_buf[0] = '\0';
					need_redraw = 1;
				} else if (ch == 'x' || ch == 'X') {
					/* Ctrl-X: 退出 */
					if (ed_modified) {
						ed_state = ED_STATE_QUIT;
						need_redraw = 1;
					} else {
						break;  /* 未修改, 直接退出 */
					}
				}
			}
			/* 方向键和特殊键 */
			else if (key & FLAG_EXT) {
				int raw = key & MASK_RAW;
				if (raw == (UP & MASK_RAW))    { cursor_up();      need_redraw = 1; }
				if (raw == (DOWN & MASK_RAW))   { cursor_down();   need_redraw = 1; }
				if (raw == (LEFT & MASK_RAW))   { cursor_left();   need_redraw = 1; }
				if (raw == (RIGHT & MASK_RAW))  { cursor_right();  need_redraw = 1; }
				if (raw == (HOME & MASK_RAW))   { cursor_home();   need_redraw = 1; }
				if (raw == (END & MASK_RAW))    { cursor_end();    need_redraw = 1; }
				if (raw == (DELETE & MASK_RAW)) { delete_after();  need_redraw = 1; }
				if (raw == (BACKSPACE & MASK_RAW)) { delete_before(); need_redraw = 1; }
				if (raw == (ENTER & MASK_RAW))  {
					insert_char('\n');
					need_redraw = 1;
				}
			}
			/* 普通字符 */
			else if (!(key & (FLAG_CTRL_L | FLAG_CTRL_R))) {
				char ch = key & 0xFF;
				if (ch == BACKSPACE) {
					delete_before();
					need_redraw = 1;
				} else if (ch == ENTER) {
					insert_char('\n');
					need_redraw = 1;
				} else if (ch >= ' ' && ch <= '~') {
					insert_char(ch);
					need_redraw = 1;
				}
			}
		}

		if (need_redraw) {
			ensure_cursor_visible();
			draw_all();
		}
	}

	/* 退出编辑器: 清屏, 光标归零 */
	ed_clear_screen();
	*p_cursor = 0;
	ed_update_hw_cursor(0);
}
