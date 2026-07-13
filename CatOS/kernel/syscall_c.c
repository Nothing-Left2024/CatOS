
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               syscall.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS 系统调用实现 (扩展)
  - user_put_char : 统一字符输出 (按属性), 供 putc/puts/write/putc_color 共用
  - sys_putc/puts: 输出字符/字符串 (使用 g_user_attr 默认属性)
  - sys_getc     : 非阻塞读键盘
  - sys_exit     : 终止当前用户进程
  - sys_clrscr   : 清屏
  - sys_gotoxy   : 移动光标
  - sys_get_xy   : 获取光标坐标 (x | y<<16)
  - sys_get_cols/rows/vmode : 屏幕信息
  - sys_set_color/putc_color: 彩色输出
  - sys_get_pid/get_name    : 进程信息
  - sys_rand/srand           : 伪随机数
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
#include "gfx.h"
#include "fat.h"
#include "wm.h"

/* 伪随机数状态 (LCG), 仅本文件可见; 默认种子=1 (标准 C 行为) */
PRIVATE	t_32	g_user_rand_seed = 1;

/* 辅助: 获取当前进程的用户槽位 (0..3), 非用户进程返回 -1 */
PRIVATE int current_user_slot(PROCESS* p_proc)
{
	int idx = p_proc - proc_table;
	if (idx < NR_TASKS + NR_PROCS) return -1;
	if (idx >= NR_TASKS + NR_PROCS + NR_USER_PROCS) return -1;
	return idx - (NR_TASKS + NR_PROCS);
}

/* 辅助: 判断当前进程是否应该使用控制台窗口 (图形桌面模式 + 用户进程 + 控制台激活) */
PRIVATE int use_user_console(PROCESS* p_proc)
{
	int slot;
	if (g_video_mode == 0) return 0;  /* 文本模式: 直接写屏幕 */
	slot = current_user_slot(p_proc);
	if (slot < 0) return 0;
	return (wm_uc_get_win_idx(slot) >= 0);
}

/*======================================================================*
                           user_put_char
 *======================================================================*/
/* 统一字符输出: 图形模式用 vm_putc, 文本模式用 out_char_color.
 * attr 为字符属性 (0x0F=黑底亮白). 供所有输出类系统调用共用. */
PUBLIC void user_put_char(char ch, t_8 attr)
{
	if (g_video_mode != 0) {
		/* 图形模式: 使用 vm_putc */
		int pos = gfx_get_cursor_pos();
		int row_bytes = g_text_cols * 2;
		int max_bytes = g_text_rows * g_text_cols * 2;

		if (pos < 0) pos = 0;

		switch (ch) {
		case '\n':
			/* 换行: 移动到下一行开头 */
			pos = (pos / row_bytes + 1) * row_bytes;
			/* 滚屏 */
			while (pos >= max_bytes) {
				gfx_scroll_up(1);
				pos -= row_bytes;
			}
			break;
		case '\b':
			/* 退格 */
			if (pos > 0) {
				pos -= 2;
				vm_putc(pos, ' ', attr);
			}
			break;
		default:
			/* 普通字符 */
			if (pos + 2 <= max_bytes) {
				vm_putc(pos, ch, attr);
				pos += 2;
			}
			break;
		}

		gfx_set_cursor(pos);
	} else {
		/* 文本模式: 使用 out_char_color 写入指定属性 */
		TTY* p_tty = &tty_table[nr_current_console];
		if (p_tty) {
			out_char_color(p_tty->p_console, ch, attr);
		}
	}
}

/*======================================================================*
                           sys_putc
 *======================================================================*/
PUBLIC int sys_putc(int ch, int unused, PROCESS* p_proc)
{
	int slot;
	if (use_user_console(p_proc)) {
		slot = current_user_slot(p_proc);
		wm_uc_putc(slot, (char)ch, g_user_attr);
		wm_uc_invalidate(slot);
		return 0;
	}
	user_put_char((char)ch, g_user_attr);
	return 0;
}

/*======================================================================*
                           sys_puts
 *======================================================================*/
PUBLIC int sys_puts(int str_off, int unused, PROCESS* p_proc)
{
	int slot;
	if (use_user_console(p_proc)) {
		/* 用户进程内存基址 = USER_PROC_BASE + slot * USER_PROC_SIZE */
		slot = current_user_slot(p_proc);
		{
			t_32 base = USER_PROC_BASE + (t_32)slot * USER_PROC_SIZE;
			char* str = (char*)(base + (t_32)str_off);
			while (*str) {
				wm_uc_putc(slot, *str, g_user_attr);
				str++;
			}
		}
		wm_uc_invalidate(slot);
		return 0;
	}
	{
		char* str = (char*)(USER_PROC_BASE + str_off);
		while (*str) {
			user_put_char(*str, g_user_attr);
			str++;
		}
	}
	return 0;
}

/*======================================================================*
                           sys_getc
 *======================================================================*/
PUBLIC int sys_getc(int unused1, int unused2, PROCESS* p_proc)
{
	int slot;
	/* 图形桌面模式: 通过控制台队列读取 (非阻塞, 不与 wm_run 竞争键盘).
	 * 返回 -1 表示无按键, 用户侧 _getc 循环重试. */
	if (g_video_mode != 0) {
		t_32 key;
		slot = current_user_slot(p_proc);
		if (slot < 0) return -1;
		key = wm_uc_pop_key(slot);
		if (key == 0) return -1;
		return (int)(key & 0xFF);
	}
	/* 文本模式: 非阻塞读键盘 (用户态 _getc 循环重试) */
	{
		t_32 key = 0;
		keyboard_read_simple(&key);
		if (key == 0) return -1;
		return (int)(key & 0xFF);
	}
}

/*======================================================================*
                           sys_exit
 *======================================================================*/
PUBLIC int sys_exit(int code, int unused, PROCESS* p_proc)
{
	int slot;
	/* 终止当前用户进程:
	 * 1. 记录退出码
	 * 2. 标记槽位为空闲 (释放内存区)
	 * 3. 关闭所有窗口 (控制台 + UW 子窗口, 如果在图形桌面模式)
	 * 4. 用 schedule() 选择下一个就绪进程
	 * 5. 调用 restart() 切换上下文 (不返回)
	 */
	p_proc->exit_code = code;
	p_proc->is_user_proc = 0;
	p_proc->ticks = 0;
	p_proc->priority = 0;
	user_proc_free_slots++;

	/* 关闭用户控制台窗口和所有 UW 子窗口 */
	slot = current_user_slot(p_proc);
	if (slot >= 0 && g_video_mode != 0) {
		/* 先关闭该进程创建的所有 UW 子窗口 */
		wm_uw_close_all_for_slot(slot);
		/* 再关闭用户控制台窗口 */
		wm_close_user_console(slot);
	}

	/* 选择下一个就绪进程 (schedule 会基于 ticks 选出) */
	schedule();

	/* restart() 是汇编函数, 加载 p_proc_ready 的栈帧并 iretd
	 * 它不返回, 直接切换到选中的进程继续执行
	 * 调用前禁用中断以避免竞态
	 */
	disable_int();
	restart();

	/* 永不到达 */
	return 0;
}

/*======================================================================*
                           sys_clrscr
 *======================================================================*/
PUBLIC int sys_clrscr(int unused1, int unused2, PROCESS* p_proc)
{
	int slot;
	if (use_user_console(p_proc)) {
		slot = current_user_slot(p_proc);
		wm_uc_clear(slot);
		wm_uc_invalidate(slot);
		return 0;
	}
	if (g_video_mode != 0) {
		gfx_clear_screen(0);
		gfx_set_cursor(0);
	} else {
		TTY* p_tty = &tty_table[nr_current_console];
		if (p_tty) {
			console_clrscr(p_tty->p_console);
		}
	}
	return 0;
}

/*======================================================================*
                           sys_gotoxy
 *======================================================================*/
PUBLIC int sys_gotoxy(int x, int y, PROCESS* p_proc)
{
	if (g_video_mode != 0) {
		int cols = g_text_cols;
		int rows = g_text_rows;
		if (cols <= 0) cols = 80;
		if (rows <= 0) rows = 25;
		if (x < 0) x = 0;
		if (x >= cols) x = cols - 1;
		if (y < 0) y = 0;
		if (y >= rows) y = rows - 1;
		gfx_set_cursor((y * cols + x) * 2);
	} else {
		TTY* p_tty = &tty_table[nr_current_console];
		if (p_tty) {
			console_gotoxy(p_tty->p_console, x, y);
		}
	}
	return 0;
}

/*======================================================================*
                           sys_get_xy
 *----------------------------------------------------------------------*
 * 返回 (x & 0xFFFF) | (y << 16), 其中 (x,y) 为当前光标的字符坐标.
 *======================================================================*/
PUBLIC int sys_get_xy(int unused1, int unused2, PROCESS* p_proc)
{
	int x, y;
	int cols = (g_text_cols > 0) ? g_text_cols : 80;

	if (g_video_mode != 0) {
		int pos = gfx_get_cursor_pos();
		if (pos < 0) pos = 0;
		x = (pos / 2) % cols;
		y = (pos / 2) / cols;
	} else {
		TTY* p_tty = &tty_table[nr_current_console];
		int cur = 0;
		if (p_tty && p_tty->p_console) {
			cur = (int)(p_tty->p_console->cursor - p_tty->p_console->original_addr);
		}
		if (cur < 0) cur = 0;
		x = cur % cols;
		y = cur / cols;
	}
	return (x & 0xFFFF) | ((y & 0xFFFF) << 16);
}

/*======================================================================*
                           sys_get_cols / sys_get_rows / sys_get_vmode
 *======================================================================*/
PUBLIC int sys_get_cols(int unused1, int unused2, PROCESS* p_proc)
{
	return (g_text_cols > 0) ? g_text_cols : 80;
}

PUBLIC int sys_get_rows(int unused1, int unused2, PROCESS* p_proc)
{
	return (g_text_rows > 0) ? g_text_rows : 25;
}

PUBLIC int sys_get_vmode(int unused1, int unused2, PROCESS* p_proc)
{
	return (int)g_video_mode;
}

/*======================================================================*
                           sys_set_color / sys_putc_color
 *======================================================================*/
PUBLIC int sys_set_color(int color, int unused, PROCESS* p_proc)
{
	g_user_attr = (t_8)color;
	return 0;
}

PUBLIC int sys_putc_color(int ch, int color, PROCESS* p_proc)
{
	user_put_char((char)ch, (t_8)color);
	return 0;
}

/*======================================================================*
                           sys_get_pid / sys_get_name
 *======================================================================*/
PUBLIC int sys_get_pid(int unused1, int unused2, PROCESS* p_proc)
{
	return (int)p_proc->pid;
}

/* 将当前进程名复制到用户缓冲区 (段内偏移 buf_off), 返回写入的字符数 (含\0前) */
PUBLIC int sys_get_name(int buf_off, int unused, PROCESS* p_proc)
{
	char* dst = (char*)(USER_PROC_BASE + buf_off);
	int i;
	for (i = 0; i < 15 && p_proc->name[i] != '\0'; i++) {
		dst[i] = p_proc->name[i];
	}
	dst[i] = '\0';
	return i;
}

/*======================================================================*
                           sys_rand / sys_srand
 *----------------------------------------------------------------------*
 * 线性同余法 (glibc 参数): 返回 [0, 32767] 区间伪随机数.
 *======================================================================*/
PUBLIC int sys_rand(int unused1, int unused2, PROCESS* p_proc)
{
	g_user_rand_seed = g_user_rand_seed * 1103515245u + 12345u;
	return (int)((g_user_rand_seed >> 16) & 0x7FFF);
}

PUBLIC int sys_srand(int seed, int unused, PROCESS* p_proc)
{
	g_user_rand_seed = (t_32)seed;
	return 0;
}

/*======================================================================*
                           put_uint_radix
 *----------------------------------------------------------------------*
 * 通用无符号整数输出: 按指定进制将数字转为字符串并输出到屏幕.
 * radix: 2/8/10/16 等; upper: radix=16 时大小写控制.
 *======================================================================*/
PRIVATE void put_uint_radix(unsigned int un, unsigned int radix, int upper)
{
	char buf[33];
	int i = 0;
	char a = upper ? 'A' : 'a';

	if (un == 0) {
		user_put_char('0', g_user_attr);
		return;
	}

	while (un > 0) {
		unsigned int d = un % radix;
		buf[i++] = (d < 10) ? (char)('0' + d) : (char)(a + d - 10);
		un /= radix;
	}
	while (i > 0) {
		user_put_char(buf[--i], g_user_attr);
	}
}

/*======================================================================*
                           sys_put_int
 *----------------------------------------------------------------------*
 * 输出有符号十进制整数 (使用当前 g_user_attr 颜色).
 *======================================================================*/
PUBLIC int sys_put_int(int n, int unused, PROCESS* p_proc)
{
	if (n < 0) {
		user_put_char('-', g_user_attr);
		put_uint_radix((unsigned int)(-(unsigned int)n), 10, 0);
	} else {
		put_uint_radix((unsigned int)n, 10, 0);
	}
	return 0;
}

/*======================================================================*
                           sys_put_uint
 *----------------------------------------------------------------------*
 * 输出无符号十进制整数.
 *======================================================================*/
PUBLIC int sys_put_uint(int n, int unused, PROCESS* p_proc)
{
	put_uint_radix((unsigned int)n, 10, 0);
	return 0;
}

/*======================================================================*
                           sys_put_hex
 *----------------------------------------------------------------------*
 * 输出十六进制整数. upper=1 使用大写 A-F, upper=0 使用小写 a-f.
 *======================================================================*/
PUBLIC int sys_put_hex(int n, int upper, PROCESS* p_proc)
{
	put_uint_radix((unsigned int)n, 16, upper);
	return 0;
}

/*======================================================================*
                           sys_put_bin
 *----------------------------------------------------------------------*
 * 输出二进制整数 (不含前导零, 0 输出 "0").
 *======================================================================*/
PUBLIC int sys_put_bin(int n, int unused, PROCESS* p_proc)
{
	put_uint_radix((unsigned int)n, 2, 0);
	return 0;
}

/*======================================================================*
                           sys_delay
 *----------------------------------------------------------------------*
 * 毫秒级延时: 基于 ticks (100Hz, 10ms/tick) 忙等.
 * 时钟中断仍会递增 ticks, 延时准确; 期间不调度其他进程 (MVP 单程序模型).
 *======================================================================*/
PUBLIC int sys_delay(int ms, int unused, PROCESS* p_proc)
{
	int ticks_needed;
	int start;

	if (ms <= 0) return 0;

	ticks_needed = (ms * HZ + 999) / 1000;	/* 向上取整 */
	if (ticks_needed <= 0) ticks_needed = 1;

	start = ticks;
	while (ticks - start < ticks_needed) {
		/* 忙等: ticks 由时钟中断递增 */
	}
	return 0;
}

/*======================================================================*
                           sys_getch
 *----------------------------------------------------------------------*
 * 阻塞读取一个字符, 返回 16 位键值 (含扩展键标志位).
 *   - 普通可见字符: 返回 ASCII (0x20~0x7E)
 *   - 扩展键 (Enter/ESC/Tab/Backspace/F1~F12/方向键等): 返回 0x100 + code
 *     例如 ENTER=0x103, ESC=0x101, F1=0x111, UP=0x125
 *   - Shift/Ctrl/Alt/CapsLock 等修饰键: 自动跳过, 不返回
 *   - 可用 (ret & 0x100) 判断是否为扩展键
 *
 * 实现说明:
 *   - 图形桌面模式: 通过控制台键盘队列读取 (非阻塞), 按键由 wm_run 路由.
 *     返回 0 表示无按键, 用户侧 _getch 循环重试 (用户态循环可被时钟中断调度).
 *     不能在内核态忙等 keyboard_read_simple — 会与 wm_run 竞争 kb_in 缓冲区,
 *     导致读到残留扫描码立即返回 (非阻塞假象) 或饿死 wm_run (死锁).
 *   - 文本模式: 非阻塞读键盘, 返回 0 时用户态重试.
 *======================================================================*/
PUBLIC int sys_getch(int unused1, int unused2, PROCESS* p_proc)
{
	int slot;
	/* 图形桌面模式: 总是通过控制台队列 (非阻塞, 不与 wm_run 竞争键盘) */
	if (g_video_mode != 0) {
		slot = current_user_slot(p_proc);
		if (slot < 0) return 0;
		return (int)wm_uc_pop_key(slot);
	}
	/* 文本模式: 非阻塞读键盘 (用户态 _getch 循环重试) */
	{
		t_32 key = 0;
		keyboard_read_simple(&key);
		return (int)key;
	}
}

/*======================================================================*
                           sys_kbhit
 *----------------------------------------------------------------------*
 * 检测键盘缓冲区是否有按键 (不消费数据).
 * 返回: 1 = 有按键, 0 = 无
 *======================================================================*/
PUBLIC int sys_kbhit(int unused1, int unused2, PROCESS* p_proc)
{
	return keyboard_has_key() ? 1 : 0;
}

/*======================================================================*
                           sys_scroll
 *----------------------------------------------------------------------*
 * 屏幕向上滚动指定行数 (内容上移, 底部出现空行).
 * lines<=0 时不动, 过大时滚到底.
 *======================================================================*/
PUBLIC int sys_scroll(int lines, int unused, PROCESS* p_proc)
{
	if (lines <= 0) return 0;

	if (g_video_mode != 0) {
		int row_bytes = g_text_cols * 2;
		int max_bytes = g_text_rows * g_text_cols * 2;
		int pos;
		int i;

		if (lines > g_text_rows) lines = g_text_rows;

		for (i = 0; i < lines; i++) {
			gfx_scroll_up(1);
		}
		/* 光标跟随上移, 不超出屏幕 */
		pos = gfx_get_cursor_pos();
		if (pos < 0) pos = 0;
		pos -= lines * row_bytes;
		if (pos < 0) pos = 0;
		if (pos >= max_bytes) pos = max_bytes - row_bytes;
		gfx_set_cursor(pos);
	} else {
		TTY* p_tty = &tty_table[nr_current_console];
		if (p_tty && p_tty->p_console) {
			int i;
			for (i = 0; i < lines; i++) {
				scroll_screen(p_tty->p_console, SCROLL_SCREEN_UP);
			}
		}
	}
	return 0;
}

/*======================================================================*
 *                    文件系统系统调用 (NR 26-32)
 *----------------------------------------------------------------------*
 *  set_io_size : 设置文件 IO 缓冲区大小 (供 file_read/write/list 使用)
 *  file_read   : 读取文件内容
 *  file_write  : 写入文件 (创建或覆盖)
 *  file_delete  : 删除文件
 *  file_exists  : 检查文件是否存在
 *  file_size    : 获取文件大小
 *  file_list    : 列出根目录文件名
 *======================================================================*/

PUBLIC int sys_set_io_size(int size, int unused, PROCESS* p_proc)
{
	g_io_size = size;
	return 0;
}

PUBLIC int sys_file_read(int name_off, int buf_off, PROCESS* p_proc)
{
	char* name = (char*)(USER_PROC_BASE + (t_32)name_off);
	char* buf  = (char*)(USER_PROC_BASE + (t_32)buf_off);
	int   bufsize = (g_io_size > 0) ? g_io_size : USER_PROC_MAX_SIZE;

	if (fat_init() != 0) return -1;
	return fat_read_file(name, buf, bufsize);
}

PUBLIC int sys_file_write(int name_off, int buf_off, PROCESS* p_proc)
{
	char* name = (char*)(USER_PROC_BASE + (t_32)name_off);
	char* buf  = (char*)(USER_PROC_BASE + (t_32)buf_off);
	int   size = g_io_size;

	if (size <= 0) return -1;
	if (fat_init() != 0) return -1;
	return fat_write_file(name, buf, size);
}

PUBLIC int sys_file_delete(int name_off, int unused, PROCESS* p_proc)
{
	char* name = (char*)(USER_PROC_BASE + (t_32)name_off);

	if (fat_init() != 0) return -1;
	return fat_delete(name);
}

/* --- file_exists / file_size: 用 fat_list_root 回调查找目录项 --- */

static FAT_DIR_ENTRY s_find_entry;
static int           s_found;
static char          s_search_name83[11];

static void find_file_callback(FAT_DIR_ENTRY* entry)
{
	char de_name[11];
	int i;

	if (s_found) return;

	/* 目录项 name[8]+ext[3] 转大写 */
	for (i = 0; i < 8; i++) {
		char c = entry->name[i];
		if (c >= 'a' && c <= 'z') c -= 32;
		de_name[i] = c;
	}
	for (i = 0; i < 3; i++) {
		char c = entry->ext[i];
		if (c >= 'a' && c <= 'z') c -= 32;
		de_name[8 + i] = c;
	}

	/* 逐字节比较 8.3 格式文件名 */
	for (i = 0; i < 11; i++) {
		if (de_name[i] != s_search_name83[i]) return;
	}

	s_find_entry = *entry;
	s_found = 1;
}

PUBLIC int sys_file_exists(int name_off, int unused, PROCESS* p_proc)
{
	char* name = (char*)(USER_PROC_BASE + (t_32)name_off);

	if (fat_init() != 0) return 0;
	fat_format_name(name, s_search_name83);
	s_found = 0;
	fat_list_root(find_file_callback);
	return s_found ? 1 : 0;
}

PUBLIC int sys_file_size(int name_off, int unused, PROCESS* p_proc)
{
	char* name = (char*)(USER_PROC_BASE + (t_32)name_off);

	if (fat_init() != 0) return -1;
	fat_format_name(name, s_search_name83);
	s_found = 0;
	fat_list_root(find_file_callback);
	if (!s_found) return -1;
	return (int)s_find_entry.file_size;
}

/* --- file_list: 遍历根目录, 文件名写入用户缓冲区 (\n 分隔) --- */

static char* s_fl_buf;
static int   s_fl_pos;
static int   s_fl_max;
static int   s_fl_count;

static void file_list_callback(FAT_DIR_ENTRY* entry)
{
	char name_buf[16];
	int  name_len = 0;
	int  i;
	int  has_ext = 0;

	if (entry->attr & FAT_ATTR_DIRECTORY) return;  /* 跳过子目录 */
	if (entry->attr & FAT_ATTR_VOLUME_ID) return;  /* 跳过卷标 */

	/* 提取文件名 (8 字符, 跳过尾部空格) */
	for (i = 0; i < 8; i++) {
		if (entry->name[i] != ' ') {
			name_buf[name_len++] = entry->name[i];
		}
	}

	/* 检查是否有扩展名 */
	for (i = 0; i < 3; i++) {
		if (entry->ext[i] != ' ') {
			has_ext = 1;
			break;
		}
	}

	/* 添加扩展名 */
	if (has_ext) {
		name_buf[name_len++] = '.';
		for (i = 0; i < 3; i++) {
			if (entry->ext[i] != ' ') {
				name_buf[name_len++] = entry->ext[i];
			}
		}
	}

	/* 写入用户缓冲区: 文件名 + '\n' */
	if (s_fl_pos + name_len + 1 <= s_fl_max) {
		for (i = 0; i < name_len; i++) {
			s_fl_buf[s_fl_pos++] = name_buf[i];
		}
		s_fl_buf[s_fl_pos++] = '\n';
		s_fl_count++;
	}
}

PUBLIC int sys_file_list(int buf_off, int unused, PROCESS* p_proc)
{
	s_fl_buf   = (char*)(USER_PROC_BASE + (t_32)buf_off);
	s_fl_pos   = 0;
	s_fl_max   = (g_io_size > 0) ? g_io_size : 4096;
	s_fl_count = 0;

	if (fat_init() != 0) return -1;
	fat_list_root(file_list_callback);
	return s_fl_count;
}

/*======================================================================*
 *                    系统控制 (NR 33-34)
 *======================================================================*/

PUBLIC int sys_reboot(int unused1, int unused2, PROCESS* p_proc)
{
	disable_int();
	/* 8042 键盘控制器复位脉冲 */
	while (in_byte(0x64) & 0x02) { }
	out_byte(0x64, 0xFE);
	while (1) { }
	return 0;
}

PUBLIC int sys_get_key_flags(int unused1, int unused2, PROCESS* p_proc)
{
	return keyboard_get_flags();
}

/*======================================================================*
 *                    文件追加与信息查询 (NR 35, 38)
 *======================================================================*/

PUBLIC int sys_file_append(int name_off, int buf_off, PROCESS* p_proc)
{
	char* name = (char*)(USER_PROC_BASE + (t_32)name_off);
	char* buf  = (char*)(USER_PROC_BASE + (t_32)buf_off);
	int   size = g_io_size;

	if (size <= 0) return -1;
	if (fat_init() != 0) return -1;
	return fat_append_file(name, buf, size);
}

PUBLIC int sys_file_stat(int name_off, int buf_off, PROCESS* p_proc)
{
	char* name = (char*)(USER_PROC_BASE + (t_32)name_off);
	int*  info = (int*)(USER_PROC_BASE + (t_32)buf_off);

	if (fat_init() != 0) return -1;
	fat_format_name(name, s_search_name83);
	s_found = 0;
	fat_list_root(find_file_callback);
	if (!s_found) return -1;

	/* info[0]=文件大小, info[1]=FAT 属性字节 */
	info[0] = (int)s_find_entry.file_size;
	info[1] = (int)s_find_entry.attr;
	return 0;
}

/*======================================================================*
 *                    类型转换 (NR 36-37)
 *======================================================================*/

PUBLIC int sys_int_to_str(int value, int buf_off, PROCESS* p_proc)
{
	char* buf = (char*)(USER_PROC_BASE + (t_32)buf_off);
	int   radix = g_io_size;
	char  tmp[33];
	int   i = 0;
	int   pos = 0;
	unsigned int un;
	int   neg = 0;

	if (radix == 0) radix = 10;
	if (radix < 2 || radix > 36) return -1;

	if (radix == 10 && value < 0) {
		neg = 1;
		un = (unsigned int)(-(unsigned int)value);
	} else {
		un = (unsigned int)value;
	}

	if (un == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return 1;
	}

	while (un > 0) {
		int d = un % radix;
		tmp[i++] = (d < 10) ? (char)('0' + d) : (char)('a' + d - 10);
		un /= radix;
	}

	if (neg) buf[pos++] = '-';
	while (i > 0) buf[pos++] = tmp[--i];
	buf[pos] = '\0';

	return pos;
}

PUBLIC int sys_atoi(int str_off, int unused, PROCESS* p_proc)
{
	char* s = (char*)(USER_PROC_BASE + (t_32)str_off);
	int   sign = 1;
	int   result = 0;

	/* 跳过前导空白 */
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

	/* 处理符号 */
	if (*s == '-') { sign = -1; s++; }
	else if (*s == '+') s++;

	/* 解析十进制数字 (带溢出保护: 饱和到 INT_MAX/INT_MIN) */
	while (*s >= '0' && *s <= '9') {
		int digit = *s - '0';
		if (result > (0x7FFFFFFF - digit) / 10) {
			/* 溢出: 饱和返回 */
			return (sign > 0) ? 0x7FFFFFFF : (int)0x80000000;
		}
		result = result * 10 + digit;
		s++;
	}

	return result * sign;
}

/*======================================================================*
                           sys_spawn
 *======================================================================*/
PUBLIC int sys_spawn(int name_off, int blocking, PROCESS* p_proc)
{
	const char* name;
	int slot;
	t_32 base;

	/* 推算当前进程的内存基址 (Phase 3: 每进程独立 128KB 区) */
	{
		int idx = p_proc - proc_table;
		if (idx >= NR_TASKS + NR_PROCS) {
			int s = idx - (NR_TASKS + NR_PROCS);
			base = USER_PROC_BASE + (t_32)s * USER_PROC_SIZE;
		} else {
			base = USER_PROC_BASE;
		}
	}
	name = (const char*)(base + (t_32)name_off);

	if (fat_init() != 0) return -1;

	/* 安全策略: spawn 总是非阻塞, 忽略 blocking 参数.
	 * 阻塞/非阻塞由 shell run 命令控制 (操作员决定), 不对应用开放,
	 * 防止恶意程序用阻塞模式卡住系统. */
	slot = exec_user_program(name);
	if (slot < 0) return slot;

	/* 非阻塞: 恢复 p_proc_ready=父进程, 让父进程立即继续执行 */
	p_proc_ready = p_proc;
	return slot;
}


/*======================================================================*
 *              用户控制台与窗口 API 系统调用 (NR 40-46)
 *----------------------------------------------------------------------*
 *  sys_uc_putc       (40): 向当前进程的控制台输出字符 (ch, attr)
 *  sys_uc_clear      (41): 清空当前进程的控制台
 *  sys_win_create    (42): 创建用户自定义窗口 (参数块在用户内存)
 *  sys_win_close     (43): 关闭用户窗口
 *  sys_win_draw_text (44): 在用户窗口画布上绘制文本 (参数块)
 *  sys_win_draw_rect (45): 在用户窗口画布上绘制矩形 (参数块)
 *  sys_win_get_event (46): 阻塞获取用户窗口事件
 *
 * 参数传递约定: 系统调用最多直接传 2 个参数 (ebx, ecx).
 * 多参数的窗口 API (create/draw_text/draw_rect) 使用参数块:
 *   用户程序在自身内存中构造参数结构体, 通过 ebx 传段内偏移,
 *   内核用 user_mem_base + offset 读取.
 *======================================================================*/

/* 辅助: 获取当前进程的内存基址 */
PRIVATE t_32 user_mem_base(PROCESS* p_proc)
{
	int slot = current_user_slot(p_proc);
	if (slot < 0) return USER_PROC_BASE;
	return USER_PROC_BASE + (t_32)slot * USER_PROC_SIZE;
}

/* NR_uc_putc (40): 向当前进程的控制台输出字符 */
PUBLIC int sys_uc_putc(int ch, int attr, PROCESS* p_proc)
{
	int slot = current_user_slot(p_proc);
	if (slot < 0) return -1;
	wm_uc_putc(slot, (char)ch, (t_8)attr);
	wm_uc_invalidate(slot);
	return 0;
}

/* NR_uc_clear (41): 清空当前进程的控制台 */
PUBLIC int sys_uc_clear(int unused1, int unused2, PROCESS* p_proc)
{
	int slot = current_user_slot(p_proc);
	if (slot < 0) return -1;
	wm_uc_clear(slot);
	wm_uc_invalidate(slot);
	return 0;
}

/* NR_win_create (42): 创建用户窗口
 * args_off 指向用户内存中的参数块: {x, y, w, h, title_off}
 * title_off 为标题字符串的段内偏移 (0=使用默认标题)
 * 返回: 窗口 ID (>=0) 或 -1 (失败) */
PUBLIC int sys_win_create(int args_off, int unused, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *args = (int*)(base + (t_32)args_off);
	int slot = current_user_slot(p_proc);
	int x, y, w, h, title_off;
	const char *title;

	if (slot < 0) return -1;
	x = args[0];
	y = args[1];
	w = args[2];
	h = args[3];
	title_off = args[4];
	title = title_off ? (const char*)(base + (t_32)title_off) : (const char*)0;

	return wm_uw_create(slot, x, y, w, h, title);
}

/* NR_win_close (43): 关闭用户窗口 */
PUBLIC int sys_win_close(int win_id, int unused, PROCESS* p_proc)
{
	wm_uw_close(win_id);
	return 0;
}

/* NR_win_draw_text (44): 在用户窗口画布上绘制文本
 * args_off 指向用户内存中的参数块: {win_id, x, y, str_off, fg, bg}
 * str_off 为字符串的段内偏移 */
PUBLIC int sys_win_draw_text(int args_off, int unused, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *args = (int*)(base + (t_32)args_off);
	int win_id, x, y, str_off, fg, bg;
	const char *str;

	win_id  = args[0];
	x       = args[1];
	y       = args[2];
	str_off = args[3];
	fg      = args[4];
	bg      = args[5];
	str = (const char*)(base + (t_32)str_off);

	wm_uw_draw_text(win_id, x, y, str, (t_8)fg, (t_8)bg);
	wm_uw_invalidate(win_id);
	return 0;
}

/* NR_win_draw_rect (45): 在用户窗口画布上绘制实心矩形
 * args_off 指向用户内存中的参数块: {win_id, x, y, w, h, color} */
PUBLIC int sys_win_draw_rect(int args_off, int unused, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *args = (int*)(base + (t_32)args_off);
	int win_id, x, y, w, h, color;

	win_id = args[0];
	x      = args[1];
	y      = args[2];
	w      = args[3];
	h      = args[4];
	color  = args[5];

	wm_uw_draw_rect(win_id, x, y, w, h, (t_8)color);
	wm_uw_invalidate(win_id);
	return 0;
}

/* NR_win_get_event (46): 获取用户窗口事件
 * win_id = 窗口 ID, ev_off = 输出缓冲段内偏移
 * 输出: ev[0]=事件类型, ev[1]=x, ev[2]=y
 * 返回: 事件类型 (0=无事件, 用户侧 _win_get_event 包装循环重试).
 * 不能在系统调用上下文中忙等 — 会饿死 wm_run (调度器死锁). */
PUBLIC int sys_win_get_event(int win_id, int ev_off, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *ev = (int*)(base + (t_32)ev_off);
	int type, x, y;

	type = wm_uw_pop_event(win_id, &x, &y);
	if (type != 0) {
		ev[0] = type;
		ev[1] = x;
		ev[2] = y;
	}
	return type;
}

/*======================================================================*
 *              窗口 API 扩展 (NR 47-52)
 *======================================================================*/

/* NR_win_clear (47): 清空用户窗口画布
 * args_off 指向参数块: {win_id, color} */
PUBLIC int sys_win_clear(int args_off, int unused, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *args = (int*)(base + (t_32)args_off);
	int win_id = args[0];
	int color  = args[1];

	wm_uw_clear(win_id, (t_8)color);
	wm_uw_invalidate(win_id);
	return 0;
}

/* NR_win_set_title (48): 设置窗口标题
 * args_off 指向参数块: {win_id, title_off} */
PUBLIC int sys_win_set_title(int args_off, int unused, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *args = (int*)(base + (t_32)args_off);
	int win_id = args[0];
	int title_off = args[1];
	const char *title = (const char*)(base + (t_32)title_off);

	wm_uw_set_title(win_id, title);
	return 0;
}

/* NR_win_draw_line (49): 在画布上画线
 * args_off 指向参数块: {win_id, x1, y1, x2, y2, color} */
PUBLIC int sys_win_draw_line(int args_off, int unused, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *args = (int*)(base + (t_32)args_off);
	int win_id = args[0];
	int x1 = args[1];
	int y1 = args[2];
	int x2 = args[3];
	int y2 = args[4];
	int color = args[5];

	wm_uw_draw_line(win_id, x1, y1, x2, y2, (t_8)color);
	wm_uw_invalidate(win_id);
	return 0;
}

/* NR_win_set_pixel (50): 设置单个像素
 * args_off 指向参数块: {win_id, x, y, color} */
PUBLIC int sys_win_set_pixel(int args_off, int unused, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *args = (int*)(base + (t_32)args_off);
	int win_id = args[0];
	int x = args[1];
	int y = args[2];
	int color = args[3];

	wm_uw_set_pixel(win_id, x, y, (t_8)color);
	wm_uw_invalidate(win_id);
	return 0;
}

/* NR_win_peek_event (51): 非阻塞获取事件
 * 返回事件类型 (0=无事件), 有事件时写入 ev[0..2] */
PUBLIC int sys_win_peek_event(int win_id, int ev_off, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *ev = (int*)(base + (t_32)ev_off);
	int type, x, y;

	type = wm_uw_pop_event(win_id, &x, &y);
	if (type != 0) {
		ev[0] = type;
		ev[1] = x;
		ev[2] = y;
	}
	return type;
}

/* NR_win_get_size (52): 获取窗口客户区尺寸
 * size_off 指向输出缓冲: size[0]=width, size[1]=height
 * 返回 0=成功, -1=失败 */
PUBLIC int sys_win_get_size(int win_id, int size_off, PROCESS* p_proc)
{
	t_32 base = user_mem_base(p_proc);
	int *size = (int*)(base + (t_32)size_off);
	int w, h;

	wm_uw_get_size(win_id, &w, &h);
	size[0] = w;
	size[1] = h;
	return (w > 0 && h > 0) ? 0 : -1;
}

/* NR_win_set_closable (53): 设置窗口是否允许点击叉号关闭
 * win_id = 窗口 ID, closable = 0(禁止) 或 1(允许)
 * 返回 0=成功, -1=失败 */
PUBLIC int sys_win_set_closable(int win_id, int closable, PROCESS* p_proc)
{
	wm_uw_set_closable(win_id, closable);
	return 0;
}

/* NR_win_get_closable (54): 查询窗口是否允许点击叉号关闭
 * 返回 1=允许, 0=禁止 (或窗口不存在) */
PUBLIC int sys_win_get_closable(int win_id, int unused, PROCESS* p_proc)
{
	return wm_uw_get_closable(win_id);
}
