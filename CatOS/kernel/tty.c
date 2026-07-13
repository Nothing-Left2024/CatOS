/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               tty.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    CatOS TTY

  完全自主实现的 Shell 输出系统:
  - 不依赖 disp_str / disp_color_str / clear_screen / vm_putc
  - 所有显存操作通过内联汇编完成 (编译器自动管理寄存器)
  - 光标由局部变量 int cursor 管理 (字节偏移, 0=左上角)
  - 换行: cursor = (cursor/160 + 1) * 160 (每行80字符×2字节)
  - 超出第25行自动滚屏 (rep movsw 快速块拷贝)
  - 回滚缓冲区: 200行历史, 支持 Shift+Up/Down 和 PageUp/PageDown 回看
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
#include "shell.h"
#include "proto.h"
#include "gfx.h"

#define TTY_FIRST	(tty_table)
#define TTY_END		(tty_table + NR_CONSOLES)

PRIVATE void	init_tty(TTY* p_tty);
PRIVATE void	tty_do_read(TTY* p_tty);
PRIVATE void	tty_do_write(TTY* p_tty);
PRIVATE void	put_key(TTY* p_tty, t_32 key);

/* 前向声明: 滚动条与回看辅助函数 */
static void draw_scrollbar(void);
static void exit_scroll_view(void);


/* =====================================================================
   内联汇编: 写单个字符到显存指定位置
   前提: GS 已设置为 Video 段选择器 (0x1B)
   ★ 统一调用 vm_putc, 便于后续图形化适配
===================================================================== */
static inline void vm_write(int pos, char ch, unsigned char color)
{
	vm_putc(pos, ch, color);
}

/* 读取显存字符部分 (统一封装) */
static inline char vm_read_char(int pos)
{
	return vm_getc(pos);
}

/* 读取显存属性部分 (统一封装) */
static inline unsigned char vm_read_attr(int pos)
{
	return vm_get_attr(pos);
}

/* =====================================================================
   更新 VGA 硬件光标位置 (闪烁下划线)
   pos: 字节偏移 (除以2得到字符偏移)
===================================================================== */
/* 屏幕常量 */
#define TTY_SCR_W	80
#define TTY_SCR_H	25
#define TTY_BPC		2

static inline void update_hw_cursor(int byte_pos)
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
#define TTY_ROW	(TTY_SCR_W * TTY_BPC)          /* = 160 */
#define TTY_MAX	(TTY_SCR_H * TTY_ROW)           /* = 4000 */

/* 动态屏幕参数辅助函数 (图形模式下使用 g_text_*, 文本模式使用 TTY_*) */
static inline int tty_bpc(void)    { return 2; }
static inline int tty_row_bytes(void) {
	return g_video_mode != 0 ? g_text_cols * 2 : TTY_ROW;
}
static inline int tty_max_bytes(void) {
	return g_video_mode != 0 ? g_text_cols * g_text_rows * 2 : TTY_MAX;
}
static inline int tty_scr_h(void) {
	return g_video_mode != 0 ? g_text_rows : TTY_SCR_H;
}

/* =====================================================================
   滚屏历史缓冲区: 存储滚出屏幕的行, 支持 Shift+Up/Down 回看
   每行160字节(80字符×2字节/字符), 最多保存 SCROLL_BACK_LINES 行
===================================================================== */
#define SCROLL_BACK_LINES  200   /* 回滚行数 */
#define SCROLL_BACK_SIZE   (SCROLL_BACK_LINES * TTY_ROW)  /* 200×160 = 32000字节 */

static char scroll_back_buf[SCROLL_BACK_SIZE];  /* 环形回滚缓冲区 */
static int  sb_write_pos = 0;    /* 下一条写入位置 (行号, 0 ~ SCROLL_BACK_LINES-1) */
static int  sb_count = 0;        /* 已存储的历史行数 */
static int  sb_view_offset = 0;  /* 当前回看偏移 (0=最新, >0=回看更早的行) */
static char sb_screen_saved[TTY_MAX]; /* 进入回看前保存当前屏幕内容 */
static int  sb_screen_is_saved = 0;   /* 屏幕是否已保存 */

/* 保存显存第0行到回滚缓冲区 (在 do_scroll_up 之前调用) */
static void sb_save_top_line(void)
{
	char *dst = scroll_back_buf + sb_write_pos * TTY_ROW;
	int i;
	for (i = 0; i < TTY_ROW; i++) {
		dst[i] = vm_read_char(i);
	}
	sb_write_pos = (sb_write_pos + 1) % SCROLL_BACK_LINES;
	if (sb_count < SCROLL_BACK_LINES) sb_count++;
}

/* =====================================================================
   软件滚屏: 将显存内容上移一行, 底行清空
   使用 rep movsw 进行快速块拷贝 (GS段内操作)
   同时保存被滚出屏幕的第0行到历史缓冲区
===================================================================== */
static void do_scroll_up(void)
{
	/* 保存即将滚出屏幕的第0行 */
	if (sb_view_offset == 0) {
		sb_save_top_line();
	}

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
			"movl %0, %%esi        \n\t"   /* ESI = 160 (源: 第1行) */
			"movl $1920, %%ecx     \n\t"   /* 24行×80字 = 1920字 */
			"rep movsw             \n\t"   /* 块拷贝 */
			"pop %%es              \n\t"
			"pop %%ds"
			:
			: "i"(TTY_ROW)
			: "eax", "esi", "edi", "ecx", "memory"
		);

		/* 清空第24行 (最后一行) */
		{
			int i;
			int base = (TTY_SCR_H - 1) * TTY_ROW;
			for (i = 0; i < TTY_SCR_W; i++) {
				vm_write(base + i * TTY_BPC, ' ', 0x07);
			}
		}
	}

	/* shell 模式不画滚动条 (滚动条仅在桌面终端中提供) */
}

/* =====================================================================
   回看滚屏: 将历史缓冲区中的行恢复到显存顶部
   lines > 0 表示向上回看 (查看更早的内容)
   lines < 0 表示向下回看 (查看更新的内容)
===================================================================== */
static void do_scroll_view(int lines)
{
	int new_offset = sb_view_offset + lines;

	/* 边界检查 */
	if (new_offset < 0) new_offset = 0;
	if (new_offset > sb_count) new_offset = sb_count;

	if (new_offset == sb_view_offset) return;  /* 无变化 */

	/* 进入回看模式: 保存当前屏幕 */
	if (sb_view_offset == 0 && new_offset > 0 && !sb_screen_is_saved) {
		int i;
		for (i = 0; i < TTY_MAX; i++) {
			sb_screen_saved[i] = vm_read_char(i);
		}
		sb_screen_is_saved = 1;
	}

	/* 退出回看模式: 恢复保存的屏幕 */
	if (new_offset == 0 && sb_view_offset > 0 && sb_screen_is_saved) {
		int i;
		for (i = 0; i < TTY_MAX; i += 2) {
			vm_write(i, sb_screen_saved[i], sb_screen_saved[i+1]);
		}
		sb_screen_is_saved = 0;
	}

	sb_view_offset = new_offset;

	/* 回看模式: 在屏幕顶部显示历史行 */
	if (sb_view_offset > 0) {
		/* 计算要显示的历史行范围 */
		int hist_start = sb_count - sb_view_offset;
		int row, col;

		for (row = 0; row < TTY_SCR_H && row < sb_view_offset; row++) {
			int hist_line = hist_start + row;
			if (hist_line < 0 || hist_line >= sb_count) continue;

			int buf_idx = (sb_write_pos - sb_count + hist_line + SCROLL_BACK_LINES * 2) % SCROLL_BACK_LINES;
			for (col = 0; col < TTY_SCR_W; col++) {
				int vpos = row * TTY_ROW + col * TTY_BPC;
				char b0 = scroll_back_buf[buf_idx * TTY_ROW + col * TTY_BPC];
				char b1 = scroll_back_buf[buf_idx * TTY_ROW + col * TTY_BPC + 1];
				vm_write(vpos, b0, b1);
			}
		}
	}
}

/*======================================================================
   侧边栏垂直滚动条: 有历史内容时始终显示
   sb_count>0 时在屏幕右侧绘制, 反映当前回看位置
======================================================================*/
static void draw_scrollbar(void)
{
	int scr_h = tty_scr_h();
	int cols  = (g_video_mode != 0) ? g_text_cols : TTY_SCR_W;
	int total, top_line, thumb_h, thumb_row;

	if (sb_count <= 0) return;

	total     = sb_count + scr_h;            /* 总内容行数 = 历史 + 可见 */
	top_line  = sb_count - sb_view_offset;   /* 当前查看窗口顶部行号 */
	thumb_h   = (scr_h * scr_h + total - 1) / total;  /* 滑块高度(行) */
	if (thumb_h < 1) thumb_h = 1;
	{
		int denom = total - scr_h;           /* 可滚动范围 */
		thumb_row = (denom > 0)
			? (top_line * (scr_h - thumb_h) / denom)
			: (scr_h - thumb_h);
	}
	if (thumb_row < 0) thumb_row = 0;
	if (thumb_row > scr_h - thumb_h) thumb_row = scr_h - thumb_h;

	if (g_video_mode != 0) {
		/* 图形模式: 在右侧 2 列字符位置画滚动条 */
		int bar_x = (cols - 2) * g_font_width;
		int bar_w = 2 * g_font_width;
		int bar_h = scr_h * g_font_height;
		fill_rect(bar_x, 0, bar_w, bar_h, 0x08);                /* 轨道: 深灰 */
		fill_rect(bar_x, thumb_row * g_font_height, bar_w,
		          thumb_h * g_font_height, 0x0F);               /* 滑块: 白 */
	} else {
		/* 文本模式: 在最后一列画字符 */
		int row;
		for (row = 0; row < scr_h; row++) {
			char ch = (row >= thumb_row && row < thumb_row + thumb_h)
				? 0xDB : 0xB1;  /* █=滑块, ▒=轨道 */
			int pos = row * TTY_ROW + (cols - 1) * TTY_BPC;
			vm_write(pos, ch, 0x07);
		}
	}
}

/* 退出回看模式: 恢复屏幕并清除滚动条 */
static void exit_scroll_view(void)
{
	if (sb_view_offset > 0) {
		do_scroll_view(-sb_view_offset);
	}
}

/* 全局命名字符串变量 (非const, 放入.data段, 避免.rodata寻址问题) */
static char S_TITLE[]      = "CatOS v0.14 Release 3";
static char S_EDITION_FP[] = " [Floppy Edition]";
static char S_EDITION_HD[] = " [Disk Edition]";
static char S_WELCOME[]    = "Type 'help' for commands.";
static char S_PROMPT[]     = "CatOS> ";

/* 动态提示符缓冲: "CatOS /path/ > " */
static char sh_prompt_buf[80];
static void build_prompt(void)
{
	const char *path;
	int i = 0;
	/* "CatOS " 前缀 */
	sh_prompt_buf[i++] = 'C';
	sh_prompt_buf[i++] = 'a';
	sh_prompt_buf[i++] = 't';
	sh_prompt_buf[i++] = 'O';
	sh_prompt_buf[i++] = 'S';
	sh_prompt_buf[i++] = ' ';
	/* 当前路径 */
	path = shell_get_cwd_path();
	while (*path && i < 70) {
		sh_prompt_buf[i++] = *path++;
	}
	/* "> " 后缀 */
	if (i < 78) sh_prompt_buf[i++] = '>';
	if (i < 79) sh_prompt_buf[i++] = ' ';
	sh_prompt_buf[i] = '\0';
}

/* =====================================================================
   清屏: 用空格填充全部字符单元
===================================================================== */
static void do_clear(void)
{
	if (g_video_mode != 0) {
		/* 图形模式: 调用统一清屏函数 */
		gfx_clear_screen(0);
	} else {
		/* 文本模式: 空格填充 */
		int i;
		for (i = 0; i < TTY_SCR_W * TTY_SCR_H; i++) {
			vm_write(i * TTY_BPC, ' ', 0x07);
		}
	}
}


/* =====================================================================
   检查光标是否超出屏幕底部, 如果超出则滚屏并将光标调整回屏幕内
===================================================================== */
static void scroll_if_needed(int *p_cursor)
{
	if (g_video_mode != 0) {
		/* 图形模式: 每行 g_text_cols 字符 × 2 字节 */
		int row_bytes = g_text_cols * 2;
		int max_bytes = g_text_cols * g_text_rows * 2;
		while (*p_cursor >= max_bytes) {
			do_scroll_up();
			*p_cursor -= row_bytes;
		}
	} else {
		/* 文本模式 */
		while (*p_cursor >= TTY_MAX) {
			do_scroll_up();
			*p_cursor -= TTY_ROW;
		}
	}
	if (*p_cursor < 0) *p_cursor = 0;
}

/* =====================================================================
   写字符串到当前光标位置, 自动更新光标 + 硬件光标
   处理 '\n' 换行: 移到下一行开头
   超出屏幕底部时自动滚屏
===================================================================== */
static void write_str(int *p_cursor, const char *s, unsigned char color)
{
	int c = *p_cursor;
	if (g_video_mode != 0) {
		int row_bytes = g_text_cols * 2;
		int max_bytes = g_text_cols * g_text_rows * 2;
		while (*s) {
			if (*s == '\n') {
				c = (c / row_bytes + 1) * row_bytes;
				if (c >= max_bytes) {
					*p_cursor = c;
					scroll_if_needed(p_cursor);
					c = *p_cursor;
				}
			} else {
				if (c >= max_bytes) {
					*p_cursor = c;
					scroll_if_needed(p_cursor);
					c = *p_cursor;
				}
				vm_write(c, *s, color);
				c += 2;
			}
			s++;
		}
	} else {
		while (*s) {
			if (*s == '\n') {
				c = (c / TTY_ROW + 1) * TTY_ROW;
				if (c >= TTY_MAX) {
					*p_cursor = c;
					scroll_if_needed(p_cursor);
					c = *p_cursor;
				}
			} else {
				if (c >= TTY_MAX) {
					*p_cursor = c;
					scroll_if_needed(p_cursor);
					c = *p_cursor;
				}
				vm_write(c, *s, color);
				c += TTY_BPC;
			}
			s++;
		}
	}
	*p_cursor = c;
	update_hw_cursor(c);				/* 同步硬件闪烁光标 */
}

/* 换行 + 更新硬件光标, 超出底部时自动滚屏 */
static void write_newline(int *p_cursor)
{
	if (g_video_mode != 0) {
		int row_bytes = g_text_cols * 2;
		int max_bytes = g_text_cols * g_text_rows * 2;
		int c = (*p_cursor / row_bytes + 1) * row_bytes;
		if (c >= max_bytes) {
			*p_cursor = c;
			scroll_if_needed(p_cursor);
		} else {
			*p_cursor = c;
		}
	} else {
		int c = (*p_cursor / TTY_ROW + 1) * TTY_ROW;
		if (c >= TTY_MAX) {
			*p_cursor = c;
			scroll_if_needed(p_cursor);
		} else {
			*p_cursor = c;
		}
	}
	update_hw_cursor(*p_cursor);					/* 同步硬件闪烁光标 */
}


/*======================================================================*
                           task_tty
 *======================================================================*/
PUBLIC void task_tty()
{
	/* 步骤1: 设置 GS = Video 段选择器 */
	__asm__ __volatile__("movw $0x1B, %%ax; movw %%ax, %%gs" : : : "eax");

	/* 步骤2: 初始化图形系统 */
	gfx_init();  /* 读取 loader 写入的图形参数 */

	/* 步骤2b: 清屏 */
	do_clear();

	/* 步骤3: 光标从左上角开始 */
	int cursor = 0;

	/* 步骤4: 显示标题 + 版本 + 版本类型 (第0行, 绿色) */
	{
		/* 读取启动驱动器号 (loader.asm 存储在物理地址 0x500) */
		t_8 boot_drive = *((volatile t_8*)0x500);
		write_str(&cursor, S_TITLE, 0x0A);
		/* 显示版本类型: 0x80=硬盘, 其他=软盘 */
		write_str(&cursor, (boot_drive & 0x80) ? S_EDITION_HD : S_EDITION_FP, 0x0B);
		write_newline(&cursor);
	}

	/* 步骤5: 初始化键盘 */
	init_keyboard();
	/* 步骤5b: 初始化 PS/2 鼠标 (IRQ 12 + 级联 IRQ 2) */
	init_mouse();

	/* 步骤6: 显示帮助提示 + 空一行 + 提示符 */
	{
		write_str(&cursor, S_WELCOME, 0x07);
		write_newline(&cursor);
		write_newline(&cursor);  /* 空一行 */
		build_prompt();
		write_str(&cursor, sh_prompt_buf, 0x0F);
	}

	/* 记录当前提示符起始位置 (退格不能越过此位置) */
	int prompt_start = cursor;
	update_hw_cursor(cursor);

	/* 步骤7: 主循环 */
	char cmdline[128];
	int pos = 0;          /* cmdline 中的字符数 */
	int cmd_cursor = 0;   /* 光标在 cmdline 中的位置 (0~pos) */

	while (1) {
		t_32 key = 0;
		keyboard_read_simple(&key);

		/* 每次迭代更新鼠标光标位置 (反色贴图) */
		mouse_draw_cursor();

		if (key == 0) continue;

		if (!(key & FLAG_EXT)) {
			/* 普通字符: 回显 + 存入cmdline */
		/* 如果处于回看模式, 先退出回看再输入 */
		exit_scroll_view();
			if (pos < 127) {
				char ch = (char)(key & 0xFF);
				if (cmd_cursor < pos) {
					/* 插入模式: 在光标处插入字符, 后面字符右移 */
					int i, j;
					for (i = pos; i > cmd_cursor; i--) {
						cmdline[i] = cmdline[i - 1];
					}
					cmdline[cmd_cursor] = ch;
					pos++;
					cmd_cursor++;
					/* 重绘从插入点到行尾 */
					{
						int disp = prompt_start + (cmd_cursor - 1) * TTY_BPC;
						for (j = cmd_cursor - 1; j < pos; j++) {
							if (disp >= tty_max_bytes()) {
								scroll_if_needed(&cursor);
								prompt_start -= tty_row_bytes();
								disp -= tty_row_bytes();
							}
							vm_write(disp, cmdline[j], 0x0F);
							disp += TTY_BPC;
						}
						cursor = prompt_start + cmd_cursor * TTY_BPC;
					}
				} else {
					/* 追加模式: 在末尾添加字符 */
					if (cursor >= tty_max_bytes()) {
						scroll_if_needed(&cursor);
						prompt_start -= tty_row_bytes();
					}
					vm_write(cursor, ch, 0x0F);
					cursor += TTY_BPC;
					cmdline[cmd_cursor] = ch;
					pos++;
					cmd_cursor++;
				}
				update_hw_cursor(cursor);
			}
		} else {
			int raw = key & MASK_RAW;

			if (raw == ENTER) {
				/* 回车: 换行 → 执行命令 → 新提示符 */
			/* 如果处于回看模式, 先退出回看 */
			exit_scroll_view();
				cmdline[pos] = '\0';
			/* 光标移到行尾再换行 */
			cursor = prompt_start + pos * TTY_BPC;
			write_newline(&cursor);

			if (pos > 0) {
				/* 隐藏鼠标光标, 防止 shell 输出残留 */
				mouse_hide_cursor();
				shell_parse_and_execute(cmdline, &cursor);
				/* 执行完命令后重新显示鼠标 */
				mouse_draw_cursor();
			}

				/* 新提示符 — 动态生成 "CatOS /path/ > " */
			{
				const char *p;
				build_prompt();
				p = sh_prompt_buf;
				while (*p) {
					if (cursor >= tty_max_bytes()) {
						scroll_if_needed(&cursor);
					}
					vm_write(cursor, *p, 0x0F);
					cursor += TTY_BPC;
					p++;
				}
			}
				prompt_start = cursor;
				update_hw_cursor(cursor);
				pos = 0;
				cmd_cursor = 0;

			} else if (raw == BACKSPACE) {
			exit_scroll_view();
				if (cmd_cursor > 0) {
					int i, j;
					/* 删除光标前一个字符, 后面字符左移 */
					for (i = cmd_cursor - 1; i < pos - 1; i++) {
						cmdline[i] = cmdline[i + 1];
					}
					pos--;
					cmd_cursor--;
					/* 重绘从光标到行尾 */
					{
						int disp = prompt_start + cmd_cursor * TTY_BPC;
						for (j = cmd_cursor; j < pos; j++) {
							vm_write(disp, cmdline[j], 0x0F);
							disp += TTY_BPC;
						}
						/* 清除末尾残留字符 */
						vm_write(disp, ' ', 0x0F);
						cursor = prompt_start + cmd_cursor * TTY_BPC;
					}
					update_hw_cursor(cursor);
				}

			} else if (raw == LEFT) {
			exit_scroll_view();
				if (cmd_cursor > 0) {
					cmd_cursor--;
					cursor = prompt_start + cmd_cursor * TTY_BPC;
					update_hw_cursor(cursor);
				}

			} else if (raw == RIGHT) {
			exit_scroll_view();
				if (cmd_cursor < pos) {
					cmd_cursor++;
					cursor = prompt_start + cmd_cursor * TTY_BPC;
					update_hw_cursor(cursor);
				}

			} else if (raw == UP) {
				/* Shift+Up: 向上回看1行 */
				if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R)) {
					do_scroll_view(1);
				}

			} else if (raw == DOWN) {
				/* Shift+Down: 向下回看1行 */
				if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R)) {
					do_scroll_view(-1);
				}

			} else if (raw == PAGEUP) {
				/* PageUp: 向上回看半屏 */
				do_scroll_view(tty_scr_h() / 2);

			} else if (raw == PAGEDOWN) {
				/* PageDown: 向下回看半屏 */
				do_scroll_view(-(tty_scr_h() / 2));
			}
		}
	}
}


/*======================================================================*
                      init_tty / in_process / put_key / etc.
 *======================================================================*/
PRIVATE void init_tty(TTY* p_tty)
{
	p_tty->inbuf_count = 0;
	p_tty->p_inbuf_head = p_tty->p_inbuf_tail = p_tty->in_buf;
	init_screen(p_tty);
}

PUBLIC void in_process(TTY* p_tty, t_32 key)
{
	if (!(key & FLAG_EXT)) {
		put_key(p_tty, key);
	} else {
		int raw_code = key & MASK_RAW;
		switch(raw_code) {
		case ENTER:
			put_key(p_tty, '\n');
			break;
		case BACKSPACE:
			put_key(p_tty, '\b');
			break;
		case UP:
			if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R))
				scroll_screen(p_tty->p_console, SCROLL_SCREEN_UP);
			break;
		case DOWN:
			if ((key & FLAG_SHIFT_L) || (key & FLAG_SHIFT_R))
				scroll_screen(p_tty->p_console, SCROLL_SCREEN_DOWN);
			break;
		case F1: case F2: case F3: case F4: case F5:
		case F6: case F7: case F8: case F9: case F10:
		case F11: case F12:
			if ((key & FLAG_ALT_L) || (key & FLAG_ALT_R))
				select_console(raw_code - F1);
			break;
		default: break;
		}
	}
}

PRIVATE void put_key(TTY* p_tty, t_32 key)
{
	if (p_tty->inbuf_count < TTY_IN_BYTES) {
		*(p_tty->p_inbuf_head) = key;
		p_tty->p_inbuf_head++;
		if (p_tty->p_inbuf_head == p_tty->in_buf + TTY_IN_BYTES)
			p_tty->p_inbuf_head = p_tty->in_buf;
		p_tty->inbuf_count++;
	}
}

PRIVATE void tty_do_read(TTY* p_tty)
{
	if (is_current_console(p_tty->p_console))
		keyboard_read(p_tty);
}

PRIVATE void tty_do_write(TTY* p_tty)
{
	if (p_tty->inbuf_count) {
		char ch = *(p_tty->p_inbuf_tail);
		p_tty->p_inbuf_tail++;
		if (p_tty->p_inbuf_tail == p_tty->in_buf + TTY_IN_BYTES)
			p_tty->p_inbuf_tail = p_tty->in_buf;
		p_tty->inbuf_count--;
		out_char(p_tty->p_console, ch);
	}
}

PUBLIC void tty_write(TTY* p_tty, char* buf, int len)
{ while (len--) out_char(p_tty->p_console, *buf++); }

PUBLIC int sys_write(char* buf, int len, PROCESS* p_proc)
{
	int i;
	char* str = (char*)(USER_PROC_BASE + (t_32)buf);

	/* 统一通过 user_put_char 输出, 使用 g_user_attr 默认属性
	 * (图形模式用 vm_putc, 文本模式用 out_char_color) */
	for (i = 0; i < len; i++) {
		user_put_char(str[i], g_user_attr);
	}
	return len;
}
