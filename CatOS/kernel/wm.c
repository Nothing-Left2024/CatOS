/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                              wm.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
              CatOS Window Manager (Enhanced UI)

  功能:
    - 窗口创建/移动/最小化/最大化/关闭
    - Z-order 调度 (活动窗口置顶)
    - 标题栏: 左侧图标 + 中间标题 + 右侧三个按钮 (最大化/最小化/关闭)
    - 3D 风格窗口边框 + 阴影
    - 鼠标交互: 点击按钮、拖拽标题栏移动、点击窗口激活
    - 任务栏: 底部显示最小化窗口, 点击还原
    - 主窗口: 大图标排列, 双击打开应用
    - 文件管理器: 文件/文件夹浏览, 双击打开文件夹

  依赖: gfx.c 绘制原语, mouse.c 鼠标查询接口
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
#include "keyboard.h"
#include "wm.h"
#include "fat.h"
#include "hd.h"

/* ===== 内部状态 ===== */

PRIVATE WINDOW windows[WM_MAX_WINDOWS];
PRIVATE int wm_count = 0;
PRIVATE int wm_active = -1;

/* 拖拽状态 */
PRIVATE int drag_active = 0;        /* 是否正在拖拽 */
PRIVATE int drag_off_x, drag_off_y; /* 鼠标相对窗口左上角的偏移 */
PRIVATE int drag_orig_x, drag_orig_y; /* 拖拽开始时窗口原始位置 */
PRIVATE int drag_box_x, drag_box_y;   /* 当前橡皮筋边框位置 (拖动时只画边框, 松手才重绘窗口) */

/* 任务栏高度 (随字体缩放, 确保能容纳字体高度) */
#define WM_TASKBAR_H  (g_font_height + 6)

/* ===== 文件管理器: 绘制回调状态 ===== */
PRIVATE int fm_draw_y;         /* 当前绘制 y 坐标 */
PRIVATE int fm_draw_idx;       /* 当前条目索引 */
PRIVATE int fm_draw_max_y;     /* 客户区最大 y (超出不画) */
PRIVATE int fm_draw_selected;  /* 选中项索引 */
PRIVATE int fm_draw_client_x;  /* 客户区 x */
PRIVATE int fm_draw_client_w;  /* 客户区宽 */

/* ===== 文件管理器: 查找回调状态 ===== */
PRIVATE int  fm_lookup_target;   /* 目标索引 */
PRIVATE int  fm_lookup_current;  /* 当前索引 */
PRIVATE t_32 fm_lookup_cluster;  /* 结果: 起始簇 */
PRIVATE int  fm_lookup_is_dir;   /* 结果: 是否目录 */
PRIVATE char fm_lookup_name[13]; /* 结果: 8.3 文件名 */
PRIVATE char fm_lookup_ext[4];   /* 结果: 扩展名 (小写, 如 "txt") */

/* ===== 文件管理器: 双击检测 ===== */
PRIVATE int fm_dclick_win;     /* 上次点击窗口 */
PRIVATE int fm_dclick_row;     /* 上次点击行 */
PRIVATE int fm_dclick_ticks;   /* 上次点击时的系统 ticks */

/* ===== 主窗口: 双击检测 ===== */
PRIVATE int main_dclick_ticks = 0;
PRIVATE int main_dclick_app = -1;

/* ===== 任务栏悬停跟踪 ===== */
PRIVATE int taskbar_hover = -1;

/* 主窗口索引 */
PRIVATE int main_window = -1;

/* ===== 终端常量与状态 (需在 wm_init 前可见) ===== */
#define TERM_COLS    34
#define TERM_ROWS    11
#define TERM_BUF_SZ  (TERM_COLS * TERM_ROWS)
#define TERM_FG      0x0A    /* 终端文字: 亮绿 */
#define TERM_BG      0x00    /* 终端背景: 黑 */
#define TERM_PROMPT  0x0B    /* 提示符颜色: 青 */
#define TERM_ERROR   0x0C    /* 错误信息: 红 */
#define TERM_INFO    0x0F    /* 普通信息: 白 */

/* 命令历史 */
#define TERM_HIST_SZ   16     /* 最多保存 16 条历史 */
/* 滚动历史行数 (PageUp/PageDown 回看) */
#define TERM_SCROLL_LINES 64

/* 终端状态 (单实例) */
PRIVATE struct {
	char text[TERM_BUF_SZ];
	t_8  attr[TERM_BUF_SZ];
	int  cur_row;           /* 光标行 */
	int  cur_col;           /* 光标列 */
	char cmdline[TERM_COLS]; /* 当前命令行 */
	int  cmd_len;            /* 命令行字符数 */
	int  cmd_cursor;         /* 命令行光标位置 (0~cmd_len) */
	int  initialized;        /* 是否已初始化 */
	int  win_idx;            /* 所属窗口索引 */
	/* 命令历史 */
	char hist[TERM_HIST_SZ][TERM_COLS]; /* 历史记录 */
	int  hist_count;         /* 已存历史条数 */
	int  hist_idx;           /* 当前浏览的历史索引 (-1=不在浏览) */
	/* 滚动回看历史 (被 term_scroll 滚掉的行) */
	char s_hist[TERM_SCROLL_LINES][TERM_COLS]; /* 历史行文本 */
	t_8  s_hist_attr[TERM_SCROLL_LINES][TERM_COLS]; /* 历史行属性 */
	int  s_hist_count;       /* 已存历史行数 */
	int  s_hist_pos;         /* 环形缓冲下一个写入位置 */
	int  view_offset;        /* 0=最新, >0=向上回看行数 */
	/* 当前工作目录 (0=根目录, >=2=子目录首簇) */
	t_32 cwd;
	char cwd_path[40];       /* 当前路径 (如 "/" 或 "/test/") */
	int  prompt_len;         /* 当前提示符长度 (动态) */
} term;

/* 终端文件缓冲区 (用于 type 命令, BSS) */
PRIVATE t_8 term_file_buf[4096];

/* 终端命名字符串 (非const, 放入 .data 段, 避免 .rodata 寻址问题) */
PRIVATE char S_T_PROMPT[]    = "CatOS> ";
PRIVATE char S_T_HELP_HDR[]  = "Commands:\n";
PRIVATE char S_T_HELP_1[]    = " help ver echo cls\n";
PRIVATE char S_T_HELP_2[]    = " dir ls type touch rd\n";
PRIVATE char S_T_HELP_3[]    = " cd mkdir run edit\n";
PRIVATE char S_T_HELP_4[]    = " ps sysinfo\n";
PRIVATE char S_T_VER1[]      = "CatOS v1.0\n";
PRIVATE char S_T_VER2[]      = " 32-bit OS, VGA Mode 13h\n";
PRIVATE char S_T_CPU[]       = "CPU: ";
PRIVATE char S_T_CPU2[]      = "x86 32-bit\n";
PRIVATE char S_T_VIDEO[]     = "Video: VGA 320x200x256\n";
PRIVATE char S_T_MEM[]       = "Mem: ";
PRIVATE char S_T_KBC[]       = " KB conv\n";
PRIVATE char S_T_FS[]        = "FS: ";
PRIVATE char S_T_FAT[]       = "FAT";
PRIVATE char S_T_FS_NONE[]   = "none\n";
PRIVATE char S_T_NO_FS[]     = "No filesystem\n";
PRIVATE char S_T_EMPTY[]     = "(empty)\n";
PRIVATE char S_T_PID[]       = "PID  NAME\n";
PRIVATE char S_T_NOPROC[]    = "No processes\n";
PRIVATE char S_T_UNKNOWN[]   = "Unknown: ";
PRIVATE char S_T_CAT_DIS[]   = "'cat' is disabled in terminal.\n";
PRIVATE char S_T_DSK_RUN[]   = "Desktop is already running.\n";
PRIVATE char S_T_REBOOT[]    = "Rebooting...\n";
PRIVATE char S_T_REBOOT_NO[] = "Use shell to reboot.\n";
PRIVATE char S_T_WELCOME1[]  = "CatOS Terminal v1.0\n";
PRIVATE char S_T_WELCOME2[]  = "Type 'help' for commands.\n";
PRIVATE char S_T_TYPE_USE[]  = "Usage: type <file>\n";
PRIVATE char S_T_TYPE_NF[]   = "File not found: ";
PRIVATE char S_T_TOUCH_USE[] = "Usage: touch <file>\n";
PRIVATE char S_T_TOUCH_OK[]  = "Created: ";
PRIVATE char S_T_TOUCH_EX[]  = "Exists: ";
PRIVATE char S_T_TOUCH_FAIL[]= "touch failed\n";
PRIVATE char S_T_RD_USE[]    = "Usage: rd <file>\n";
PRIVATE char S_T_RD_OK[]     = "Deleted: ";
PRIVATE char S_T_RD_NF[]     = "Not found: ";
PRIVATE char S_T_RD_FAIL[]   = "rd failed\n";
PRIVATE char S_T_EDIT_USE[]  = "Usage: edit <file>\n";
PRIVATE char S_T_DIR_TAG[]   = " <DIR> ";
PRIVATE char S_T_DIR_SPACE[] = "       ";
PRIVATE char S_T_TERM_TITLE[] = "Terminal";
/* cd/mkdir 命令字符串 */
PRIVATE char S_T_CD_ROOT[]   = "/\n";
PRIVATE char S_T_CD_NF[]     = "Dir not found: ";
PRIVATE char S_T_MKDIR_USE[] = "Usage: mkdir <name>\n";
PRIVATE char S_T_MKDIR_OK[]  = "Created: ";
PRIVATE char S_T_MKDIR_EX[]  = "Exists: ";
PRIVATE char S_T_MKDIR_FAIL[]= "mkdir failed\n";
PRIVATE char S_T_RUN_USE[]   = "Usage: run <file>\n";
PRIVATE char S_T_RUN_NF[]    = "Not found: ";
PRIVATE char S_T_RUN_OK[]    = "Running: ";
PRIVATE char S_T_RUN_FAIL[]  = "Run failed\n";
/* 用户控制台退出提示 (非const, 放入 .data 段) */
PRIVATE char S_UC_PRESS_KEY[] = "\nPress any key to continue...\n";
/* 主窗口图标标签 (非const, 放入 .data 段, 避免 .rodata 寻址问题) */
PRIVATE char S_M_FILE[] = "File Manager";
PRIVATE char S_M_CALC[] = "Calculator";
PRIVATE char S_M_TERM[] = "Terminal";
PRIVATE char S_M_ABOUT[] = "About";

/* 前向声明: 后面定义的客户区绘制函数 */
PRIVATE void wm_draw_main_client(int idx, int hover_app);
PRIVATE void wm_draw_fm_client(int idx);
PRIVATE void wm_draw_calc_client(int idx);
PRIVATE void wm_draw_about_client(int idx);
PRIVATE void wm_draw_term_client(int idx);
PRIVATE void wm_draw_ed_client(int idx);
PRIVATE void wm_draw_ow_client(int idx);
PRIVATE void wm_draw_uc_client(int idx);
PRIVATE void wm_draw_uw_client(int idx);
PRIVATE void term_handle_key(t_32 key);
PRIVATE void ed_handle_key(int idx, t_32 key);
PRIVATE void ed_handle_click(int idx, int mx, int my);
PRIVATE void wm_close(int idx);
PRIVATE int wm_create_terminal(void);
PUBLIC int wm_create_editor(const char *filename, t_32 cluster);
PUBLIC int wm_create_openwith(const char *filename, t_32 cluster);

/* ===== 编辑器缓冲区 (单实例, BSS) ===== */
#define ED_MAX_CHARS  2048   /* 编辑器最大字符数 */
PRIVATE char ed_buf[ED_MAX_CHARS];
PRIVATE int  ed_total_rows;  /* 缓冲区总行数 */

/* ===== 用户程序控制台窗口 (每个用户进程一个) ===== */
#define UC_COLS      34        /* 控制台列数 (与终端一致) */
#define UC_ROWS      11        /* 控制台行数 */
#define UC_KEY_QSZ   32        /* 键盘事件队列大小 */
PRIVATE struct USER_CONSOLE {
	int  active;                                  /* 是否激活 */
	int  owner_slot;                              /* 用户进程槽位 0..3 */
	int  win_idx;                                 /* 关联的 WM 窗口索引 */
	int  exiting;                                 /* 程序已退出, 等待按键确认关闭 */
	char text[UC_ROWS][UC_COLS];                  /* 文本缓冲 */
	t_8  attr[UC_ROWS][UC_COLS];                  /* 属性缓冲 (低4位前景, 高4位背景) */
	int  cur_row, cur_col;                        /* 光标位置 */
	int  cursor_visible;                          /* 光标是否可见 (闪烁) */
	/* 键盘事件队列 (环形缓冲) */
	t_32 key_queue[UC_KEY_QSZ];
	int  key_head, key_tail, key_count;
} user_consoles[NR_USER_PROCS];

/* ===== 用户程序自定义窗口 (基础窗口 API) ===== */
#define UW_MAX       4          /* 最多 4 个用户自定义窗口 */
#define UW_MAX_W     200        /* 画布最大宽度 */
#define UW_MAX_H     140        /* 画布最大高度 */
#define UW_EV_QSZ    16         /* 事件队列大小 */
/* 事件类型 */
#define UW_EV_NONE     0
#define UW_EV_LDOWN    1        /* 左键按下 */
#define UW_EV_LUP      2        /* 左键释放 */
#define UW_EV_MOUSE    3        /* 鼠标移动 */
#define UW_EV_KEY      4        /* 按键 */
#define UW_EV_CLOSE    5        /* 关闭请求 */
PRIVATE struct USER_WINDOW {
	int  active;                                  /* 是否激活 */
	int  owner_slot;                              /* 用户进程槽位 */
	int  win_idx;                                 /* WM 窗口索引 */
	int  cw, ch;                                  /* 画布尺寸 */
	int  closable;                                /* 是否允许点击叉号关闭 (1=允许, 0=禁止) */
	t_8  canvas[UW_MAX_W * UW_MAX_H];             /* 画布像素缓冲 */
	/* 事件队列 (每个事件 3 个 int: type, x, y) */
	int  ev_queue[UW_EV_QSZ * 3];
	int  ev_head, ev_tail, ev_count;
} user_windows[UW_MAX];


/*======================================================================*
                      用户控制台缓冲操作函数
 *======================================================================*/
/* 滚动控制台一行 */
PRIVATE void uc_scroll_up(int slot)
{
	struct USER_CONSOLE *uc = &user_consoles[slot];
	int r, c;
	for (r = 0; r < UC_ROWS - 1; r++) {
		for (c = 0; c < UC_COLS; c++) {
			uc->text[r][c] = uc->text[r+1][c];
			uc->attr[r][c] = uc->attr[r+1][c];
		}
	}
	for (c = 0; c < UC_COLS; c++) {
		uc->text[UC_ROWS-1][c] = ' ';
		uc->attr[UC_ROWS-1][c] = 0x0F;
	}
}

/* 向控制台写入一个字符 */
PUBLIC void wm_uc_putc(int slot, char ch, t_8 attr)
{
	struct USER_CONSOLE *uc;
	if (slot < 0 || slot >= NR_USER_PROCS) return;
	uc = &user_consoles[slot];
	if (!uc->active) return;

	if (ch == '\n') {
		uc->cur_col = 0;
		uc->cur_row++;
		if (uc->cur_row >= UC_ROWS) {
			uc_scroll_up(slot);
			uc->cur_row = UC_ROWS - 1;
		}
		return;
	}
	if (ch == '\b') {
		if (uc->cur_col > 0) {
			uc->cur_col--;
			uc->text[uc->cur_row][uc->cur_col] = ' ';
			uc->attr[uc->cur_row][uc->cur_col] = attr;
		}
		return;
	}
	if (ch == '\r') {
		uc->cur_col = 0;
		return;
	}
	/* 普通可打印字符 */
	uc->text[uc->cur_row][uc->cur_col] = ch;
	uc->attr[uc->cur_row][uc->cur_col] = attr;
	uc->cur_col++;
	if (uc->cur_col >= UC_COLS) {
		uc->cur_col = 0;
		uc->cur_row++;
		if (uc->cur_row >= UC_ROWS) {
			uc_scroll_up(slot);
			uc->cur_row = UC_ROWS - 1;
		}
	}
}

/* 清空控制台 */
PUBLIC void wm_uc_clear(int slot)
{
	struct USER_CONSOLE *uc;
	int r, c;
	if (slot < 0 || slot >= NR_USER_PROCS) return;
	uc = &user_consoles[slot];
	if (!uc->active) return;
	for (r = 0; r < UC_ROWS; r++) {
		for (c = 0; c < UC_COLS; c++) {
			uc->text[r][c] = ' ';
			uc->attr[r][c] = 0x0F;
		}
	}
	uc->cur_row = 0;
	uc->cur_col = 0;
}

/* 向键盘队列推入按键 */
PUBLIC void wm_uc_push_key(int slot, t_32 key)
{
	struct USER_CONSOLE *uc;
	if (slot < 0 || slot >= NR_USER_PROCS) return;
	uc = &user_consoles[slot];
	if (!uc->active) return;
	if (uc->key_count >= UC_KEY_QSZ) return;  /* 队列满, 丢弃 */
	uc->key_queue[uc->key_tail] = key;
	uc->key_tail = (uc->key_tail + 1) % UC_KEY_QSZ;
	uc->key_count++;
}

/* 从键盘队列弹出按键 (0=无按键) */
PUBLIC t_32 wm_uc_pop_key(int slot)
{
	struct USER_CONSOLE *uc;
	t_32 key;
	if (slot < 0 || slot >= NR_USER_PROCS) return 0;
	uc = &user_consoles[slot];
	if (!uc->active || uc->key_count == 0) return 0;
	key = uc->key_queue[uc->key_head];
	uc->key_head = (uc->key_head + 1) % UC_KEY_QSZ;
	uc->key_count--;
	return key;
}

/* 获取控制台关联的窗口索引 */
PUBLIC int wm_uc_get_win_idx(int slot)
{
	if (slot < 0 || slot >= NR_USER_PROCS) return -1;
	if (!user_consoles[slot].active) return -1;
	return user_consoles[slot].win_idx;
}

/* 标记控制台窗口需要重绘 */
PUBLIC void wm_uc_invalidate(int slot)
{
	if (slot < 0 || slot >= NR_USER_PROCS) return;
	if (!user_consoles[slot].active) return;
	wm_invalidate(user_consoles[slot].win_idx);
}


/*======================================================================*
                      用户自定义窗口操作函数
 *======================================================================*/
/* 找空闲的用户窗口槽位, 返回索引 (-1=已满) */
PRIVATE int uw_find_free(void)
{
	int i;
	for (i = 0; i < UW_MAX; i++) {
		if (!user_windows[i].active) return i;
	}
	return -1;
}

/* 创建用户窗口, 返回 UW 槽位索引 (>=0) 或 -1 (失败) */
PUBLIC int wm_uw_create(int slot, int x, int y, int w, int h, const char *title)
{
	int uw_idx;
	int win_idx;
	struct USER_WINDOW *uw;
	char default_title[] = "App";

	if (w <= 0 || h <= 0) return -1;
	if (w > UW_MAX_W) w = UW_MAX_W;
	if (h > UW_MAX_H) h = UW_MAX_H;

	uw_idx = uw_find_free();
	if (uw_idx < 0) return -1;
	uw = &user_windows[uw_idx];

	/* 创建 WM 窗口 (含标题栏边框) */
	win_idx = wm_create(x, y, w + 2 * WM_BORDER_3D,
	                    h + WM_TITLE_H + WM_BORDER_3D,
	                    title ? title : default_title);
	if (win_idx < 0) return -1;

	windows[win_idx].type = WM_WT_USERWINDOW;
	windows[win_idx].uw_owner = slot;
	windows[win_idx].uw_canvas_idx = uw_idx;

	uw->active = 1;
	uw->owner_slot = slot;
	uw->win_idx = win_idx;
	uw->cw = w;
	uw->ch = h;
	uw->closable = 1;                /* 默认允许点击叉号关闭 */
	uw->ev_head = 0;
	uw->ev_tail = 0;
	uw->ev_count = 0;
	/* 清空画布为黑色 */
	{
		int i;
		for (i = 0; i < w * h; i++) uw->canvas[i] = 0x00;
	}

	wm_set_active(win_idx);
	return win_idx;  /* 返回 WM 窗口索引作为 win_id */
}

/* 关闭用户窗口 */
PUBLIC void wm_uw_close(int win_id)
{
	int uw_idx;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	user_windows[uw_idx].active = 0;
	windows[win_id].uw_canvas_idx = -1;
	windows[win_id].uw_owner = -1;
	wm_close(win_id);
}

/* 设置用户窗口是否允许点击叉号关闭 (1=允许, 0=禁止)
 * 禁止后, 点击叉号不会推送 EV_CLOSE 事件, 窗口无法通过叉号关闭
 * 应用仍可通过 win_close() 主动关闭 */
PUBLIC void wm_uw_set_closable(int win_id, int closable)
{
	int uw_idx;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	user_windows[uw_idx].closable = closable ? 1 : 0;
}

/* 查询用户窗口是否允许点击叉号关闭 (1=允许, 0=禁止) */
PUBLIC int wm_uw_get_closable(int win_id)
{
	int uw_idx;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return 0;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return 0;
	return user_windows[uw_idx].closable;
}

/* 关闭指定 slot 的所有用户自定义窗口 (供 sys_exit 调用)
 * 遍历所有窗口, 关闭 owner 为 slot 的 UW 窗口 */
PUBLIC void wm_uw_close_all_for_slot(int slot)
{
	int i;
	if (slot < 0 || slot >= NR_USER_PROCS) return;
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (windows[i].visible &&
		    windows[i].type == WM_WT_USERWINDOW &&
		    windows[i].uw_owner == slot) {
			wm_uw_close(i);
		}
	}
}

/* 在画布上绘制文本 (使用字体位图, 裁剪到画布范围) */
PUBLIC void wm_uw_draw_text(int win_id, int x, int y, const char *str, t_8 fg, t_8 bg)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	uw = &user_windows[uw_idx];
	if (!uw->active) return;
	draw_string_to_buf(uw->canvas, uw->cw, uw->ch, x, y, str, fg, bg);
}

/* 在画布上绘制实心矩形 */
PUBLIC void wm_uw_draw_rect(int win_id, int x, int y, int w, int h, t_8 color)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	int r, c;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	uw = &user_windows[uw_idx];
	if (!uw->active) return;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > uw->cw) w = uw->cw - x;
	if (y + h > uw->ch) h = uw->ch - y;
	if (w <= 0 || h <= 0) return;
	for (r = 0; r < h; r++) {
		for (c = 0; c < w; c++) {
			uw->canvas[(y + r) * uw->cw + (x + c)] = color;
		}
	}
}

/* 清空画布 */
PUBLIC void wm_uw_clear(int win_id, t_8 color)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	int i;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	uw = &user_windows[uw_idx];
	if (!uw->active) return;
	for (i = 0; i < uw->cw * uw->ch; i++) uw->canvas[i] = color;
}

/* 标记用户窗口重绘 */
PUBLIC void wm_uw_invalidate(int win_id)
{
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	wm_invalidate(win_id);
}

/* 推入事件 */
PUBLIC void wm_uw_push_event(int win_id, int ev_type, int ev_x, int ev_y)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	uw = &user_windows[uw_idx];
	if (!uw->active) return;
	if (uw->ev_count >= UW_EV_QSZ) return;  /* 队列满 */
	uw->ev_queue[uw->ev_tail * 3]     = ev_type;
	uw->ev_queue[uw->ev_tail * 3 + 1] = ev_x;
	uw->ev_queue[uw->ev_tail * 3 + 2] = ev_y;
	uw->ev_tail = (uw->ev_tail + 1) % UW_EV_QSZ;
	uw->ev_count++;
}

/* 弹出事件, 返回事件类型 (0=无), 坐标写入 *px/*py */
PUBLIC int wm_uw_pop_event(int win_id, int *px, int *py)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	int ev_type;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return 0;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return 0;
	uw = &user_windows[uw_idx];
	if (!uw->active || uw->ev_count == 0) return 0;
	ev_type = uw->ev_queue[uw->ev_head * 3];
	if (px) *px = uw->ev_queue[uw->ev_head * 3 + 1];
	if (py) *py = uw->ev_queue[uw->ev_head * 3 + 2];
	uw->ev_head = (uw->ev_head + 1) % UW_EV_QSZ;
	uw->ev_count--;
	return ev_type;
}

/* 设置画布上单个像素 */
PUBLIC void wm_uw_set_pixel(int win_id, int x, int y, t_8 color)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	uw = &user_windows[uw_idx];
	if (!uw->active) return;
	if (x < 0 || y < 0 || x >= uw->cw || y >= uw->ch) return;
	uw->canvas[y * uw->cw + x] = color;
}

/* 在画布上画线 (Bresenham 算法) */
PUBLIC void wm_uw_draw_line(int win_id, int x1, int y1, int x2, int y2, t_8 color)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	int dx, dy, sx, sy, err, e2;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	uw = &user_windows[uw_idx];
	if (!uw->active) return;
	dx = (x2 >= x1) ? (x2 - x1) : (x1 - x2);
	dy = (y2 >= y1) ? (y1 - y2) : (y2 - y1);
	sx = (x1 < x2) ? 1 : -1;
	sy = (y1 < y2) ? 1 : -1;
	err = dx + dy;
	while (1) {
		if (x1 >= 0 && y1 >= 0 && x1 < uw->cw && y1 < uw->ch) {
			uw->canvas[y1 * uw->cw + x1] = color;
		}
		if (x1 == x2 && y1 == y2) break;
		e2 = 2 * err;
		if (e2 >= dy) { err += dy; x1 += sx; }
		if (e2 <= dx) { err += dx; y1 += sy; }
	}
}

/* 设置窗口标题 */
PUBLIC void wm_uw_set_title(int win_id, const char *title)
{
	int i;
	WINDOW *win;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) return;
	if (!title) return;
	win = &windows[win_id];
	for (i = 0; i < 31 && title[i]; i++) win->title[i] = title[i];
	win->title[i] = 0;
	wm_invalidate(win_id);
}

/* 获取用户窗口客户区尺寸 (宽高写入 *pw/*ph) */
PUBLIC void wm_uw_get_size(int win_id, int *pw, int *ph)
{
	int uw_idx;
	struct USER_WINDOW *uw;
	if (win_id < 0 || win_id >= WM_MAX_WINDOWS) { *pw = 0; *ph = 0; return; }
	uw_idx = windows[win_id].uw_canvas_idx;
	if (uw_idx < 0 || uw_idx >= UW_MAX) { *pw = 0; *ph = 0; return; }
	uw = &user_windows[uw_idx];
	if (!uw->active) { *pw = 0; *ph = 0; return; }
	*pw = uw->cw;
	*ph = uw->ch;
}


/*======================================================================*
                          内部辅助: 按钮几何
 *======================================================================*/
/* 计算三个按钮的 x 坐标 (左上角)
   顺序: [最大化] [最小化] [关闭]  (从左到右)
   布局: 标题栏右侧, 按钮间距 WM_BTN_GAP */
PRIVATE void wm_get_btn_rect(int idx, int* max_x, int* min_x, int* close_x)
{
	WINDOW* win = &windows[idx];
	int right = win->x + win->w - WM_BORDER_3D;   /* 标题栏右边界 */
	int y = win->y + (WM_TITLE_H - WM_BTN_H) / 2;
	*close_x = right - WM_BTN_W;
	*min_x   = *close_x - WM_BTN_W - WM_BTN_GAP;
	*max_x   = *min_x - WM_BTN_W - WM_BTN_GAP;
	(void)y;
}

/* 判断像素坐标 (px,py) 是否在窗口 idx 的标题栏按钮区域内
   返回: 0=无, 1=最大化, 2=最小化, 3=关闭
   注意: 最大化状态下按钮仍可点击 (用于还原) */
PRIVATE int wm_hit_test_btn(int idx, int px, int py)
{
	WINDOW* win;
	int max_x, min_x, close_x;
	int btn_y, btn_y2;
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return 0;
	win = &windows[idx];
	if (!win->visible || win->state == WM_WS_MINIMIZED) return 0;
	wm_get_btn_rect(idx, &max_x, &min_x, &close_x);
	btn_y  = win->y + (WM_TITLE_H - WM_BTN_H) / 2;
	btn_y2 = btn_y + WM_BTN_H;
	if (py < btn_y || py >= btn_y2) return 0;
	if (px >= max_x   && px < max_x   + WM_BTN_W) return 1;
	if (px >= min_x   && px < min_x   + WM_BTN_W) return 2;
	if (px >= close_x && px < close_x + WM_BTN_W) return 3;
	return 0;
}

/* 判断 (px,py) 是否在标题栏区域 (用于拖拽)
   最大化状态下禁用拖拽 (窗口已铺满屏幕) */
PRIVATE int wm_hit_test_title(int idx, int px, int py)
{
	WINDOW* win;
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return 0;
	win = &windows[idx];
	if (!win->visible || win->state != WM_WS_NORMAL) return 0;
	if (px < win->x || px >= win->x + win->w) return 0;
	if (py < win->y || py >= win->y + WM_TITLE_H) return 0;
	/* 排除按钮区域 */
	if (wm_hit_test_btn(idx, px, py)) return 0;
	return 1;
}

/* 判断 (px,py) 是否在窗口 idx 的可见区域内 */
PRIVATE int wm_hit_test_window(int idx, int px, int py)
{
	WINDOW* win;
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return 0;
	win = &windows[idx];
	if (!win->visible || win->state == WM_WS_MINIMIZED) return 0;
	if (px < win->x || px >= win->x + win->w) return 0;
	if (py < win->y || py >= win->y + win->h) return 0;
	return 1;
}

/* 从顶层到底层找第一个包含 (px,py) 的可见窗口, 返回索引 (-1=无) */
PRIVATE int wm_find_window_at(int px, int py)
{
	int i;
	if (wm_active >= 0 && wm_hit_test_window(wm_active, px, py)) {
		return wm_active;
	}
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (i == wm_active) continue;
		if (wm_hit_test_window(i, px, py)) return i;
	}
	return -1;
}


/*======================================================================*
                          文件管理器辅助函数
 *======================================================================*/

/* 将 FAT_DIR_ENTRY 格式化为 "NAME.EXT" 字符串 (文件夹无扩展名) */
PRIVATE void fm_format_name(FAT_DIR_ENTRY *de, char *out)
{
	int i, j = 0;
	for (i = 0; i < 8 && de->name[i] != ' '; i++) out[j++] = de->name[i];
	if (de->ext[0] != ' ') {
		out[j++] = '.';
		for (i = 0; i < 3 && de->ext[i] != ' '; i++) out[j++] = de->ext[i];
	}
	out[j] = 0;
}

/* 绘制回调: 画出每个目录条目 (跳过 ".") */
PRIVATE void fm_draw_cb(FAT_DIR_ENTRY *de)
{
	int row_h = WM_FM_ROW_H;
	char name[16];
	int is_dir;
	int x, y;
	t_8 bg, fg, icon_col;

	if (de->name[0] == '.' && (de->name[1] == ' ' || de->name[1] == '.')) return;

	y = fm_draw_y;
	if (y + row_h <= fm_draw_max_y) {
		x = fm_draw_client_x + 2;
		is_dir = (de->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
		fm_format_name(de, name);

		if (fm_draw_idx == fm_draw_selected) {
			fill_rect(fm_draw_client_x, y, fm_draw_client_w, row_h,
				WM_COL_FM_SELECTED);
			bg = WM_COL_FM_SELECTED;
			fg = WM_COL_FM_SEL_TEXT;
		} else {
			bg = WM_COL_CLIENT;
			fg = is_dir ? WM_COL_FM_DIR_TEXT : WM_COL_FM_FILE_TEXT;
		}
		icon_col = is_dir ? WM_COL_FM_DIR_ICON : WM_COL_FM_FILE_ICON;

		fill_rect(x, y + (row_h - WM_FM_ICON_H) / 2,
			WM_FM_ICON_W, WM_FM_ICON_H, icon_col);

		draw_string(x + WM_FM_ICON_W + 3,
			y + (row_h - g_font_height) / 2, name, fg, bg);
	}

	fm_draw_y += row_h;
	fm_draw_idx++;
}

/* 查找回调: 找到指定索引的条目, 同时保存文件名和扩展名 */
PRIVATE void fm_lookup_cb(FAT_DIR_ENTRY *de)
{
	if (de->name[0] == '.' && (de->name[1] == ' ' || de->name[1] == '.')) return;

	if (fm_lookup_current == fm_lookup_target) {
		int i, j = 0;
		fm_lookup_cluster = fat_get_start_cluster_pub(de);
		fm_lookup_is_dir = (de->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
		/* 保存 8.3 文件名 */
		for (i = 0; i < 8 && de->name[i] != ' '; i++) {
			fm_lookup_name[j++] = de->name[i];
		}
		if (de->ext[0] != ' ') {
			fm_lookup_name[j++] = '.';
			for (i = 0; i < 3 && de->ext[i] != ' '; i++) {
				fm_lookup_name[j++] = de->ext[i];
			}
		}
		fm_lookup_name[j] = 0;
		/* 保存小写扩展名 */
		fm_lookup_ext[0] = (de->ext[0] >= 'A' && de->ext[0] <= 'Z') ? de->ext[0]+32 : de->ext[0];
		fm_lookup_ext[1] = (de->ext[1] >= 'A' && de->ext[1] <= 'Z') ? de->ext[1]+32 : de->ext[1];
		fm_lookup_ext[2] = (de->ext[2] >= 'A' && de->ext[2] <= 'Z') ? de->ext[2]+32 : de->ext[2];
		fm_lookup_ext[3] = 0;
	}
	fm_lookup_current++;
}

/* 绘制文件管理器客户区: 文件列表 */
PRIVATE void wm_draw_fm_client(int idx)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;

	/* 确保 FAT 文件系统已初始化 (setup 后 fat_initialized 可能被重置) */
	if (fat_init() != 0) {
		win->fm_count = 0;
		return;
	}

	fm_draw_y = cy + 2;
	fm_draw_idx = 0;
	fm_draw_max_y = cy + ch;
	fm_draw_selected = win->fm_selected;
	fm_draw_client_x = cx;
	fm_draw_client_w = cw;

	fat_list_dir(win->fm_dir_cluster, fm_draw_cb);

	win->fm_count = fm_draw_idx;
}

/* 文件管理器行命中测试, 返回行索引 (-1=无) */
PRIVATE int fm_hit_test_row(int idx, int px, int py)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int row;
	if (px < cx || px >= cx + cw) return -1;
	if (py < cy + 2) return -1;
	row = (py - cy - 2) / WM_FM_ROW_H;
	if (row < 0 || row >= win->fm_count) return -1;
	return row;
}

/* 判断扩展名是否为指定值 */
PRIVATE int ext_is(const char *ext, const char *target)
{
	int i;
	for (i = 0; i < 3; i++) {
		if (target[i] == 0) return (ext[i] == 0 || ext[i] == ' ');
		if (ext[i] != target[i]) return 0;
	}
	return target[3] == 0;
}

/* 打开文件管理器中的条目 (双击):
   文件夹 → 新文件管理器窗口
   txt/log/c 文件 → 记事本编辑器
   其他文件 → "打开方式" 选择弹窗 */
PRIVATE void fm_open_entry(int win_idx, int entry_idx)
{
	fm_lookup_target = entry_idx;
	fm_lookup_current = 0;
	fm_lookup_cluster = 0;
	fm_lookup_is_dir = 0;
	fm_lookup_name[0] = 0;
	fm_lookup_ext[0] = 0;
	fat_list_dir(windows[win_idx].fm_dir_cluster, fm_lookup_cb);

	if (fm_lookup_is_dir) {
		wm_create_fm(fm_lookup_cluster);
	} else {
		/* 文件所在目录的簇号 (用于编辑器/打开方式按目录读写文件) */
		t_32 dir_cluster = windows[win_idx].fm_dir_cluster;
		/* .ce 可执行文件 → 直接运行 (并发, 自动分配控制台窗口) */
		if (ext_is(fm_lookup_ext, "ce") && user_proc_free_slots > 0) {
			exec_user_program_in(dir_cluster, fm_lookup_name);
		} else if (ext_is(fm_lookup_ext, "txt") || ext_is(fm_lookup_ext, "log") ||
		    ext_is(fm_lookup_ext, "c\0") || ext_is(fm_lookup_ext, "h\0")) {
			/* txt/log/c → 编辑器 */
			wm_create_editor(fm_lookup_name, dir_cluster);
		} else {
			/* 未知扩展名 → "打开方式" 弹窗 */
			wm_create_openwith(fm_lookup_name, dir_cluster);
		}
	}
}


/*======================================================================*
                          绘制: 窗口阴影 / 3D 边框
 *======================================================================*/

/* 绘制窗口阴影 (右下偏移 3px, 半透明感用深灰) */
PRIVATE void wm_draw_shadow(int x, int y, int w, int h)
{
	int sx = x + 4;
	int sy = y + 4;
	int sw = w;
	int sh = h;

	fill_rect(sx, sy, sw, 2, WM_COL_SHADOW);
	fill_rect(sx, sy + 2, 2, sh - 2, WM_COL_SHADOW);
	fill_rect(sx + 2, sy + sh - 2, sw - 2, 2, WM_COL_SHADOW);
	fill_rect(sx + sw - 2, sy + 2, 2, sh - 2, WM_COL_SHADOW);
}

/* 绘制 3D 窗口边框 (raised 风格: 左上亮, 右下暗) */
PRIVATE void wm_draw_3d_border(int x, int y, int w, int h, int active)
{
	t_8 light = WM_COL_3D_HIGHLIGHT;
	t_8 dark = active ? 0x08 : 0x08;

	/* 外边框 */
	draw_hline(x, x + w - 1, y, light);
	draw_vline(x, y + 1, y + h - 1, light);
	draw_hline(x, x + w - 1, y + h - 1, dark);
	draw_vline(x + w - 1, y + 1, y + h - 1, dark);

	/* 内边框 (加深 3D 效果) */
	if (active) {
		draw_hline(x + 1, x + w - 2, y + 1, 0x0F);
		draw_vline(x + 1, y + 2, y + h - 2, 0x0F);
		draw_hline(x + 1, x + w - 2, y + h - 2, 0x00);
		draw_vline(x + w - 2, y + 2, y + h - 2, 0x00);
	} else {
		draw_hline(x + 1, x + w - 2, y + 1, 0x07);
		draw_vline(x + 1, y + 2, y + h - 2, 0x07);
		draw_hline(x + 1, x + w - 2, y + h - 2, 0x00);
		draw_vline(x + w - 2, y + 2, y + h - 2, 0x00);
	}
}


/*======================================================================*
                          绘制: 大图标 (MainWindow)
 *======================================================================*/

/* 绘制一个大图标 (48x48 + 下方文字)
   app_id: WM_APP_* 常量
   x, y: 图标单元格左上角
   hovered: 是否悬停 (高亮) */
PRIVATE void wm_draw_large_icon(int app_id, int x, int y, int hovered)
{
	int cx = x + (WM_ICON_CELL_W - WM_ICON_LARGE_W) / 2;
	int cy = y + 2;
	/* 缩放因子: 基础字体 8px 时 s=1, 16px 时 s=2, 按比例缩放图标内部元素 */
	int s = g_font_width / 8;
	if (s < 1) s = 1;

	t_8 icon_bg = hovered ? 0x0E : WM_COL_ICON_BG;
	t_8 icon_border = hovered ? 0x0F : WM_COL_ICON_BORDER;

	fill_rect(cx, cy, WM_ICON_LARGE_W, WM_ICON_LARGE_H, icon_bg);
	draw_hline(cx, cx + WM_ICON_LARGE_W - 1, cy, icon_border);
	draw_hline(cx, cx + WM_ICON_LARGE_W - 1, cy + WM_ICON_LARGE_H - 1, icon_border);
	draw_vline(cx, cy, cy + WM_ICON_LARGE_H - 1, icon_border);
	draw_vline(cx + WM_ICON_LARGE_W - 1, cy, cy + WM_ICON_LARGE_H - 1, icon_border);

	switch (app_id) {
	case WM_APP_FILEMANAGER: {
		fill_rect(cx + 8*s, cy + 4*s, 20*s, 4*s, 0x0E);
		fill_rect(cx + 6*s, cy + 8*s, WM_ICON_LARGE_W - 12*s, WM_ICON_LARGE_H - 12*s, 0x0E);
		draw_hline(cx + 8*s, cx + 27*s, cy + 4*s, 0x06);
		draw_hline(cx + 8*s, cx + 27*s, cy + 7*s, 0x06);
		draw_vline(cx + 8*s, cy + 4*s, cy + 7*s, 0x06);
		draw_vline(cx + 27*s, cy + 4*s, cy + 7*s, 0x06);
		draw_hline(cx + 6*s, cx + WM_ICON_LARGE_W - 7*s, cy + 8*s, 0x06);
		draw_hline(cx + 6*s, cx + WM_ICON_LARGE_W - 7*s, cy + WM_ICON_LARGE_H - 5*s, 0x06);
		draw_vline(cx + 6*s, cy + 8*s, cy + WM_ICON_LARGE_H - 5*s, 0x06);
		draw_vline(cx + WM_ICON_LARGE_W - 7*s, cy + 8*s, cy + WM_ICON_LARGE_H - 5*s, 0x06);
		break;
	}
	case WM_APP_CALCULATOR: {
		int ci, cj;
		fill_rect(cx + 4*s, cy + 4*s, WM_ICON_LARGE_W - 8*s, 10*s, 0x07);
		fill_rect(cx + 6*s, cy + 6*s, WM_ICON_LARGE_W - 12*s, 6*s, 0x0F);
		for (ci = 0; ci < 4; ci++) {
			for (cj = 0; cj < 4; cj++) {
				int bx = cx + 4*s + cj * 10*s;
				int by = cy + 18*s + ci * 7*s;
				fill_rect(bx, by, 8*s, 5*s, 0x08);
				fill_rect(bx + s, by + s, 6*s, 3*s, 0x07);
			}
		}
		break;
	}
	case WM_APP_TERMINAL: {
		fill_rect(cx + 4*s, cy + 4*s, WM_ICON_LARGE_W - 8*s, WM_ICON_LARGE_H - 8*s, 0x00);
		draw_hline(cx + 4*s, cx + WM_ICON_LARGE_W - 5*s, cy + 4*s, 0x07);
		draw_hline(cx + 4*s, cx + WM_ICON_LARGE_W - 5*s, cy + WM_ICON_LARGE_H - 5*s, 0x07);
		draw_vline(cx + 4*s, cy + 4*s, cy + WM_ICON_LARGE_H - 5*s, 0x07);
		draw_vline(cx + WM_ICON_LARGE_W - 5*s, cy + 4*s, cy + WM_ICON_LARGE_H - 5*s, 0x07);
		draw_char(cx + 10*s, cy + 12*s, '>', 0x0A, 0x00);
		draw_char(cx + 18*s, cy + 12*s, ' ', 0x0A, 0x00);
		draw_hline(cx + 18*s, cx + 22*s, cy + 20*s, 0x0A);
		break;
	}
	case WM_APP_ABOUT: {
		int r = WM_ICON_LARGE_W / 2 - 4*s;
		int ox = cx + WM_ICON_LARGE_W / 2;
		int oy = cy + WM_ICON_LARGE_H / 2;
		int ci;
		static const t_8 circle_half[22] = {
			20,19,18,17,16,15,13,12,10,8,5,5,8,10,12,13,15,16,17,18,19,20
		};
		for (ci = -r; ci <= r; ci++) {
			int idx = ci + r;
			int half;
			if (idx >= 0 && idx < 22) {
				half = circle_half[idx] * r / 20;
			} else {
				half = 0;
			}
			draw_hline(ox - half, ox + half, oy + ci, 0x01);
		}
		draw_hline(ox - r, ox + r, oy - r, 0x01);
		draw_hline(ox - r, ox + r, oy + r, 0x01);
		draw_vline(ox, oy - r, oy + r, 0x01);
		draw_char(ox - 2*s, oy - 6*s, 'i', 0x0F, 0x01);
		break;
	}
	}

	/* 图标下方文字 */
	{
		char *label = S_M_ABOUT;
		switch (app_id) {
		case WM_APP_FILEMANAGER: label = S_M_FILE; break;
		case WM_APP_CALCULATOR:  label = S_M_CALC; break;
		case WM_APP_TERMINAL:    label = S_M_TERM; break;
		case WM_APP_ABOUT:       label = S_M_ABOUT; break;
		}
		int text_y = cy + WM_ICON_LARGE_H + 2;
		int text_w = 0;
		char *p = label;
		while (*p++) text_w += g_font_width;
		int text_x = x + (WM_ICON_CELL_W - text_w) / 2;
		draw_string(text_x, text_y, label, WM_COL_ICON_TEXT,
			WM_COL_CLIENT);
	}
}


/*======================================================================*
                          绘制: 主窗口客户区 (大图标)
 *======================================================================*/
PRIVATE void wm_draw_main_client(int idx, int hover_app)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;

	/* 客户区背景 */
	fill_rect(cx, cy, cw, ch, WM_COL_CLIENT);

	/* 计算网格布局 */
	int cols = cw / WM_ICON_CELL_W;
	int rows = ch / WM_ICON_CELL_H;
	if (cols < 1) cols = 1;
	if (rows < 1) rows = 1;

	int total_w = cols * WM_ICON_CELL_W;
	int total_h = rows * WM_ICON_CELL_H;
	int start_x = cx + (cw - total_w) / 2;
	int start_y = cy + (ch - total_h) / 2;
	if (start_x < cx) start_x = cx;

	/* 大图标: File Manager */
	int app_ids[] = {
		WM_APP_FILEMANAGER,
		WM_APP_CALCULATOR,
		WM_APP_TERMINAL,
		WM_APP_ABOUT
	};
	int num_apps = 4;
	int a;

	for (a = 0; a < num_apps; a++) {
		int col = a % cols;
		int row = a / cols;
		int ix = start_x + col * WM_ICON_CELL_W;
		int iy = start_y + row * WM_ICON_CELL_H;
		int hovered = (a == hover_app);
		wm_draw_large_icon(app_ids[a], ix, iy, hovered);
	}
}

/* 主窗口大图标命中测试, 返回应用 ID (-1=无) */
PRIVATE int wm_hit_test_main_icon(int idx, int px, int py)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;

	int cols = cw / WM_ICON_CELL_W;
	int rows = ch / WM_ICON_CELL_H;
	if (cols < 1) cols = 1;
	if (rows < 1) rows = 1;

	int total_w = cols * WM_ICON_CELL_W;
	int total_h = rows * WM_ICON_CELL_H;
	int start_x = cx + (cw - total_w) / 2;
	int start_y = cy + (ch - total_h) / 2;
	if (start_x < cx) start_x = cx;

	int app_ids[] = {
		WM_APP_FILEMANAGER,
		WM_APP_CALCULATOR,
		WM_APP_TERMINAL,
		WM_APP_ABOUT
	};
	int num_apps = 4;
	int a;

	for (a = 0; a < num_apps; a++) {
		int col = a % cols;
		int row = a / cols;
		int ix = start_x + col * WM_ICON_CELL_W;
		int iy = start_y + row * WM_ICON_CELL_H;
		if (px >= ix && px < ix + WM_ICON_CELL_W &&
		    py >= iy && py < iy + WM_ICON_CELL_H) {
			return app_ids[a];
		}
	}
	return -1;
}


/*======================================================================*
                          绘制: 窗口图标 / 按钮
 *======================================================================*/

/* 绘制窗口图标 (左上角小方块) */
PRIVATE void wm_draw_icon(int idx)
{
	WINDOW* win = &windows[idx];
	int ix = win->x + WM_BORDER_3D + 2;
	int iy = win->y + (WM_TITLE_H - WM_ICON_H) / 2;
	fill_rect(ix, iy, WM_ICON_W, WM_ICON_H, WM_COL_ICON);
	fill_rect(ix + 2, iy + 2, WM_ICON_W - 4, WM_ICON_H - 4, 0x0F);
	draw_char(ix + 4, iy + 2, 'C', 0x0E, 0x0F);
}

/* 绘制单个按钮 (背景 + 符号)
   type: 1=最大化/还原 2=最小化 3=关闭(X)
   hovered: 是否悬停 */
PRIVATE void wm_draw_btn(int x, int y, int type, int hovered, int maximized)
{
	t_8 bg, fg;
	int cx, cy;
	int i;

	if (hovered) {
		bg = 0x0E;
		fg = 0x00;
	} else {
		bg = WM_COL_BTN;
		fg = WM_COL_BTN_TEXT;
	}

	fill_rect(x, y, WM_BTN_W, WM_BTN_H, bg);

	draw_hline(x, x + WM_BTN_W - 1, y, fg);
	draw_hline(x, x + WM_BTN_W - 1, y + WM_BTN_H - 1, fg);
	draw_vline(x, y, y + WM_BTN_H - 1, fg);
	draw_vline(x + WM_BTN_W - 1, y, y + WM_BTN_H - 1, fg);

	cx = x + WM_BTN_W / 2;
	cy = y + WM_BTN_H / 2;

	if (type == 1) {
		if (maximized) {
			draw_hline(cx - 3, cx + 2, cy - 2, fg);
			draw_hline(cx - 3, cx + 2, cy - 3, fg);
			draw_vline(cx - 3, cy - 3, cy - 2, fg);
			draw_vline(cx + 2, cy - 3, cy - 2, fg);
			draw_hline(cx - 2, cx + 3, cy + 2, fg);
			draw_hline(cx - 2, cx + 3, cy + 3, fg);
			draw_vline(cx - 2, cy, cy + 3, fg);
			draw_vline(cx + 3, cy, cy + 3, fg);
		} else {
			for (i = 0; i < 4; i++) {
				draw_hline(cx - i, cx + i, cy - 2 + i, fg);
			}
		}
	} else if (type == 2) {
		for (i = 0; i < 4; i++) {
			draw_hline(cx - i, cx + i, cy + 1 - i, fg);
		}
	} else {
		for (i = -3; i <= 3; i++) {
			put_pixel(cx + i, cy + i, fg);
			put_pixel(cx + i, cy - i, fg);
		}
	}
}

/* 绘制三个按钮 (支持悬停高亮) */
PRIVATE void wm_draw_buttons(int idx, int hover_btn)
{
	int max_x, min_x, close_x;
	int y;
	int maximized = (windows[idx].state == WM_WS_MAXIMIZED);
	wm_get_btn_rect(idx, &max_x, &min_x, &close_x);
	y = windows[idx].y + (WM_TITLE_H - WM_BTN_H) / 2;
	wm_draw_btn(max_x,   y, 1, hover_btn == 1, maximized);
	wm_draw_btn(min_x,   y, 2, hover_btn == 2, 0);
	wm_draw_btn(close_x, y, 3, hover_btn == 3, 0);
}


/*======================================================================*
                          绘制: 任务栏
 *======================================================================*/
PRIVATE void wm_draw_taskbar(int hover_idx)
{
	int bar_y = g_screen_height - WM_TASKBAR_H;
	int i;
	int slot_x = 52;
	int slot_w = 80;

	fill_rect(0, bar_y, g_screen_width, WM_TASKBAR_H, WM_COL_TASKBAR);
	draw_hline(0, g_screen_width - 1, bar_y, 0x08);
	draw_hline(0, g_screen_width - 1, bar_y + 1, 0x0F);

	draw_string(4, bar_y + (WM_TASKBAR_H - g_font_height) / 2,
		"CatOS", 0x00, WM_COL_TASKBAR);

	/* 最小化窗口列表 */
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (!windows[i].visible) continue;
		if (windows[i].state != WM_WS_MINIMIZED) continue;
		t_8 bg = (i == hover_idx) ? 0x09 : WM_COL_BTN;
		t_8 fg = (i == hover_idx) ? 0x0F : 0x00;
		fill_rect(slot_x, bar_y + 2, slot_w, WM_TASKBAR_H - 4, bg);
		draw_hline(slot_x, slot_x + slot_w - 1, bar_y + 2, 0x00);
		draw_hline(slot_x, slot_x + slot_w - 1, bar_y + WM_TASKBAR_H - 3, 0x00);
		draw_vline(slot_x, bar_y + 2, bar_y + WM_TASKBAR_H - 3, 0x00);
		draw_vline(slot_x + slot_w - 1, bar_y + 2, bar_y + WM_TASKBAR_H - 3, 0x00);
		{
			int tx = slot_x + 4;
			int ty = bar_y + (WM_TASKBAR_H - g_font_height) / 2;
			int max_chars = (slot_w - 8) / g_font_width;
			int k;
			for (k = 0; k < max_chars && windows[i].title[k]; k++) {
				draw_char(tx + k * g_font_width, ty,
					windows[i].title[k], fg, bg);
			}
		}
		slot_x += slot_w + 2;
	}
}

/* 判断 (px,py) 是否命中任务栏中的最小化窗口, 返回索引 (-1=无) */
PRIVATE int wm_hit_test_taskbar(int px, int py)
{
	int bar_y = g_screen_height - WM_TASKBAR_H;
	int i;
	int slot_x = 52;
	int slot_w = 80;
	if (py < bar_y || py >= g_screen_height) return -1;
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (!windows[i].visible) continue;
		if (windows[i].state != WM_WS_MINIMIZED) continue;
		if (px >= slot_x && px < slot_x + slot_w) return i;
		slot_x += slot_w + 2;
	}
	return -1;
}


/*======================================================================*
                          wm_draw_window (增强版)
 *======================================================================*/
PUBLIC void wm_draw_window(int idx)
{
	WINDOW* win;
	int title_y, client_x, client_y, client_w, client_h;
	int is_active;

	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	win = &windows[idx];
	if (!win->visible || win->state == WM_WS_MINIMIZED) return;

	is_active = (idx == wm_active);

	if (win->state == WM_WS_MAXIMIZED) {
		/* 最大化尺寸已由 wm_maximize 一次性设置, 这里仅兜底 */
		if (win->w != g_screen_width ||
		    win->h != g_screen_height - WM_TASKBAR_H) {
			win->x = 0;
			win->y = 0;
			win->w = g_screen_width;
			win->h = g_screen_height - WM_TASKBAR_H;
		}
	}

	/* 1. 窗口阴影 (仅正常状态) */
	if (win->state == WM_WS_NORMAL) {
		wm_draw_shadow(win->x, win->y, win->w, win->h);
	}

	/* 2. 客户区背景 */
	client_x = win->x + WM_BORDER_3D;
	client_y = win->y + WM_TITLE_H;
	client_w = win->w - 2 * WM_BORDER_3D;
	client_h = win->h - WM_TITLE_H - WM_BORDER_3D;
	fill_rect(client_x, client_y, client_w, client_h, WM_COL_CLIENT);

	/* 3. 标题栏背景 (渐变效果: 上部亮色, 下部暗色) */
	title_y = win->y;
	if (is_active) {
		fill_rect(win->x, title_y, win->w, WM_TITLE_H / 2, WM_COL_TITLE_ACCENT);
		fill_rect(win->x, title_y + WM_TITLE_H / 2, win->w,
			WM_TITLE_H - WM_TITLE_H / 2, WM_COL_TITLE_ACT);
	} else {
		fill_rect(win->x, title_y, win->w, WM_TITLE_H, WM_COL_TITLE_INACT);
	}

	/* 4. 图标 (左上角) */
	wm_draw_icon(idx);

	/* 5. 标题文字 (居中) */
	{
		int text_w = 0;
		int text_x;
		int text_y = title_y + (WM_TITLE_H - g_font_height) / 2;
		const char* s = win->title;
		while (*s++) text_w += g_font_width;
		text_x = win->x + (win->w - text_w) / 2;
		draw_string(text_x, text_y, win->title,
			WM_COL_TITLE_TEXT,
			is_active ? WM_COL_TITLE_ACCENT : WM_COL_TITLE_INACT);
	}

	/* 6. 三个按钮 (右上角) */
	wm_draw_buttons(idx, 0);

	/* 7. 3D 边框 */
	wm_draw_3d_border(win->x, win->y, win->w, win->h, is_active);

	/* 8. 标题栏与客户区分隔线 */
	draw_hline(win->x + 1, win->x + win->w - 2,
		win->y + WM_TITLE_H - 1, 0x00);

	/* 9. 客户区内容 (按窗口类型) */
	if (win->type == WM_WT_MAIN) {
		wm_draw_main_client(idx, -1);
	} else if (win->type == WM_WT_FILEMANAGER) {
		wm_draw_fm_client(idx);
	} else if (win->type == WM_WT_CALCULATOR) {
		wm_draw_calc_client(idx);
	} else if (win->type == WM_WT_ABOUT) {
		wm_draw_about_client(idx);
	} else if (win->type == WM_WT_TERMINAL) {
		wm_draw_term_client(idx);
	} else if (win->type == WM_WT_EDITOR) {
		wm_draw_ed_client(idx);
	} else if (win->type == WM_WT_OPENWITH) {
		wm_draw_ow_client(idx);
	} else if (win->type == WM_WT_USERCONSOLE) {
		wm_draw_uc_client(idx);
	} else if (win->type == WM_WT_USERWINDOW) {
		wm_draw_uw_client(idx);
	}
}


/*======================================================================*
                          wm_invalidate  (标记单个窗口脏)
 *======================================================================*/
PUBLIC void wm_invalidate(int idx)
{
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	windows[idx].dirty = 1;
}


/*======================================================================*
                          wm_redraw_all  (全量重绘, 强制清 dirty)
 *======================================================================*/
/* 全量重绘: 清屏 + 重画所有 visible 窗口 + 任务栏.
 * 用于窗口创建/关闭/激活切换/最大化等结构性变化.
 * 频繁的局部更新 (终端输入/编辑器输入/拖拽) 应使用 wm_draw_window_only. */
PUBLIC void wm_redraw_all(void)
{
	int i;

	gfx_hide_mouse_cursor();

	/* 1. 桌面背景 */
	fill_rect(0, 0, g_screen_width,
		g_screen_height - WM_TASKBAR_H, WM_COL_DESKTOP);

	/* 2. 按 Z-order 绘制 (非活动窗口在前, 活动窗口最后) */
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (windows[i].visible && i != wm_active) {
			wm_draw_window(i);
		}
	}
	if (wm_active >= 0 && windows[wm_active].visible) {
		wm_draw_window(wm_active);
	}

	/* 3. 任务栏 */
	wm_draw_taskbar(-1);

	/* 4. 清除所有窗口的 dirty 标志 */
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		windows[i].dirty = 0;
	}
}


/*======================================================================*
                          wm_draw_window_only  (单窗口局部重绘)
 *======================================================================*/
/* 只重绘指定窗口: 恢复该窗口覆盖的区域 (桌面+底层窗口), 然后画该窗口.
 * 用于终端/编辑器等高频局部更新场景, 避免全屏清空.
 * 注意: 此函数不处理其他窗口的 dirty 状态, 调用者需确保被遮挡的
 * 底层窗口在该窗口移动后通过 wm_redraw_all 全量更新. */
PUBLIC void wm_draw_window_only(int idx)
{
	WINDOW* win;
	int bx, by, bw, bh;

	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	win = &windows[idx];
	if (!win->visible || win->state == WM_WS_MINIMIZED) return;

	gfx_hide_mouse_cursor();

	/* 1. 恢复该窗口覆盖的区域: 桌面背景 + 重叠的底层窗口.
	 * 简化实现: 用桌面背景填充窗口的包围盒 (含阴影),
	 * 然后重画所有与该区域重叠的非活动窗口, 最后重画该窗口. */
	bx = win->x - 1;
	by = win->y - 1;
	bw = win->w + 8;  /* 含右下阴影 4px + 1px 余量 */
	bh = win->h + 8;
	if (bx < 0) bx = 0;
	if (by < 0) by = 0;
	if (bx + bw > g_screen_width) bw = g_screen_width - bx;
	if (by + bh > g_screen_height - WM_TASKBAR_H) {
		bh = g_screen_height - WM_TASKBAR_H - by;
	}
	fill_rect(bx, by, bw, bh, WM_COL_DESKTOP);

	/* 2. 重画所有与该区域重叠的非活动可见窗口 (保持 Z-order) */
	{
		int i;
		for (i = 0; i < WM_MAX_WINDOWS; i++) {
			WINDOW* w = &windows[i];
			if (i == idx) continue;
			if (!w->visible || w->state == WM_WS_MINIMIZED) continue;
			/* 简单的重叠测试 */
			if (w->x < bx + bw && w->x + w->w > bx &&
			    w->y < by + bh && w->y + w->h > by) {
				wm_draw_window(i);
			}
		}
	}

	/* 3. 重画目标窗口 */
	wm_draw_window(idx);

	/* 4. 清除该窗口的 dirty 标志 */
	win->dirty = 0;
}


/*======================================================================*
                          wm_set_active
 *======================================================================*/
PUBLIC void wm_set_active(int idx)
{
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	if (!windows[idx].visible) return;
	if (windows[idx].state == WM_WS_MINIMIZED) {
		windows[idx].state = WM_WS_NORMAL;
	}
	wm_active = idx;
	wm_redraw_all();
}


/*======================================================================*
                          wm_get_active
 *======================================================================*/
PUBLIC int wm_get_active(void)
{
	return wm_active;
}


/*======================================================================*
                          wm_move_window  (局部重绘优化版)
 *======================================================================*/
/* 移动窗口: 只重绘旧位置 + 新位置覆盖的区域, 避免全屏清空.
 * 拖拽时每像素移动只影响两个矩形区域, 大幅减少重绘开销. */
PUBLIC void wm_move_window(int idx, int dx, int dy)
{
	WINDOW* win;
	int old_x, old_y, old_w, old_h;
	int new_x, new_y;
	int rx, ry, rw, rh;

	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	win = &windows[idx];
	if (!win->visible || win->state != WM_WS_NORMAL) return;
	if (dx == 0 && dy == 0) return;

	/* 记录旧位置 (含阴影范围) */
	old_x = win->x;
	old_y = win->y;
	old_w = win->w;
	old_h = win->h;

	/* 计算新位置 (含边界裁剪) */
	new_x = win->x + dx;
	new_y = win->y + dy;
	if (new_x < -win->w + 30) new_x = -win->w + 30;
	if (new_y < 0) new_y = 0;
	if (new_x + 30 > g_screen_width) new_x = g_screen_width - 30;
	if (new_y + win->h > g_screen_height - WM_TASKBAR_H)
		new_y = g_screen_height - WM_TASKBAR_H - win->h;

	win->x = new_x;
	win->y = new_y;

	gfx_hide_mouse_cursor();

	/* 计算需要重绘的包围矩形: 旧位置 ∪ 新位置 (含阴影 4px) */
	rx = (new_x < old_x) ? new_x : old_x;
	ry = (new_y < old_y) ? new_y : old_y;
	rw = ((new_x + win->w > old_x + old_w) ? new_x + win->w : old_x + old_w) - rx + 8;
	rh = ((new_y + win->h > old_y + old_h) ? new_y + win->h : old_y + old_h) - ry + 8;
	if (rx < 0) { rw += rx; rx = 0; }
	if (ry < 0) { rh += ry; ry = 0; }
	if (rx + rw > g_screen_width) rw = g_screen_width - rx;
	if (ry + rh > g_screen_height - WM_TASKBAR_H) {
		rh = g_screen_height - WM_TASKBAR_H - ry;
	}

	/* 1. 用桌面背景清除受影响区域 */
	fill_rect(rx, ry, rw, rh, WM_COL_DESKTOP);

	/* 2. 重画该区域内的所有可见窗口 (保持 Z-order: 非活动在前, 活动在后) */
	{
		int i;
		for (i = 0; i < WM_MAX_WINDOWS; i++) {
			WINDOW* w = &windows[i];
			if (!w->visible || w->state == WM_WS_MINIMIZED) continue;
			if (w->x < rx + rw && w->x + w->w > rx &&
			    w->y < ry + rh && w->y + w->h > ry) {
				if (i != wm_active) wm_draw_window(i);
			}
		}
		if (wm_active >= 0 && wm_active != idx) {
			WINDOW* w = &windows[wm_active];
			if (w->visible && w->state != WM_WS_MINIMIZED &&
			    w->x < rx + rw && w->x + w->w > rx &&
			    w->y < ry + rh && w->y + w->h > ry) {
				wm_draw_window(wm_active);
			}
		}
		/* 最后画被移动的窗口 (如果是活动窗口) */
		if (idx == wm_active) {
			wm_draw_window(idx);
		}
	}
}


/*======================================================================*
                          wm_draw_drag_box  (拖拽橡皮筋边框)
 *======================================================================*/
/* 绘制 2 像素宽的高对比边框轮廓 (拖动期间代替完整窗口显示).
 * 调用者负责在画新边框前用 fill_rect 桌面背景色擦除旧边框区域. */
PRIVATE void wm_draw_drag_box(int x, int y, int w, int h)
{
	if (w < 2 || h < 2) return;
	/* 外框 (1像素白线) */
	draw_hline(x, x + w - 1, y,             WM_COL_BORDER);
	draw_hline(x, x + w - 1, y + h - 1,     WM_COL_BORDER);
	draw_vline(x,             y, y + h - 1, WM_COL_BORDER);
	draw_vline(x + w - 1,     y, y + h - 1, WM_COL_BORDER);
	/* 内框 (2像素, 增强可见性) */
	if (w > 4 && h > 4) {
		draw_hline(x + 1, x + w - 2, y + 1,         WM_COL_BORDER);
		draw_hline(x + 1, x + w - 2, y + h - 2,     WM_COL_BORDER);
		draw_vline(x + 1,         y + 1, y + h - 2, WM_COL_BORDER);
		draw_vline(x + w - 2,     y + 1, y + h - 2, WM_COL_BORDER);
	}
}


/*======================================================================*
                          wm_init
 *======================================================================*/
PUBLIC void wm_init(void)
{
	int i;
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		windows[i].visible = 0;
		windows[i].title[0] = 0;
		windows[i].x = windows[i].y = 0;
		windows[i].w = windows[i].h = 0;
		windows[i].prev_x = windows[i].prev_y = 0;
		windows[i].prev_w = windows[i].prev_h = 0;
		windows[i].state = WM_WS_NORMAL;
		windows[i].type = WM_WT_NORMAL;
		windows[i].fm_dir_cluster = 0;
		windows[i].fm_selected = 0;
		windows[i].fm_count = 0;
		windows[i].calc_accum = 0;
		windows[i].calc_operand = 0;
		windows[i].calc_operator = 0;
		windows[i].calc_new_input = 0;
		windows[i].calc_display[0] = 0;
	}
	wm_count = 0;
	wm_active = -1;
	drag_active = 0;
	fm_dclick_ticks = 0;
	fm_dclick_win = -1;
	fm_dclick_row = -1;
	main_dclick_ticks = 0;
	main_dclick_app = -1;
	taskbar_hover = -1;
	main_window = -1;
	term.initialized = 0;
	term.win_idx = -1;
	/* 清理所有用户控制台状态 (防止上次桌面残留) */
	{
		int i;
		for (i = 0; i < NR_USER_PROCS; i++) {
			user_consoles[i].active = 0;
			user_consoles[i].exiting = 0;
		}
	}
}


/*======================================================================*
                          wm_create
 *======================================================================*/
PUBLIC int wm_create(int x, int y, int w, int h, const char* title)
{
	int idx;
	int i;

	if (wm_count >= WM_MAX_WINDOWS) return -1;

	idx = -1;
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (!windows[i].visible) { idx = i; break; }
	}
	if (idx < 0) return -1;

	if (w < 80) w = 80;
	if (h < WM_TITLE_H + WM_BORDER_3D + 10)
		h = WM_TITLE_H + WM_BORDER_3D + 10;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x + w > g_screen_width)  x = g_screen_width - w;
	if (y + h > g_screen_height - WM_TASKBAR_H)
		y = g_screen_height - WM_TASKBAR_H - h;

	windows[idx].x = x;  windows[idx].y = y;
	windows[idx].w = w;  windows[idx].h = h;
	windows[idx].prev_x = x;  windows[idx].prev_y = y;
	windows[idx].prev_w = w;  windows[idx].prev_h = h;
	windows[idx].visible = 1;
	windows[idx].state = WM_WS_NORMAL;
	windows[idx].type = WM_WT_NORMAL;
	windows[idx].fm_dir_cluster = 0;
	windows[idx].fm_selected = 0;
	windows[idx].fm_count = 0;
	windows[idx].calc_accum = 0;
	windows[idx].calc_operand = 0;
	windows[idx].calc_operator = 0;
	windows[idx].calc_new_input = 0;
	windows[idx].ed_menu_open = 0;
	windows[idx].dirty = 0;
	windows[idx].uc_owner = -1;
	windows[idx].uw_owner = -1;
	windows[idx].uw_canvas_idx = -1;

	{
		const char* s = title;
		char* d = windows[idx].title;
		for (i = 0; i < 31 && *s; i++) *d++ = *s++;
		*d = 0;
	}

	wm_count++;
	wm_active = idx;
	return idx;
}


/*======================================================================*
                          wm_create_fm
 *======================================================================*/
PUBLIC int wm_create_fm(t_32 dir_cluster)
{
	int offset = (wm_count * 15) % 80;
	int idx;

	/* 确保 FAT 已初始化 (fat_init 内部有幂等检查) */
	fat_init();

	idx = wm_create(60 + offset, 40 + offset, 30 * g_font_width, 15 * g_font_height, "File Manager");
	if (idx < 0) return -1;
	windows[idx].type = WM_WT_FILEMANAGER;
	windows[idx].fm_dir_cluster = dir_cluster;
	windows[idx].fm_selected = 0;
	windows[idx].fm_count = 0;
	wm_redraw_all();
	return idx;
}


/*======================================================================*
                          About 应用
 *======================================================================*/
PRIVATE int wm_create_about(void)
{
	int idx = wm_create(80, 50, 28 * g_font_width, 10 * g_font_height, "About CatOS");
	if (idx < 0) return -1;
	windows[idx].type = WM_WT_ABOUT;
	wm_redraw_all();
	return idx;
}

PRIVATE void wm_draw_about_client(int idx)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;
	int line_y;
	int text_x;

	fill_rect(cx, cy, cw, ch, WM_COL_CLIENT);

	/* 系统名称 */
	text_x = cx + (cw - 8 * g_font_width) / 2;
	draw_string(text_x, cy + 8, "CatOS v1.0", 0x0B, WM_COL_CLIENT);

	/* 分隔线 */
	draw_hline(cx + 4, cx + cw - 5, cy + 24, 0x08);

	/* 信息行 */
	line_y = cy + 30;
	draw_string(cx + 8, line_y,         "A 32-bit OS", 0x0F, WM_COL_CLIENT);
	draw_string(cx + 8, line_y + 14,    "VGA Mode 13h", 0x07, WM_COL_CLIENT);
	draw_string(cx + 8, line_y + 28,    "FAT12/16/32", 0x07, WM_COL_CLIENT);
	draw_string(cx + 8, line_y + 42,    "Window Mgr",  0x07, WM_COL_CLIENT);
}


/*======================================================================*
                          Calculator 应用
 *======================================================================*/

/* 计算器按钮布局 */
/* 计算器按钮尺寸 (随字体缩放) */
#define CALC_BTN_W   (g_font_height + 12)
#define CALC_BTN_H   (g_font_height + 4)
#define CALC_BTN_GAP  3
#define CALC_DISP_H  (g_font_height + 6)

/* 按钮定义: row, col, label, action */
/* action: 0=数字 1=+ 2=- 3=* 4=/ 5== 6=C(清除) */
PRIVATE const char calc_labels[4][5] = {
	{ '7','8','9','/','C' },
	{ '4','5','6','*',' ' },
	{ '1','2','3','-',' ' },
	{ '0','.','=','+',' ' },
};

/* 按钮位置 → action 映射
   返回: 0-9=数字, 10=+, 11=-, 12=*, 13=/, 14==, 15=C, -1=无 */
PRIVATE int calc_hit_test(int idx, int px, int py)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H + CALC_DISP_H + 4;
	int row, col;

	for (row = 0; row < 4; row++) {
		for (col = 0; col < 5; col++) {
			int bx = cx + col * (CALC_BTN_W + CALC_BTN_GAP);
			int by = cy + row * (CALC_BTN_H + CALC_BTN_GAP);
			if (px >= bx && px < bx + CALC_BTN_W &&
			    py >= by && py < by + CALC_BTN_H) {
				char ch = calc_labels[row][col];
				if (ch == ' ') return -1;
				if (ch >= '0' && ch <= '9') return ch - '0';
				switch (ch) {
				case '+': return 10;
				case '-': return 11;
				case '*': return 12;
				case '/': return 13;
				case '=': return 14;
				case 'C': return 15;
				}
			}
		}
	}
	return -1;
}

PRIVATE void calc_format_num(t_32 val, char* out)
{
	/* 简单整数转字符串 */
	char tmp[12];
	int i = 0;
	int j = 0;
	if (val == 0) { out[0] = '0'; out[1] = 0; return; }
	if (val < 0) { out[j++] = '-'; val = -val; }
	while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
	while (i > 0) out[j++] = tmp[--i];
	out[j] = 0;
}

PRIVATE void calc_do_action(int idx, int action)
{
	WINDOW* win = &windows[idx];

	if (action >= 0 && action <= 9) {
		/* 数字输入 */
		if (win->calc_new_input) {
			win->calc_operand = action;
			win->calc_new_input = 0;
		} else {
			win->calc_operand = win->calc_operand * 10 + action;
		}
	} else if (action >= 10 && action <= 13) {
		/* 运算符 */
		if (win->calc_operator != 0 && !win->calc_new_input) {
			/* 执行上一次运算 */
			switch (win->calc_operator) {
			case 10: win->calc_accum += win->calc_operand; break;
			case 11: win->calc_accum -= win->calc_operand; break;
			case 12: win->calc_accum *= win->calc_operand; break;
			case 13:
				if (win->calc_operand != 0)
					win->calc_accum /= win->calc_operand;
				break;
			}
		} else {
			win->calc_accum = win->calc_operand;
		}
		win->calc_operator = action;
		win->calc_new_input = 1;
	} else if (action == 14) {
		/* 等号 */
		if (win->calc_operator != 0) {
			switch (win->calc_operator) {
			case 10: win->calc_accum += win->calc_operand; break;
			case 11: win->calc_accum -= win->calc_operand; break;
			case 12: win->calc_accum *= win->calc_operand; break;
			case 13:
				if (win->calc_operand != 0)
					win->calc_accum /= win->calc_operand;
				break;
			}
			win->calc_operand = win->calc_accum;
			win->calc_operator = 0;
			win->calc_new_input = 1;
		}
	} else if (action == 15) {
		/* 清除 */
		win->calc_accum = 0;
		win->calc_operand = 0;
		win->calc_operator = 0;
		win->calc_new_input = 1;
	}

	/* 更新显示文本 */
	calc_format_num(win->calc_operand, win->calc_display);
}

PRIVATE int wm_create_calculator(void)
{
	/* 计算器窗口尺寸根据字体大小动态计算 */
	int calc_w = 5 * (CALC_BTN_W + CALC_BTN_GAP) + 2 * WM_BORDER_3D + 4;
	int calc_h = CALC_DISP_H + 4 * (CALC_BTN_H + CALC_BTN_GAP) + WM_TITLE_H + WM_BORDER_3D + 8;
	int idx = wm_create(100, 40, calc_w, calc_h, "Calculator");
	if (idx < 0) return -1;
	windows[idx].type = WM_WT_CALCULATOR;
	windows[idx].calc_accum = 0;
	windows[idx].calc_operand = 0;
	windows[idx].calc_operator = 0;
	windows[idx].calc_new_input = 1;
	windows[idx].calc_display[0] = '0';
	windows[idx].calc_display[1] = 0;
	wm_redraw_all();
	return idx;
}

PRIVATE void wm_draw_calc_client(int idx)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;
	int row, col;

	fill_rect(cx, cy, cw, ch, WM_COL_CLIENT);

	/* 显示屏 */
	fill_rect(cx + 2, cy + 2, cw - 4, CALC_DISP_H, 0x00);
	draw_hline(cx + 2, cx + cw - 3, cy + 2, 0x07);
	draw_hline(cx + 2, cx + cw - 3, cy + 2 + CALC_DISP_H - 1, 0x07);
	draw_vline(cx + 2, cy + 2, cy + 2 + CALC_DISP_H - 1, 0x07);
	draw_vline(cx + cw - 3, cy + 2, cy + 2 + CALC_DISP_H - 1, 0x07);
	{
		int dl = 0;
		while (win->calc_display[dl]) dl++;
		int dx = cx + cw - 4 - dl * g_font_width;
		int dy = cy + 2 + (CALC_DISP_H - g_font_height) / 2;
		draw_string(dx, dy, win->calc_display, 0x0A, 0x00);
	}

	/* 按钮网格 */
	for (row = 0; row < 4; row++) {
		for (col = 0; col < 5; col++) {
			int bx = cx + col * (CALC_BTN_W + CALC_BTN_GAP);
			int by = cy + CALC_DISP_H + 4 + row * (CALC_BTN_H + CALC_BTN_GAP);
			char ch = calc_labels[row][col];
			if (ch == ' ') continue;
			/* 按钮背景 */
			fill_rect(bx, by, CALC_BTN_W, CALC_BTN_H, 0x07);
			draw_hline(bx, bx + CALC_BTN_W - 1, by, 0x0F);
			draw_vline(bx, by, by + CALC_BTN_H - 1, 0x0F);
			draw_hline(bx, bx + CALC_BTN_W - 1, by + CALC_BTN_H - 1, 0x00);
			draw_vline(bx + CALC_BTN_W - 1, by, by + CALC_BTN_H - 1, 0x00);
			/* 按钮文字 */
			{
				int tx = bx + (CALC_BTN_W - g_font_width) / 2;
				int ty = by + (CALC_BTN_H - g_font_height) / 2;
				t_8 fg = 0x00;
				if (ch == '+' || ch == '-' || ch == '*' || ch == '/')
					fg = 0x0C;
				else if (ch == '=' || ch == 'C')
					fg = 0x09;
				draw_char(tx, ty, ch, fg, 0x07);
			}
		}
	}
}


/*======================================================================*
                          Terminal 应用
 *======================================================================*/

/* 终端文本缓冲操作 */
PRIVATE void term_clear(void)
{
	int i;
	for (i = 0; i < TERM_BUF_SZ; i++) {
		term.text[i] = ' ';
		term.attr[i] = TERM_FG;
	}
	term.cur_row = 0;
	term.cur_col = 0;
	term.s_hist_count = 0;
	term.s_hist_pos = 0;
	term.view_offset = 0;
}

/* 滚动一行 */
PRIVATE void term_scroll(void)
{
	int r, c;
	/* 把将被滚掉的顶行存入历史缓冲 (环形) */
	{
		char *htext = term.s_hist[term.s_hist_pos];
		t_8  *hattr = term.s_hist_attr[term.s_hist_pos];
		for (c = 0; c < TERM_COLS; c++) {
			htext[c] = term.text[c];
			hattr[c] = term.attr[c];
		}
		term.s_hist_pos = (term.s_hist_pos + 1) % TERM_SCROLL_LINES;
		if (term.s_hist_count < TERM_SCROLL_LINES) term.s_hist_count++;
	}
	/* 上移所有行 */
	for (r = 0; r < TERM_ROWS - 1; r++) {
		for (c = 0; c < TERM_COLS; c++) {
			term.text[r * TERM_COLS + c] = term.text[(r + 1) * TERM_COLS + c];
			term.attr[r * TERM_COLS + c] = term.attr[(r + 1) * TERM_COLS + c];
		}
	}
	/* 清空最后一行 */
	for (c = 0; c < TERM_COLS; c++) {
		term.text[(TERM_ROWS - 1) * TERM_COLS + c] = ' ';
		term.attr[(TERM_ROWS - 1) * TERM_COLS + c] = TERM_FG;
	}
}

/* 输出一个字符到终端缓冲 */
PRIVATE void term_putc(char ch, t_8 color)
{
	if (ch == '\n') {
		term.cur_col = 0;
		term.cur_row++;
		if (term.cur_row >= TERM_ROWS) {
			term_scroll();
			term.cur_row = TERM_ROWS - 1;
		}
		return;
	}
	if (term.cur_col >= TERM_COLS) {
		term.cur_col = 0;
		term.cur_row++;
		if (term.cur_row >= TERM_ROWS) {
			term_scroll();
			term.cur_row = TERM_ROWS - 1;
		}
	}
	term.text[term.cur_row * TERM_COLS + term.cur_col] = ch;
	term.attr[term.cur_row * TERM_COLS + term.cur_col] = color;
	term.cur_col++;
}

/* 输出字符串 */
PRIVATE void term_puts(const char* s, t_8 color)
{
	while (*s) term_putc(*s++, color);
}

/* 字符串比较 */
PRIVATE int term_streq(const char* a, const char* b)
{
	while (*a && *b && *a == *b) { a++; b++; }
	return (*a == *b);
}

/* 整数转字符串 */
PRIVATE void term_itoa(int val, char* out)
{
	char tmp[12];
	int i = 0, j = 0;
	if (val == 0) { out[0] = '0'; out[1] = 0; return; }
	if (val < 0) { out[j++] = '-'; val = -val; }
	while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
	while (i > 0) out[j++] = tmp[--i];
	out[j] = 0;
}

/* 桌面终端路径维护 */
PRIVATE void term_path_reset(void)
{
	term.cwd_path[0] = '/';
	term.cwd_path[1] = '\0';
}
PRIVATE void term_path_enter(const char *name)
{
	int i = 0;
	while (term.cwd_path[i] && i < 36) i++;
	while (*name && i < 36) term.cwd_path[i++] = *name++;
	if (i == 0 || term.cwd_path[i - 1] != '/') {
		if (i < 39) term.cwd_path[i++] = '/';
	}
	term.cwd_path[i] = '\0';
}
PRIVATE void term_path_parent(void)
{
	int i = 0;
	while (term.cwd_path[i]) i++;
	if (i > 1 && term.cwd_path[i - 1] == '/') i--;
	while (i > 1 && term.cwd_path[i - 1] != '/') i--;
	if (i == 0) i = 1;
	term.cwd_path[i] = '\0';
	if (i == 1 && term.cwd_path[0] != '/') {
		term.cwd_path[0] = '/';
		term.cwd_path[1] = '\0';
	}
}

/* 显示提示符 */
/* 动态提示符缓冲 */
PRIVATE char term_prompt_buf[50];

PRIVATE void term_show_prompt(void)
{
	int i = 0;
	/* "CatOS " 前缀 */
	term_prompt_buf[i++] = 'C';
	term_prompt_buf[i++] = 'a';
	term_prompt_buf[i++] = 't';
	term_prompt_buf[i++] = 'O';
	term_prompt_buf[i++] = 'S';
	term_prompt_buf[i++] = ' ';
	/* 当前路径 */
	{
		const char *p = term.cwd_path;
		while (*p && i < 42) term_prompt_buf[i++] = *p++;
	}
	/* "> " 后缀 */
	if (i < 46) term_prompt_buf[i++] = '>';
	if (i < 47) term_prompt_buf[i++] = ' ';
	term_prompt_buf[i] = '\0';
	term.prompt_len = i;
	term_puts(term_prompt_buf, TERM_PROMPT);
}

/* dir 命令回调: 显示每个目录条目 */
PRIVATE int term_dir_count = 0;
PRIVATE void term_dir_cb(FAT_DIR_ENTRY *de)
{
	char name[16];
	int k, j = 0;
	if (de->name[0] == '.' && (de->name[1] == ' ' || de->name[1] == '.')) return;
	for (k = 0; k < 8 && de->name[k] != ' '; k++) name[j++] = de->name[k];
	if (de->ext[0] != ' ') {
		name[j++] = '.';
		for (k = 0; k < 3 && de->ext[k] != ' '; k++) name[j++] = de->ext[k];
	}
	name[j] = 0;
	if (de->attr & FAT_ATTR_DIRECTORY) {
		term_puts(S_T_DIR_TAG, 0x0B);
	} else {
		term_puts(S_T_DIR_SPACE, TERM_FG);
	}
	term_puts(name, TERM_INFO);
	term_putc('\n', TERM_FG);
	term_dir_count++;
}

/* 终端命令执行 */
PRIVATE void term_execute(const char* cmd)
{
	const char* p = cmd;
	while (*p == ' ' || *p == '\t') p++;

	if (*p == 0) {
		term_show_prompt();
		return;
	}

	/* help */
	if (term_streq(p, "help")) {
		term_puts(S_T_HELP_HDR, TERM_INFO);
		term_puts(S_T_HELP_1, TERM_FG);
		term_puts(S_T_HELP_2, TERM_FG);
		term_puts(S_T_HELP_3, TERM_FG);
		term_puts(S_T_HELP_4, TERM_FG);
		term_show_prompt();
		return;
	}

	/* ver / version */
	if (term_streq(p, "ver") || term_streq(p, "version")) {
		term_puts(S_T_VER1, TERM_INFO);
		term_puts(S_T_VER2, TERM_FG);
		term_show_prompt();
		return;
	}

	/* echo */
	if (p[0]=='e' && p[1]=='c' && p[2]=='h' && p[3]=='o' &&
	    (p[4]==' ' || p[4]=='\0')) {
		if (p[4] == ' ') {
			term_puts(p + 5, TERM_INFO);
		}
		term_putc('\n', TERM_FG);
		term_show_prompt();
		return;
	}

	/* cls / clear */
	if (term_streq(p, "cls") || term_streq(p, "clear")) {
		term_clear();
		term_show_prompt();
		return;
	}

	/* sysinfo */
	if (term_streq(p, "sysinfo")) {
		char num[12];
		int conv_kb;
		term_puts(S_T_CPU, TERM_FG);
		term_puts(S_T_CPU2, TERM_INFO);
		term_puts(S_T_VIDEO, TERM_FG);
		/* 读取 BIOS 常规内存 (0x413) */
		__asm__ __volatile__("movw 0x413, %w0" : "=a"(conv_kb));
		term_puts(S_T_MEM, TERM_FG);
		term_itoa(conv_kb, num);
		term_puts(num, TERM_INFO);
		term_puts(S_T_KBC, TERM_FG);
		term_puts(S_T_FS, TERM_FG);
		{
			fat_init();
			FAT_INFO *fi = fat_get_info();
			if (fi) {
				term_puts(S_T_FAT, TERM_INFO);
				term_itoa(fi->fat_type, num);
				term_puts(num, TERM_INFO);
				term_putc('\n', TERM_FG);
			} else {
				term_puts(S_T_FS_NONE, TERM_ERROR);
			}
		}
		term_show_prompt();
		return;
	}

	/* dir / ls */
	if (term_streq(p, "dir") || term_streq(p, "ls")) {
		fat_init();
		{
			FAT_INFO *fi = fat_get_info();
			if (!fi) {
				term_puts(S_T_NO_FS, TERM_ERROR);
				term_show_prompt();
				return;
			}
			term_dir_count = 0;
			fat_list_dir(term.cwd, term_dir_cb);
			if (term_dir_count == 0) {
				term_puts(S_T_EMPTY, TERM_FG);
			}
		}
		term_show_prompt();
		return;
	}

	/* type <file> - 显示文件内容 */
	if (p[0]=='t' && p[1]=='y' && p[2]=='p' && p[3]=='e' &&
	    (p[4]==' ' || p[4]=='\0')) {
		const char* fname = p + 4;
		while (*fname == ' ') fname++;
		if (*fname == '\0') {
			term_puts(S_T_TYPE_USE, TERM_ERROR);
			term_show_prompt();
			return;
		}
		fat_init();
		{
			int n = fat_read_file_in(term.cwd, fname, term_file_buf, sizeof(term_file_buf) - 1);
			int i;
			if (n < 0) {
				term_puts(S_T_TYPE_NF, TERM_ERROR);
				term_puts(fname, TERM_ERROR);
				term_putc('\n', TERM_ERROR);
				term_show_prompt();
				return;
			}
			for (i = 0; i < n; i++) {
				char c = (char)term_file_buf[i];
				if (c == '\r') continue;
				if (c == '\t') c = ' ';
				if (c == '\n') {
					term_putc('\n', TERM_FG);
					continue;
				}
				if ((t_8)c < 0x20) continue;
				term_putc(c, TERM_INFO);
			}
			term_putc('\n', TERM_FG);
		}
		term_show_prompt();
		return;
	}

	/* touch <file> - 创建空文件 */
	if (p[0]=='t' && p[1]=='o' && p[2]=='u' && p[3]=='c' && p[4]=='h' &&
	    (p[5]==' ' || p[5]=='\0')) {
		const char* fname = p + 5;
		while (*fname == ' ') fname++;
		if (*fname == '\0') {
			term_puts(S_T_TOUCH_USE, TERM_ERROR);
			term_show_prompt();
			return;
		}
		fat_init();
		{
			int ret = fat_touch_in(term.cwd, fname);
			if (ret == 0) {
				term_puts(S_T_TOUCH_OK, 0x0A);
				term_puts(fname, TERM_INFO);
				term_putc('\n', TERM_FG);
			} else if (ret == 1) {
				term_puts(S_T_TOUCH_EX, 0x0E);
				term_puts(fname, TERM_INFO);
				term_putc('\n', TERM_FG);
			} else {
				term_puts(S_T_TOUCH_FAIL, TERM_ERROR);
			}
		}
		term_show_prompt();
		return;
	}

	/* rd <file> - 删除文件 */
	if (p[0]=='r' && p[1]=='d' && p[2]==' ') {
		const char* fname = p + 3;
		while (*fname == ' ') fname++;
		if (*fname == '\0') {
			term_puts(S_T_RD_USE, TERM_ERROR);
			term_show_prompt();
			return;
		}
		fat_init();
		{
			int ret = fat_delete_in(term.cwd, fname);
			if (ret == 0) {
				term_puts(S_T_RD_OK, 0x0A);
				term_puts(fname, TERM_INFO);
				term_putc('\n', TERM_FG);
			} else if (ret == 1) {
				term_puts(S_T_RD_NF, TERM_ERROR);
				term_puts(fname, TERM_ERROR);
				term_putc('\n', TERM_ERROR);
			} else {
				term_puts(S_T_RD_FAIL, TERM_ERROR);
			}
		}
		term_show_prompt();
		return;
	}

	/* edit <file> - 用记事本打开文件 */
	if (p[0]=='e' && p[1]=='d' && p[2]=='i' && p[3]=='t' &&
	    (p[4]==' ' || p[4]=='\0')) {
		const char* fname = p + 4;
		while (*fname == ' ') fname++;
		if (*fname == '\0') {
			term_puts(S_T_EDIT_USE, TERM_ERROR);
			term_show_prompt();
			return;
		}
		wm_create_editor(fname, term.cwd);
		term_show_prompt();
		return;
	}

	/* ps - 列出进程 */
	if (term_streq(p, "ps")) {
		char num[12];
		int i;
		int count = 0;
		term_puts(S_T_PID, TERM_INFO);
		for (i = 0; i < NR_TASKS + NR_PROCS; i++) {
			if (proc_table[i].priority > 0) {
				term_itoa(proc_table[i].pid, num);
				term_puts(num, TERM_FG);
				term_puts("  ", TERM_FG);  /* 短空格串, 无 .rodata 寻址风险 */
				term_puts(proc_table[i].name, TERM_INFO);
				term_putc('\n', TERM_FG);
				count++;
			}
		}
		if (count == 0) {
			term_puts(S_T_NOPROC, TERM_FG);
		}
		term_show_prompt();
		return;
	}

	/* cat - 屏蔽 (桌面已运行) */
	if (term_streq(p, "cat")) {
		term_puts(S_T_CAT_DIS, TERM_ERROR);
		term_puts(S_T_DSK_RUN, TERM_ERROR);
		term_show_prompt();
		return;
	}

	/* ===== cd (切换当前目录) =====
	 * cd / cd \ / cd /  → 根目录
	 * cd .              → 当前目录 (不变)
	 * cd ..             → 父目录 (根目录时不变)
	 * cd <name>         → 进入指定子目录
	 */
	if (p[0]=='c' && p[1]=='d' && (p[2]=='\0' || p[2]==' ')) {
		const char *arg;
		/* cd 无参数 → 回到根目录 */
		if (p[2] == '\0') {
			term.cwd = 0;
			term_path_reset();
			term_puts(S_T_CD_ROOT, 0x0B);
			term_show_prompt();
			return;
		}
		arg = p + 3;
		while (*arg == ' ') arg++;
		/* cd (空参数) → 回到根目录 */
		if (*arg == '\0') {
			term.cwd = 0;
			term_path_reset();
			term_puts(S_T_CD_ROOT, 0x0B);
			term_show_prompt();
			return;
		}
		/* cd \ 或 cd / → 根目录 */
		if (*arg == '\\' || *arg == '/') {
			term.cwd = 0;
			term_path_reset();
			term_puts(S_T_CD_ROOT, 0x0B);
			term_show_prompt();
			return;
		}
		/* cd . → 当前目录 (不变) */
		if (arg[0] == '.' && arg[1] == '\0') {
			term_show_prompt();
			return;
		}
		/* cd .. → 父目录 */
		if (arg[0] == '.' && arg[1] == '.' && arg[2] == '\0') {
			if (term.cwd == 0) {
				term_show_prompt();
				return;  /* 已在根目录 */
			}
			fat_init();
			{
				t_32 parent = fat_find_subdir(term.cwd, "..");
				/* parent>=2: 父目录簇; parent==0: 父目录是根目录 */
				term.cwd = (parent >= 2) ? parent : 0;
				term_path_parent();
			}
			term_show_prompt();
			return;
		}
		/* cd <name> → 进入子目录 */
		fat_init();
		{
			t_32 target = fat_find_subdir(term.cwd, arg);
			if (target >= 2) {
				term.cwd = target;
				term_path_enter(arg);
			} else {
				term_puts(S_T_CD_NF, TERM_ERROR);
				term_puts(arg, TERM_ERROR);
				term_putc('\n', TERM_ERROR);
			}
		}
		term_show_prompt();
		return;
	}

	/* ===== mkdir (创建子目录) ===== */
	if (p[0]=='m' && p[1]=='k' && p[2]=='d' && p[3]=='i' && p[4]=='r' &&
	    (p[5]==' ' || p[5]=='\0')) {
		const char *arg;
		if (p[5] == '\0') {
			term_puts(S_T_MKDIR_USE, TERM_ERROR);
			term_show_prompt();
			return;
		}
		arg = p + 6;
		while (*arg == ' ') arg++;
		if (*arg == '\0') {
			term_puts(S_T_MKDIR_USE, TERM_ERROR);
			term_show_prompt();
			return;
		}
		fat_init();
		{
			int ret = fat_mkdir_in(term.cwd, arg);
			if (ret == 0) {
				term_puts(S_T_MKDIR_OK, 0x0A);
				term_puts(arg, TERM_INFO);
				term_putc('\n', TERM_FG);
			} else if (ret == 1) {
				term_puts(S_T_MKDIR_EX, 0x0E);
				term_puts(arg, TERM_INFO);
				term_putc('\n', TERM_FG);
			} else {
				term_puts(S_T_MKDIR_FAIL, TERM_ERROR);
			}
		}
		term_show_prompt();
		return;
	}

	/* ===== run (运行可执行文件, 非阻塞) =====
	 * 桌面终端不支持前台阻塞 (会冻结 UI), 总是后台启动.
	 * 程序结束后自动回到桌面 (槽位释放).
	 */
	if (p[0]=='r' && p[1]=='u' && p[2]=='n' && p[3]==' ') {
		const char *fname = p + 4;
		const char *amp;
		int ret;
		while (*fname == ' ' || *fname == '\t') fname++;
		if (*fname == '\0') {
			term_puts(S_T_RUN_USE, TERM_ERROR);
			term_show_prompt();
			return;
		}
		/* 去掉末尾的 '&' (桌面终端忽略前后台区别) */
		amp = fname;
		while (*amp) amp++;
		if (amp > fname) amp--;
		while (amp > fname && (*amp == ' ' || *amp == '\t' || *amp == '&')) amp--;
		(void)amp;  /* 桌面终端总是非阻塞, 不处理 '&' */
		fat_init();
		if (user_proc_free_slots <= 0) {
			term_puts(S_T_RUN_FAIL, TERM_ERROR);
			term_show_prompt();
			return;
		}
		ret = exec_user_program_in(term.cwd, fname);
		if (ret < 0) {
			if (ret == -1) {
				term_puts(S_T_RUN_NF, TERM_ERROR);
				term_puts(fname, TERM_ERROR);
				term_putc('\n', TERM_ERROR);
			} else {
				term_puts(S_T_RUN_FAIL, TERM_ERROR);
			}
		} else {
			term_puts(S_T_RUN_OK, 0x0A);
			term_puts(fname, TERM_INFO);
			term_putc('\n', TERM_FG);
		}
		term_show_prompt();
		return;
	}

	/* reboot */
	if (term_streq(p, "reboot")) {
		term_puts(S_T_REBOOT, TERM_INFO);
		term_puts(S_T_REBOOT_NO, TERM_ERROR);
		term_show_prompt();
		return;
	}

	/* 未知命令 */
	term_puts(S_T_UNKNOWN, TERM_ERROR);
	term_puts(p, TERM_ERROR);
	term_putc('\n', TERM_ERROR);
	term_show_prompt();
}

/* 保存命令到历史 */
PRIVATE void term_hist_add(const char* cmd)
{
	int i;
	if (term.hist_count >= TERM_HIST_SZ) {
		/* 满了: 整体前移 */
		for (i = 0; i < TERM_HIST_SZ - 1; i++) {
			int j;
			for (j = 0; j < TERM_COLS; j++)
				term.hist[i][j] = term.hist[i + 1][j];
		}
		term.hist_count = TERM_HIST_SZ - 1;
	}
	/* 复制到末尾 */
	{
		int j;
		for (j = 0; j < TERM_COLS - 1 && cmd[j]; j++)
			term.hist[term.hist_count][j] = cmd[j];
		term.hist[term.hist_count][j] = 0;
	}
	term.hist_count++;
}

/* 重绘当前命令行 (保留动态提示符, 从 prompt_len 列开始) */
PRIVATE void term_redraw_cmdline(void)
{
	int i;
	int base = term.cur_row * TERM_COLS;
	int pl = term.prompt_len;
	if (pl <= 0) pl = 7;  /* 安全默认 */
	/* 清除命令行区域 (保留提示符) */
	for (i = pl; i < TERM_COLS; i++) {
		term.text[base + i] = ' ';
		term.attr[base + i] = TERM_FG;
	}
	/* 绘制命令内容 (从提示符后开始) */
	for (i = 0; i < term.cmd_len && pl + i < TERM_COLS; i++) {
		term.text[base + pl + i] = term.cmdline[i];
		term.attr[base + pl + i] = TERM_FG;
	}
	term.cur_col = pl + term.cmd_cursor;
}

/* 处理终端键盘输入 */
PRIVATE void term_handle_key(t_32 key)
{
	if (!term.initialized) return;

	if (key == 0) return;

	/* 非滚动键按下时退出回看模式 */
	if (key != PAGEUP && key != PAGEDOWN && term.view_offset > 0) {
		term.view_offset = 0;
	}

	if (!(key & FLAG_EXT)) {
		/* 普通可打印字符 */
		char ch = (char)(key & 0xFF);
		if (term.cmd_len < TERM_COLS - 8 && ch >= ' ' && ch <= '~') {
			/* 插入模式: 在光标处插入字符 */
			int i;
			for (i = term.cmd_len; i > term.cmd_cursor; i--)
				term.cmdline[i] = term.cmdline[i - 1];
			term.cmdline[term.cmd_cursor] = ch;
			term.cmd_len++;
			term.cmd_cursor++;
			term_redraw_cmdline();
		}
	} else {
		/* 直接比较完整键值 (与编辑器一致) */
		if (key == ENTER) {
			/* 执行命令 */
			term.cmdline[term.cmd_len] = 0;
			term_putc('\n', TERM_FG);
			if (term.cmd_len > 0) {
				term_hist_add(term.cmdline);
				term_execute(term.cmdline);
			} else {
				term_show_prompt();
			}
			term.cmd_len = 0;
			term.cmd_cursor = 0;
			term.cmdline[0] = 0;
			term.hist_idx = -1;
		} else if (key == BACKSPACE) {
			if (term.cmd_cursor > 0) {
				int i;
				for (i = term.cmd_cursor - 1; i < term.cmd_len; i++)
					term.cmdline[i] = term.cmdline[i + 1];
				term.cmd_len--;
				term.cmd_cursor--;
				term_redraw_cmdline();
			}
		} else if (key == LEFT) {
			if (term.cmd_cursor > 0) {
				term.cmd_cursor--;
				term.cur_col = term.prompt_len + term.cmd_cursor;
			}
		} else if (key == RIGHT) {
			if (term.cmd_cursor < term.cmd_len) {
				term.cmd_cursor++;
				term.cur_col = term.prompt_len + term.cmd_cursor;
			}
		} else if (key == UP) {
			/* 浏览历史: 上一条 */
			if (term.hist_count > 0) {
				if (term.hist_idx < 0) {
					term.hist_idx = term.hist_count - 1;
				} else if (term.hist_idx > 0) {
					term.hist_idx--;
				}
				{
					int i;
					for (i = 0; i < TERM_COLS - 1 && term.hist[term.hist_idx][i]; i++)
						term.cmdline[i] = term.hist[term.hist_idx][i];
					term.cmdline[i] = 0;
					term.cmd_len = i;
					term.cmd_cursor = term.cmd_len;
					term_redraw_cmdline();
				}
			}
		} else if (key == DOWN) {
			/* 浏览历史: 下一条 */
			if (term.hist_idx >= 0) {
				if (term.hist_idx < term.hist_count - 1) {
					term.hist_idx++;
					{
						int i;
						for (i = 0; i < TERM_COLS - 1 && term.hist[term.hist_idx][i]; i++)
							term.cmdline[i] = term.hist[term.hist_idx][i];
						term.cmdline[i] = 0;
						term.cmd_len = i;
						term.cmd_cursor = term.cmd_len;
						term_redraw_cmdline();
					}
				} else {
					/* 回到空命令行 */
					term.hist_idx = -1;
					term.cmd_len = 0;
					term.cmd_cursor = 0;
					term.cmdline[0] = 0;
					term_redraw_cmdline();
				}
			}
		} else if (key == HOME) {
			term.cmd_cursor = 0;
			term.cur_col = term.prompt_len;
		} else if (key == END) {
			term.cmd_cursor = term.cmd_len;
			term.cur_col = term.prompt_len + term.cmd_cursor;
		} else if (key == PAGEUP) {
			/* 向上回看历史 */
			if (term.view_offset < term.s_hist_count)
				term.view_offset++;
		} else if (key == PAGEDOWN) {
			/* 向下回看 (回到最新) */
			if (term.view_offset > 0)
				term.view_offset--;
		}
	}
}

/* 创建终端窗口 */
PRIVATE int wm_create_terminal(void)
{
	/* 窗口尺寸根据字体大小动态计算:
	 * 宽度 = 文本区(TERM_COLS * 字宽) + 滚动条 + 边距 + 边框
	 * 高度 = 文本区(TERM_ROWS * 字高) + 标题栏 + 边框 */
	int win_w = TERM_COLS * g_font_width + 2 + 2 * WM_BORDER_3D + 4;
	int win_h = TERM_ROWS * g_font_height + WM_TITLE_H + WM_BORDER_3D + 4;
	int idx = wm_create(20, 20, win_w, win_h, S_T_TERM_TITLE);
	if (idx < 0) return -1;
	windows[idx].type = WM_WT_TERMINAL;

	/* 初始化终端状态 */
	term_clear();
	term.initialized = 1;
	term.win_idx = idx;
	term.cmd_len = 0;
	term.cmd_cursor = 0;
	term.cmdline[0] = 0;
	term.hist_count = 0;
	term.hist_idx = -1;
	term.cwd = 0;
	term.cwd_path[0] = '/';
	term.cwd_path[1] = '\0';

	/* 欢迎信息 */
	term_puts(S_T_WELCOME1, TERM_INFO);
	term_puts(S_T_WELCOME2, TERM_FG);
	term_show_prompt();

	wm_redraw_all();
	return idx;
}

/*======================================================================*
                          wm_create_user_console  (用户控制台窗口)
 *======================================================================*/
/* 为用户进程 slot 创建控制台窗口.
 * title 可为 NULL (使用默认标题).
 * 返回窗口索引 (-1=失败) */
PUBLIC int wm_create_user_console(int slot, const char *title)
{
	int idx;
	struct USER_CONSOLE *uc;
	char default_title[] = "Console";
	char *t;

	if (slot < 0 || slot >= NR_USER_PROCS) return -1;
	uc = &user_consoles[slot];
	if (uc->active) {
		if (uc->exiting) {
			/* 旧程序已退出但窗口等待按键, 强制关闭后创建新窗口 */
			wm_force_close_user_console(slot);
		} else {
			return uc->win_idx;  /* 同槽位已有活动控制台, 复用 */
		}
	}

	t = title ? (char*)title : default_title;
	idx = wm_create(20 + slot * 10, 20 + slot * 10,
	                UC_COLS * g_font_width + 2 * WM_BORDER_3D,
	                UC_ROWS * g_font_height + WM_TITLE_H + WM_BORDER_3D,
	                t);
	if (idx < 0) return -1;

	windows[idx].type = WM_WT_USERCONSOLE;
	windows[idx].uc_owner = slot;

	uc->active = 1;
	uc->owner_slot = slot;
	uc->win_idx = idx;
	uc->exiting = 0;
	uc->cur_row = 0;
	uc->cur_col = 0;
	uc->cursor_visible = 1;  /* 光标初始可见, wm_run 定时翻转 */
	uc->key_head = 0;
	uc->key_tail = 0;
	uc->key_count = 0;
	{
		int r, c;
		for (r = 0; r < UC_ROWS; r++) {
			for (c = 0; c < UC_COLS; c++) {
				uc->text[r][c] = ' ';
				uc->attr[r][c] = 0x0F;
			}
		}
	}

	wm_set_active(idx);
	wm_redraw_all();
	return idx;
}

/*======================================================================*
                          wm_close_user_console  (标记程序退出, 等待按键)
 *======================================================================*/
/* 程序退出时不立即关闭窗口, 而是显示 "Press any key to continue..." 并标记 exiting.
 * wm_run 收到该窗口的按键后会调用 wm_force_close_user_console 真正关闭.
 * 这样用户能看到程序最后的输出, 按键后才关闭窗口. */
PUBLIC void wm_close_user_console(int slot)
{
	struct USER_CONSOLE *uc;
	if (slot < 0 || slot >= NR_USER_PROCS) return;
	uc = &user_consoles[slot];
	if (!uc->active) return;

	/* 显示退出提示 */
	{
		char *p = S_UC_PRESS_KEY;
		while (*p) {
			wm_uc_putc(slot, *p, 0x0B);  /* 青色提示 */
			p++;
		}
	}
	wm_uc_invalidate(slot);

	/* 标记退出等待状态: 不再接受按键到队列, wm_run 检测到此标志后
	 * 收到任意按键即真正关闭窗口 */
	uc->exiting = 1;
}

/* 真正关闭用户控制台窗口 (释放窗口资源) */
PUBLIC void wm_force_close_user_console(int slot)
{
	int win_idx;
	if (slot < 0 || slot >= NR_USER_PROCS) return;
	if (!user_consoles[slot].active) return;
	win_idx = user_consoles[slot].win_idx;
	user_consoles[slot].active = 0;
	user_consoles[slot].exiting = 0;
	if (win_idx >= 0 && win_idx < WM_MAX_WINDOWS) {
		windows[win_idx].uc_owner = -1;
		wm_close(win_idx);
	}
}

/* 强制终止指定槽位的用户进程, 并关闭其所有窗口 (控制台 + 子窗口)
 * 用于点击用户控制台窗口关闭按钮时, 直接结束进程
 * 流程:
 *   1. 关闭该 slot 创建的所有 UW 子窗口
 *   2. 强制关闭 USERCONSOLE 窗口
 *   3. 标记进程为非活跃 (priority=0, ticks=0, is_user_proc=0)
 *   4. 释放槽位计数 (user_proc_free_slots++)
 * 注意: 本函数由 wm_run (task_tty) 调用, 修改的是其他进程的标志位,
 *       不涉及上下文切换, 进程在下次调度时自然不会被选中 */
PUBLIC void wm_kill_user_proc(int slot)
{
	int i;
	PROCESS *p;
	if (slot < 0 || slot >= NR_USER_PROCS) return;

	/* 步骤 1: 关闭该 slot 创建的所有 UW 子窗口 */
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (windows[i].visible &&
		    windows[i].type == WM_WT_USERWINDOW &&
		    windows[i].uw_owner == slot) {
			wm_uw_close(i);
		}
	}

	/* 步骤 2: 强制关闭 USERCONSOLE 窗口 (含窗口资源清理) */
	wm_force_close_user_console(slot);

	/* 步骤 3: 终止进程 (标记为非活跃, 释放槽位) */
	p = &proc_table[NR_TASKS + NR_PROCS + slot];
	if (p->is_user_proc) {
		p->is_user_proc = 0;
		p->ticks = 0;
		p->priority = 0;
		p->exit_code = 0;
		user_proc_free_slots++;
	}
}

/* 绘制终端客户区 */
PRIVATE void wm_draw_term_client(int idx)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;
	int r, c;

	/* 黑色背景 */
	fill_rect(cx, cy, cw, ch, TERM_BG);

	/* 绘制文本缓冲 (支持回看: view_offset>0 时顶部从历史缓冲取行) */
	for (r = 0; r < TERM_ROWS; r++) {
		if (term.view_offset > 0 && r < term.view_offset) {
			/* 从历史缓冲取行 */
			int hist_line = term.s_hist_count - term.view_offset + r;
			if (hist_line >= 0 && hist_line < term.s_hist_count) {
				int buf_idx = (term.s_hist_pos - term.s_hist_count + hist_line
				               + TERM_SCROLL_LINES * 2) % TERM_SCROLL_LINES;
				char *htext = term.s_hist[buf_idx];
				t_8  *hattr = term.s_hist_attr[buf_idx];
				for (c = 0; c < TERM_COLS; c++) {
					char ch2 = htext[c];
					if (ch2 == ' ') continue;
					draw_char(cx + 2 + c * g_font_width,
					          cy + 2 + r * g_font_height,
					          ch2, hattr[c], TERM_BG);
				}
			}
			/* hist_line < 0 表示该行无历史, 留空 */
		} else {
			/* 从当前文本缓冲取行 */
			int text_row = r - term.view_offset;
			if (text_row >= 0 && text_row < TERM_ROWS) {
				for (c = 0; c < TERM_COLS; c++) {
					int idx2 = text_row * TERM_COLS + c;
					char ch2 = term.text[idx2];
					if (ch2 == ' ') continue;
					draw_char(cx + 2 + c * g_font_width,
					          cy + 2 + r * g_font_height,
					          ch2, term.attr[idx2], TERM_BG);
				}
			}
		}
	}

	/* 绘制滚动条 (有历史时在文本右侧显示) */
	if (term.s_hist_count > 0) {
		int bar_x = cx + 2 + TERM_COLS * g_font_width + 2;
		int bar_w = 4;
		int bar_y = cy + 2;
		int bar_h = TERM_ROWS * g_font_height;
		int total = term.s_hist_count + TERM_ROWS;
		int visible = TERM_ROWS;
		int thumb_h = (visible * visible + total - 1) / total;
		int denom = total - visible;
		int top_line = term.s_hist_count - term.view_offset;
		int thumb_row = (denom > 0)
			? (top_line * (visible - thumb_h) / denom) : 0;
		if (thumb_h < 1) thumb_h = 1;
		if (thumb_row < 0) thumb_row = 0;
		if (thumb_row > visible - thumb_h) thumb_row = visible - thumb_h;
		/* 轨道: 深灰 */
		fill_rect(bar_x, bar_y, bar_w, bar_h, 0x08);
		/* 滑块: 白 */
		fill_rect(bar_x, bar_y + thumb_row * g_font_height, bar_w,
		          thumb_h * g_font_height, 0x0F);
	}

	/* 绘制光标 (仅在非回看模式) */
	if (term.win_idx == idx && term.initialized && term.view_offset == 0) {
		int cx_cursor = cx + 2 + term.cur_col * g_font_width;
		int cy_cursor = cy + 2 + term.cur_row * g_font_height;
		/* 反色方块光标 */
		fill_rect(cx_cursor, cy_cursor + g_font_height - 2,
		          g_font_width, 2, TERM_FG);
	}
}


/*======================================================================*
                          记事本编辑器 (Notepad)
 *======================================================================*/

/* 编辑器布局常量 */
#define ED_PAD_X        2
#define ED_PAD_Y        2
#define ED_STATUS_H     (g_font_height + 2)  /* 底部状态栏高度 (随字体缩放) */
#define ED_SCROLL_W     6    /* 右侧滚动条宽度 */
#define ED_MENU_H       (g_font_height + 2)  /* 顶部菜单栏高度 (随字体缩放) */
#define ED_MENU_ITEM_H  (g_font_height + 2)  /* 下拉菜单条目高度 (随字体缩放) */
#define ED_MENU_W       (8 * g_font_width)    /* 下拉菜单宽度 (随字体缩放) */

/* 命名字符串 (非const, 放入 .data 段, 避免 .rodata 寻址问题) */
PRIVATE char S_ED_TITLE[]    = "Notepad";
PRIVATE char S_ED_PREFIX[]   = "Notepad - ";
PRIVATE char S_ED_LN[]       = "Ln ";
PRIVATE char S_ED_COL[]      = " Col ";
PRIVATE char S_ED_STAR[]     = "*";
PRIVATE char S_ED_MENU_FILE[] = "File";
PRIVATE char S_ED_M_SAVE[]   = "Save  Ctrl+S";
PRIVATE char S_ED_M_CLOSE[]  = "Close";

/* 查找编辑器窗口索引 (-1=无) */
PRIVATE int ed_find_win(void)
{
	int i;
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (windows[i].visible && windows[i].type == WM_WT_EDITOR)
			return i;
	}
	return -1;
}

/* 计算缓冲区总行数 */
PRIVATE void ed_count_lines(int idx)
{
	int i, count = 1;
	WINDOW* w = &windows[idx];
	for (i = 0; i < w->ed_buf_len; i++) {
		if (ed_buf[i] == '\n') count++;
	}
	ed_total_rows = count;
}

/* 获取第 line 行的起始偏移 */
PRIVATE int ed_line_offset(int idx, int line)
{
	WINDOW* w = &windows[idx];
	int i, cur_line = 0;
	for (i = 0; i < w->ed_buf_len && cur_line < line; i++) {
		if (ed_buf[i] == '\n') cur_line++;
	}
	return i;
}

/* 获取第 line 行的长度 (不含 \n) */
PRIVATE int ed_line_length(int idx, int line)
{
	WINDOW* w = &windows[idx];
	int off = ed_line_offset(idx, line);
	int len = 0;
	while (off + len < w->ed_buf_len && ed_buf[off + len] != '\n') len++;
	return len;
}

/* 将光标 row/col 转为缓冲区偏移 */
PRIVATE int ed_cursor_to_offset(int idx)
{
	WINDOW* w = &windows[idx];
	int off = ed_line_offset(idx, w->ed_cur_row);
	int line_len = ed_line_length(idx, w->ed_cur_row);
	if (w->ed_cur_col > line_len) w->ed_cur_col = line_len;
	return off + w->ed_cur_col;
}

/* 在光标处插入字符 */
PRIVATE void ed_insert_char(int idx, char ch)
{
	WINDOW* w = &windows[idx];
	int off = ed_cursor_to_offset(idx);
	int i;
	if (w->ed_buf_len >= ED_MAX_CHARS - 1) return;
	for (i = w->ed_buf_len; i > off; i--) ed_buf[i] = ed_buf[i - 1];
	ed_buf[off] = ch;
	w->ed_buf_len++;
	ed_buf[w->ed_buf_len] = 0;
	if (ch == '\n') {
		w->ed_cur_row++;
		w->ed_cur_col = 0;
	} else {
		w->ed_cur_col++;
	}
	w->ed_modified = 1;
	ed_count_lines(idx);
}

/* 删除光标前一个字符 (Backspace) */
PRIVATE void ed_delete_char(int idx)
{
	WINDOW* w = &windows[idx];
	int off = ed_cursor_to_offset(idx);
	int i;
	if (off == 0) return;
	for (i = off - 1; i < w->ed_buf_len; i++) ed_buf[i] = ed_buf[i + 1];
	w->ed_buf_len--;
	ed_buf[w->ed_buf_len] = 0;
	w->ed_modified = 1;
	/* 调整光标 */
	if (w->ed_cur_col > 0) {
		w->ed_cur_col--;
	} else if (w->ed_cur_row > 0) {
		w->ed_cur_row--;
		w->ed_cur_col = ed_line_length(idx, w->ed_cur_row);
	}
	ed_count_lines(idx);
}

/* 保存文件 */
PRIVATE void ed_save(int idx)
{
	WINDOW* w = &windows[idx];
	char fname[16];
	int i;
	/* 将文件名转为大写 (FAT 8.3 格式) */
	for (i = 0; i < 15 && w->ed_filename[i]; i++) {
		char c = w->ed_filename[i];
		fname[i] = (c >= 'a' && c <= 'z') ? c - 32 : c;
	}
	fname[i] = 0;
	if (fat_write_file_in(w->ed_cluster, fname, ed_buf, w->ed_buf_len) >= 0) {
		w->ed_modified = 0;
	}
}

/* 调整滚动位置使光标可见 */
PRIVATE void ed_ensure_visible(int idx, int visible_rows)
{
	WINDOW* w = &windows[idx];
	if (w->ed_cur_row < w->ed_top_row)
		w->ed_top_row = w->ed_cur_row;
	if (w->ed_cur_row >= w->ed_top_row + visible_rows)
		w->ed_top_row = w->ed_cur_row - visible_rows + 1;
}

/* 创建编辑器窗口并加载文件 */
PUBLIC int wm_create_editor(const char *filename, t_32 cluster)
{
	int idx;
	int n;
	WINDOW* w;

	/* 单实例: 关闭已有编辑器 */
	{
		int old = ed_find_win();
		if (old >= 0) { windows[old].visible = 0; wm_count--; }
	}

	idx = wm_create(20, 15, 40 * g_font_width, 20 * g_font_height, S_ED_TITLE);
	if (idx < 0) return -1;
	w = &windows[idx];
	w->type = WM_WT_EDITOR;
	w->ed_modified = 0;
	w->ed_cur_row = 0;
	w->ed_cur_col = 0;
	w->ed_top_row = 0;
	w->ed_buf_len = 0;
	w->ed_menu_open = 0;
	w->ed_cluster = cluster;  /* 保存文件所在目录簇号 */
	ed_buf[0] = 0;

	/* 复制文件名 */
	{
		int i;
		for (i = 0; i < 15 && filename[i]; i++)
			w->ed_filename[i] = filename[i];
		w->ed_filename[i] = 0;
	}

	/* 读取文件内容 (按 cluster 指定的目录) */
	fat_init();
	n = fat_read_file_in(w->ed_cluster, w->ed_filename, ed_buf, ED_MAX_CHARS - 1);
	if (n < 0) {
		/* 文件不存在或读取失败 → 空白编辑器 */
		ed_buf[0] = 0;
		w->ed_buf_len = 0;
	} else {
		w->ed_buf_len = n;
		ed_buf[n] = 0;
	}
	ed_count_lines(idx);

	/* 更新标题: "Notepad - " + 文件名 (手动复制避免 .rodata 寻址) */
	{
		int i, j = 0;
		for (i = 0; S_ED_PREFIX[i]; i++)
			w->title[j++] = S_ED_PREFIX[i];
		for (i = 0; j < 31 && w->ed_filename[i]; i++)
			w->title[j++] = w->ed_filename[i];
		w->title[j] = 0;
	}

	wm_redraw_all();
	return idx;
}

/* 绘制编辑器客户区 */
PRIVATE void wm_draw_ed_client(int idx)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;
	int text_cy = cy + ED_MENU_H;              /* 文本区顶部 y */
	int text_w = cw - ED_SCROLL_W;             /* 文本区宽 (留滚动条) */
	int text_h = ch - ED_MENU_H - ED_STATUS_H; /* 文本区高 (留菜单+状态栏) */
	int cols = (text_w - ED_PAD_X * 2) / g_font_width;
	int rows = (text_h - ED_PAD_Y * 2) / g_font_height;
	int r, c;

	/* 白色背景 */
	fill_rect(cx, cy, cw, ch, WM_COL_ED_BG);

	/* === 顶部菜单栏 === */
	{
		int my = cy;
		fill_rect(cx, my, cw, ED_MENU_H, WM_COL_ED_MENU_BG);
		/* 底部分隔线 */
		draw_hline(cx, cx + cw - 1, my + ED_MENU_H - 1, WM_COL_BORDER_DARK);
		/* "File" 菜单项 */
		{
			int fx = cx + 4;
			int fw = 5 * g_font_width;  /* "File" = 4 字符 + 留白 */
			t_8 bg, fg;
			if (win->ed_menu_open) {
				bg = WM_COL_ED_MENU_SEL;
				fg = WM_COL_ED_MENU_STXT;
			} else {
				bg = WM_COL_ED_MENU_BG;
				fg = WM_COL_ED_MENU_TEXT;
			}
			fill_rect(fx, my + 1, fw, ED_MENU_H - 2, bg);
			draw_string(fx + 2, my + 3, S_ED_MENU_FILE, fg, bg);
		}
	}

	/* === 文本区 === */
	/* 绘制可见文本行 */
	for (r = 0; r < rows; r++) {
		int line = win->ed_top_row + r;
		int off = ed_line_offset(idx, line);
		int line_len = ed_line_length(idx, line);
		int py = text_cy + ED_PAD_Y + r * g_font_height;
		for (c = 0; c < cols && c < line_len; c++) {
			char ch = ed_buf[off + c];
			if (ch >= 32) {
				draw_char(cx + ED_PAD_X + c * g_font_width,
				          py, ch, WM_COL_ED_TEXT, WM_COL_ED_BG);
			}
		}
	}

	/* 绘制光标 (下划线) */
	if (win->ed_cur_row >= win->ed_top_row &&
	    win->ed_cur_row < win->ed_top_row + rows) {
		int py = text_cy + ED_PAD_Y +
		          (win->ed_cur_row - win->ed_top_row) * g_font_height;
		int px = cx + ED_PAD_X + win->ed_cur_col * g_font_width;
		if (px + g_font_width > cx + text_w)
			px = cx + text_w - g_font_width;
		fill_rect(px, py + g_font_height - 2, g_font_width, 2,
		          WM_COL_ED_CURSOR);
	}

	/* 右侧滚动条 */
	{
		int sb_x = cx + cw - ED_SCROLL_W;
		int sb_y = text_cy;
		int sb_h = text_h;
		fill_rect(sb_x, sb_y, ED_SCROLL_W, sb_h, WM_COL_ED_SCROLL);
		if (ed_total_rows > rows) {
			int thumb_h = (rows * sb_h) / ed_total_rows;
			int thumb_y = (win->ed_top_row * sb_h) / ed_total_rows;
			if (thumb_h < 4) thumb_h = 4;
			fill_rect(sb_x, sb_y + thumb_y, ED_SCROLL_W, thumb_h,
			          WM_COL_ED_THUMB);
		}
	}

	/* === 底部状态栏 === */
	{
		int sy = text_cy + text_h;
		char num[12];
		fill_rect(cx, sy, cw, ED_STATUS_H, WM_COL_ED_STATUS);
		/* 文件名 */
		draw_string(cx + 4, sy + 2, win->ed_filename,
		            0x0F, WM_COL_ED_STATUS);
		/* 修改标记 */
		if (win->ed_modified) {
			draw_string(cx + 4 + 100, sy + 2, S_ED_STAR,
			            WM_COL_ED_MODIFIED, WM_COL_ED_STATUS);
		}
		/* 行/列 */
		draw_string(cx + cw - 80, sy + 2, S_ED_LN,
		            0x0F, WM_COL_ED_STATUS);
		num[0] = '0' + (win->ed_cur_row + 1) / 10;
		num[1] = '0' + (win->ed_cur_row + 1) % 10;
		num[2] = 0;
		draw_string(cx + cw - 62, sy + 2, num, 0x0F, WM_COL_ED_STATUS);
		draw_string(cx + cw - 50, sy + 2, S_ED_COL,
		            0x0F, WM_COL_ED_STATUS);
		num[0] = '0' + (win->ed_cur_col + 1) / 10;
		num[1] = '0' + (win->ed_cur_col + 1) % 10;
		num[2] = 0;
		draw_string(cx + cw - 26, sy + 2, num, 0x0F, WM_COL_ED_STATUS);
	}

	/* === 下拉菜单 (File 展开) === */
	if (win->ed_menu_open) {
		int mx0 = cx + 4;
		int my0 = cy + ED_MENU_H;
		int mw = ED_MENU_W;
		int mh = ED_MENU_ITEM_H * 2 + 4;
		/* 阴影 */
		fill_rect(mx0 + 2, my0 + 2, mw, mh, WM_COL_SHADOW);
		/* 背景 */
		fill_rect(mx0, my0, mw, mh, WM_COL_ED_MENU_BG);
		/* 边框 */
		draw_hline(mx0, mx0 + mw - 1, my0, 0x00);
		draw_hline(mx0, mx0 + mw - 1, my0 + mh - 1, 0x00);
		draw_vline(mx0, my0, my0 + mh - 1, 0x00);
		draw_vline(mx0 + mw - 1, my0, my0 + mh - 1, 0x00);
		/* Save 项 */
		draw_string(mx0 + 6, my0 + 3, S_ED_M_SAVE,
		            WM_COL_ED_MENU_TEXT, WM_COL_ED_MENU_BG);
		/* 分隔线 */
		draw_hline(mx0 + 2, mx0 + mw - 3, my0 + ED_MENU_ITEM_H, 0x08);
		/* Close 项 */
		draw_string(mx0 + 6, my0 + ED_MENU_ITEM_H + 3, S_ED_M_CLOSE,
		            WM_COL_ED_MENU_TEXT, WM_COL_ED_MENU_BG);
	}
}

/* 编辑器键盘处理 */
PRIVATE void ed_handle_key(int idx, t_32 key)
{
	WINDOW* w = &windows[idx];
	int visible_rows = (w->h - WM_TITLE_H - WM_BORDER_3D - ED_MENU_H - ED_STATUS_H - ED_PAD_Y * 2) / g_font_height;
	int visible_cols = (w->w - 2 * WM_BORDER_3D - ED_SCROLL_W - ED_PAD_X * 2) / g_font_width;
	int line_len;

	if (!(key & FLAG_EXT)) {
		char ch = (char)(key & 0xFF);
		if (ch >= 32 && ch < 127) {
			ed_insert_char(idx, ch);
		} else if (ch == '\r' || ch == '\n') {
			ed_insert_char(idx, '\n');
		}
	} else {
		/* 已在 FLAG_EXT 分支, 直接比较完整键值 (与 wm_run 一致) */
		if (key == BACKSPACE) {
			ed_delete_char(idx);
		} else if (key == ENTER) {
			ed_insert_char(idx, '\n');
		} else if (key == LEFT) {
			if (w->ed_cur_col > 0) w->ed_cur_col--;
			else if (w->ed_cur_row > 0) {
				w->ed_cur_row--;
				w->ed_cur_col = ed_line_length(idx, w->ed_cur_row);
			}
		} else if (key == RIGHT) {
			line_len = ed_line_length(idx, w->ed_cur_row);
			if (w->ed_cur_col < line_len) w->ed_cur_col++;
			else if (w->ed_cur_row < ed_total_rows - 1) {
				w->ed_cur_row++;
				w->ed_cur_col = 0;
			}
		} else if (key == UP) {
			if (w->ed_cur_row > 0) w->ed_cur_row--;
			line_len = ed_line_length(idx, w->ed_cur_row);
			if (w->ed_cur_col > line_len) w->ed_cur_col = line_len;
		} else if (key == DOWN) {
			if (w->ed_cur_row < ed_total_rows - 1) w->ed_cur_row++;
			line_len = ed_line_length(idx, w->ed_cur_row);
			if (w->ed_cur_col > line_len) w->ed_cur_col = line_len;
		} else if (key == PAGEUP) {
			w->ed_top_row -= visible_rows;
			if (w->ed_top_row < 0) w->ed_top_row = 0;
		} else if (key == PAGEDOWN) {
			w->ed_top_row += visible_rows;
			if (w->ed_top_row > ed_total_rows - visible_rows)
				w->ed_top_row = ed_total_rows - visible_rows;
			if (w->ed_top_row < 0) w->ed_top_row = 0;
		} else if (key == HOME) {
			w->ed_cur_col = 0;
		} else if (key == END) {
			w->ed_cur_col = ed_line_length(idx, w->ed_cur_row);
		}
	}
	ed_ensure_visible(idx, visible_rows);
	(void)visible_cols;
}

/* 编辑器鼠标点击处理: 菜单栏 + 下拉菜单 */
PRIVATE void ed_handle_click(int idx, int mx, int my)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;
	int menu_cy = cy;                 /* 菜单栏顶部 */
	int text_cy = cy + ED_MENU_H;     /* 文本区顶部 */

	/* 点击在菜单栏区域 */
	if (my >= menu_cy && my < menu_cy + ED_MENU_H &&
	    mx >= cx && mx < cx + cw) {
		/* "File" 菜单项区域 */
		int fx = cx + 4;
		int fw = 5 * g_font_width;
		if (mx >= fx && mx < fx + fw) {
			win->ed_menu_open = !win->ed_menu_open;
			return;
		}
		/* 菜单栏其他位置: 关闭菜单 */
		if (win->ed_menu_open) {
			win->ed_menu_open = 0;
			return;
		}
		return;
	}

	/* 菜单已展开: 检查是否点击在下拉菜单项上 */
	if (win->ed_menu_open) {
		int mx0 = cx + 4;
		int my0 = cy + ED_MENU_H;
		int mw = ED_MENU_W;
		int mh = ED_MENU_ITEM_H * 2 + 4;
		if (mx >= mx0 && mx < mx0 + mw &&
		    my >= my0 && my < my0 + mh) {
			int item = (my - my0) / ED_MENU_ITEM_H;
			win->ed_menu_open = 0;
			if (item == 0) {
				/* Save */
				ed_save(idx);
			} else if (item == 1) {
				/* Close (跳过分隔线区域) */
				wm_close(idx);
			}
			return;
		}
		/* 点击菜单外: 关闭菜单 */
		win->ed_menu_open = 0;
		return;
	}

	/* 点击在文本区: 移动光标到点击位置 */
	{
		int text_w = cw - ED_SCROLL_W;
		int text_h = ch - ED_MENU_H - ED_STATUS_H;
		if (my >= text_cy && my < text_cy + text_h &&
		    mx >= cx && mx < cx + text_w) {
			int col = (mx - cx - ED_PAD_X) / g_font_width;
			int row = (my - text_cy - ED_PAD_Y) / g_font_height;
			int line_len;
			int visible_rows = (text_h - ED_PAD_Y * 2) / g_font_height;
			int target_row = win->ed_top_row + row;
			if (target_row < 0) target_row = 0;
			if (target_row >= ed_total_rows) target_row = ed_total_rows - 1;
			win->ed_cur_row = target_row;
			line_len = ed_line_length(idx, win->ed_cur_row);
			if (col < 0) col = 0;
			if (col > line_len) col = line_len;
			win->ed_cur_col = col;
			(void)visible_rows;
		}
	}
}


/*======================================================================*
                          "打开方式" 弹窗 (Open With)
 *======================================================================*/

/* 弹窗选项 */
#define OW_ITEM_H    16
#define OW_COUNT      2   /* 选项数: Notepad / Terminal */

/* 命名字符串 (非const, 放入 .data 段, 避免 .rodata 寻址问题) */
PRIVATE char S_OW_NOTEPAD[]  = "Notepad";
PRIVATE char S_OW_TERMINAL[] = "Terminal";
PRIVATE char S_OW_TITLE[]    = "Open With...";

/* 创建"打开方式"弹窗 */
PUBLIC int wm_create_openwith(const char *filename, t_32 cluster)
{
	int idx;
	WINDOW* w;

	idx = wm_create(80, 60, 20 * g_font_width, 6 * g_font_height, S_OW_TITLE);
	if (idx < 0) return -1;
	w = &windows[idx];
	w->type = WM_WT_OPENWITH;
	w->ow_target_cluster = cluster;
	w->fm_selected = 0;  /* 复用为弹窗选中项 */
	/* 保存文件名 */
	{
		int i;
		for (i = 0; i < 15 && filename[i]; i++)
			w->ed_filename[i] = filename[i];
		w->ed_filename[i] = 0;
	}
	wm_redraw_all();
	return idx;
}

/* 绘制"打开方式"弹窗客户区 */
PRIVATE void wm_draw_ow_client(int idx)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int ch = win->h - WM_TITLE_H - WM_BORDER_3D;
	int i;
	char *ow_items[OW_COUNT];
	ow_items[0] = S_OW_NOTEPAD;
	ow_items[1] = S_OW_TERMINAL;

	fill_rect(cx, cy, cw, ch, WM_COL_OW_BG);

	for (i = 0; i < OW_COUNT; i++) {
		int y = cy + 2 + i * OW_ITEM_H;
		t_8 bg, fg;
		if (i == win->fm_selected) {
			bg = WM_COL_OW_SEL;
			fg = WM_COL_OW_SEL_TEXT;
		} else {
			bg = WM_COL_OW_BG;
			fg = WM_COL_OW_TEXT;
		}
		fill_rect(cx + 2, y, cw - 4, OW_ITEM_H - 2, bg);
		draw_string(cx + 6, y + 2, ow_items[i], fg, bg);
	}
}

/*======================================================================*
                          wm_draw_uc_client  (用户控制台窗口客户区)
 *======================================================================*/
/* 从 user_consoles[slot].text/attr 合成到屏幕客户区 */
PRIVATE void wm_draw_uc_client(int idx)
{
	WINDOW* win = &windows[idx];
	int slot = win->uc_owner;
	struct USER_CONSOLE *uc;
	int cx, cy, cw, ch;
	int r, c;

	if (slot < 0 || slot >= NR_USER_PROCS) return;
	uc = &user_consoles[slot];
	if (!uc->active) return;

	cx = win->x + WM_BORDER_3D;
	cy = win->y + WM_TITLE_H;
	cw = win->w - 2 * WM_BORDER_3D;
	ch = win->h - WM_TITLE_H - WM_BORDER_3D;

	/* 客户区黑色背景 */
	fill_rect(cx, cy, cw, ch, 0x00);

	/* 绘制文本缓冲 (8x16 字体, 34 列 11 行) */
	for (r = 0; r < UC_ROWS; r++) {
		int row_y = cy + r * g_font_height;
		if (row_y + g_font_height > cy + ch) break;
		for (c = 0; c < UC_COLS; c++) {
			int col_x = cx + c * g_font_width;
			if (col_x + g_font_width > cx + cw) break;
			{
				char ch2 = uc->text[r][c];
				t_8 attr = uc->attr[r][c];
				t_8 fg = attr & 0x0F;
				t_8 bg = (attr >> 4) & 0x0F;
				if (bg == 0) bg = 0x00;  /* 黑底 */
				draw_char(col_x, row_y, ch2, fg, bg);
			}
		}
	}

	/* 绘制光标 (闪烁: cursor_visible 控制可见性) */
	if (uc->cursor_visible) {
		int cx_cur = cx + uc->cur_col * g_font_width;
		int cy_cur = cy + uc->cur_row * g_font_height;
		if (cx_cur + g_font_width <= cx + cw &&
		    cy_cur + g_font_height <= cy + ch) {
			/* 反相显示光标位置 */
			draw_char(cx_cur, cy_cur, '_', 0x0F, 0x00);
		}
	}
}

/*======================================================================*
                          wm_draw_uw_client  (用户自定义窗口客户区)
 *======================================================================*/
/* 从 user_windows[uw_idx].canvas 合成到屏幕客户区 */
PRIVATE void wm_draw_uw_client(int idx)
{
	WINDOW* win = &windows[idx];
	int uw_idx = win->uw_canvas_idx;
	struct USER_WINDOW *uw;
	int cx, cy, cw, ch;
	int r, c;

	if (uw_idx < 0 || uw_idx >= UW_MAX) return;
	uw = &user_windows[uw_idx];
	if (!uw->active) return;

	cx = win->x + WM_BORDER_3D;
	cy = win->y + WM_TITLE_H;
	cw = win->w - 2 * WM_BORDER_3D;
	ch = win->h - WM_TITLE_H - WM_BORDER_3D;

	/* 合成画布到屏幕 */
	for (r = 0; r < uw->ch && r < ch; r++) {
		for (c = 0; c < uw->cw && c < cw; c++) {
			put_pixel(cx + c, cy + r, uw->canvas[r * uw->cw + c]);
		}
	}
}

/* 弹窗点击处理: 返回 1=已处理, 0=点击外部(应关闭) */
PRIVATE int ow_handle_click(int idx, int px, int py)
{
	WINDOW* win = &windows[idx];
	int cx = win->x + WM_BORDER_3D;
	int cy = win->y + WM_TITLE_H;
	int cw = win->w - 2 * WM_BORDER_3D;
	int i;

	/* 点击在弹窗外 → 关闭 */
	if (px < cx || px >= cx + cw ||
	    py < cy || py >= cy + (win->h - WM_TITLE_H - WM_BORDER_3D)) {
		windows[idx].visible = 0;
		wm_count--;
		wm_redraw_all();
		return 1;
	}

	/* 点击在选项上 */
	for (i = 0; i < OW_COUNT; i++) {
		int y = cy + 2 + i * OW_ITEM_H;
		if (py >= y && py < y + OW_ITEM_H - 2) {
			/* 选中该选项 → 执行并关闭弹窗 */
			char fname[16];
			int j;
			t_32 dir_cluster = windows[idx].ow_target_cluster;
			windows[idx].visible = 0;
			wm_count--;
			for (j = 0; j < 15 && windows[idx].ed_filename[j]; j++)
				fname[j] = windows[idx].ed_filename[j];
			fname[j] = 0;

			if (i == 0) {
				/* Notepad */
				wm_create_editor(fname, dir_cluster);
			} else {
				/* Terminal: 尝试运行该文件.
				 * 若是 .ce 可执行文件 → 启动并自动分配控制台窗口.
				 * 否则 → 打开终端窗口 (旧行为). */
				int len = j;
				int is_ce = 0;
				if (len >= 3 &&
				    fname[len-3] == '.' &&
				    (fname[len-2] == 'c' || fname[len-2] == 'C') &&
				    (fname[len-1] == 'e' || fname[len-1] == 'E')) {
					is_ce = 1;
				}
				if (is_ce && user_proc_free_slots > 0) {
					/* 运行 .ce 文件, exec_user_program_in 会自动创建控制台窗口 */
					int ret = exec_user_program_in(dir_cluster, fname);
					if (ret < 0) {
						/* 运行失败, 打开终端显示错误 */
						wm_create_terminal();
					}
				} else {
					/* 非 .ce 文件或槽位已满, 打开终端窗口 */
					wm_create_terminal();
				}
			}
			wm_redraw_all();
			return 1;
		}
	}
	return 1;
}


/*======================================================================*
                          窗口操作: 最小化/最大化/关闭
 *======================================================================*/
PRIVATE void wm_minimize(int idx)
{
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	windows[idx].state = WM_WS_MINIMIZED;
	{
		int i, found = -1;
		for (i = 0; i < WM_MAX_WINDOWS; i++) {
			if (windows[i].visible && windows[i].state == WM_WS_NORMAL) {
				found = i; break;
			}
		}
		wm_active = found;
	}
	wm_redraw_all();
}

PRIVATE void wm_maximize(int idx)
{
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	if (windows[idx].state == WM_WS_MAXIMIZED) {
		windows[idx].state = WM_WS_NORMAL;
		windows[idx].x = windows[idx].prev_x;
		windows[idx].y = windows[idx].prev_y;
		windows[idx].w = windows[idx].prev_w;
		windows[idx].h = windows[idx].prev_h;
	} else {
		windows[idx].prev_x = windows[idx].x;
		windows[idx].prev_y = windows[idx].y;
		windows[idx].prev_w = windows[idx].w;
		windows[idx].prev_h = windows[idx].h;
		windows[idx].state = WM_WS_MAXIMIZED;
		/* 一次性设置最大化尺寸, 避免 wm_draw_window 每帧重设 */
		windows[idx].x = 0;
		windows[idx].y = 0;
		windows[idx].w = g_screen_width;
		windows[idx].h = g_screen_height - WM_TASKBAR_H;
	}
	wm_redraw_all();
}

PRIVATE void wm_close(int idx)
{
	if (idx < 0 || idx >= WM_MAX_WINDOWS) return;
	windows[idx].visible = 0;
	windows[idx].state = WM_WS_NORMAL;
	wm_count--;
	{
		int i, found = -1;
		for (i = WM_MAX_WINDOWS - 1; i >= 0; i--) {
			if (windows[i].visible && windows[i].state != WM_WS_MINIMIZED) {
				found = i; break;
			}
		}
		wm_active = found;
	}
	wm_redraw_all();
}


/*======================================================================*
                          主窗口应用打开
 *======================================================================*/
PRIVATE void wm_open_main_app(int app_id)
{
	switch (app_id) {
	case WM_APP_FILEMANAGER:
		wm_create_fm(0);
		break;
	case WM_APP_CALCULATOR:
		wm_create_calculator();
		break;
	case WM_APP_TERMINAL:
		wm_create_terminal();
		break;
	case WM_APP_ABOUT:
		wm_create_about();
		break;
	}
}


/*======================================================================*
                          wm_run (主循环)
 *======================================================================*/
/* wm_step 的静态状态 (从 wm_run 提取, 供 shell 前台等待时调用) */
PRIVATE t_32 wm_s_key = 0;
PRIVATE int wm_s_mx = 0, wm_s_my = 0;
PRIVATE int wm_s_prev_mx = -1, wm_s_prev_my = -1;
PRIVATE int wm_s_btns = 0;
PRIVATE int wm_s_prev_btns = 0;
PRIVATE int wm_s_running = 1;
PRIVATE int wm_s_cursor_blast = 0;

/* wm_step: 窗口管理器单步处理 (一次事件循环迭代)
 * 提取自 wm_run 主循环, 供 shell 前台等待时调用, 避免 wm_run 卡死.
 * 返回值: 1=继续运行, 0=请求退出桌面 (ESC 或主窗口关闭) */
PUBLIC int wm_step(void)
{
	int i;  /* 循环变量 */

	/* 0. 重绘所有标记为 dirty 的窗口 (用户程序异步输出时设置 dirty) */
	for (i = 0; i < WM_MAX_WINDOWS; i++) {
		if (windows[i].visible && windows[i].dirty) {
			wm_draw_window_only(i);
		}
	}

	/* 0b. 控制台光标闪烁: 每 25 ticks (250ms) 翻转一次可见状态 */
	if (ticks - wm_s_cursor_blast >= 25) {
		wm_s_cursor_blast = ticks;
		for (i = 0; i < NR_USER_PROCS; i++) {
			if (user_consoles[i].active &&
			    !user_consoles[i].exiting &&
			    user_consoles[i].win_idx >= 0) {
				user_consoles[i].cursor_visible =
				    !user_consoles[i].cursor_visible;
				wm_uc_invalidate(i);
			}
		}
	}

	/* 1. 键盘事件 */
	wm_s_key = 0;
	keyboard_read_simple(&wm_s_key);
	if (wm_s_key == ESC) { wm_s_running = 0; return 0; }

	if (wm_s_key == TAB) {
		int found = -1;
		for (i = wm_active + 1; i < WM_MAX_WINDOWS; i++) {
			if (windows[i].visible && windows[i].state != WM_WS_MINIMIZED) {
				found = i; break;
			}
		}
		if (found < 0) {
			for (i = 0; i <= wm_active; i++) {
				if (windows[i].visible && windows[i].state != WM_WS_MINIMIZED) {
					found = i; break;
				}
			}
		}
		if (found >= 0 && found != wm_active) wm_set_active(found);
		wm_s_key = 0;
	}

	/* 终端键盘路由 */
	if (wm_s_key != 0 && wm_active >= 0 &&
	    windows[wm_active].type == WM_WT_TERMINAL &&
	    windows[wm_active].state == WM_WS_NORMAL) {
		term_handle_key(wm_s_key);
		wm_draw_window_only(wm_active);
		wm_s_key = 0;
	}

	/* 用户控制台键盘路由 */
	if (wm_s_key != 0) {
		int uc_slot = -1;
		int uc_exiting = 0;
		for (i = 0; i < NR_USER_PROCS; i++) {
			if (user_consoles[i].active &&
			    user_consoles[i].win_idx >= 0 &&
			    windows[user_consoles[i].win_idx].state == WM_WS_NORMAL) {
				if (!user_consoles[i].exiting) {
					uc_slot = i;
					break;
				} else if (uc_slot < 0) {
					uc_slot = i;
					uc_exiting = 1;
				}
			}
		}
		if (uc_slot >= 0) {
			if (uc_exiting) {
				wm_force_close_user_console(uc_slot);
			} else {
				wm_uc_push_key(uc_slot, wm_s_key);
				wm_uc_invalidate(uc_slot);
			}
			wm_s_key = 0;
		}
	}

	/* 文件管理器键盘路由 */
	if (wm_s_key != 0 && wm_active >= 0 &&
	    windows[wm_active].type == WM_WT_FILEMANAGER &&
	    windows[wm_active].state == WM_WS_NORMAL) {
		if (wm_s_key == ENTER) {
			int sel = windows[wm_active].fm_selected;
			if (sel >= 0 && sel < windows[wm_active].fm_count) {
				fm_open_entry(wm_active, sel);
			}
			wm_s_key = 0;
		}
	}

	/* 用户自定义窗口键盘路由 */
	if (wm_s_key != 0 && wm_active >= 0 &&
	    windows[wm_active].type == WM_WT_USERWINDOW &&
	    windows[wm_active].state == WM_WS_NORMAL) {
		wm_uw_push_event(wm_active, UW_EV_KEY, (int)wm_s_key, 0);
		wm_s_key = 0;
	}

	/* 编辑器键盘路由 */
	if (wm_s_key != 0 && wm_active >= 0 &&
	    windows[wm_active].type == WM_WT_EDITOR &&
	    windows[wm_active].state == WM_WS_NORMAL) {
		if ((wm_s_key & FLAG_CTRL_L) && (wm_s_key & 0xFF) == 's') {
			ed_save(wm_active);
		} else if ((wm_s_key & FLAG_CTRL_L) && (wm_s_key & 0xFF) == 'S') {
			ed_save(wm_active);
		} else {
			ed_handle_key(wm_active, wm_s_key);
		}
		wm_draw_window_only(wm_active);
		wm_s_key = 0;
	}

	if (wm_s_key != 0 && wm_active >= 0 && windows[wm_active].state == WM_WS_NORMAL) {
		int step = 8;
		if (wm_s_key == LEFT)       { wm_move_window(wm_active, -step, 0); }
		else if (wm_s_key == RIGHT) { wm_move_window(wm_active, step, 0); }
		else if (wm_s_key == UP)    { wm_move_window(wm_active, 0, -step); }
		else if (wm_s_key == DOWN)  { wm_move_window(wm_active, 0, step); }
	}

	/* 2. 鼠标事件 */
	wm_s_mx = mouse_get_smooth_x();
	wm_s_my = mouse_get_smooth_y();
	wm_s_btns = mouse_get_buttons();

	if (wm_s_mx != wm_s_prev_mx || wm_s_my != wm_s_prev_my) {
		if (drag_active && wm_active >= 0) {
			/* 拖拽中: 只移动橡皮筋边框, 不重绘整个窗口.
			 * 用桌面背景擦除旧边框矩形, 在新位置画边框.
			 * 底层其他窗口内容会被擦除, 但拖动结束时会全量重绘恢复. */
			WINDOW* win = &windows[wm_active];
			int new_x = wm_s_mx - drag_off_x;
			int new_y = wm_s_my - drag_off_y;
			/* 边界裁剪 (与 wm_move_window 一致) */
			if (new_x < -win->w + 30) new_x = -win->w + 30;
			if (new_y < 0) new_y = 0;
			if (new_x + 30 > g_screen_width) new_x = g_screen_width - 30;
			if (new_y + win->h > g_screen_height - WM_TASKBAR_H)
				new_y = g_screen_height - WM_TASKBAR_H - win->h;
			if (new_x != drag_box_x || new_y != drag_box_y) {
				gfx_hide_mouse_cursor();
				/* 擦除旧边框 (覆盖整个旧矩形, 含 2px 边框) */
				fill_rect(drag_box_x, drag_box_y, win->w, win->h,
				          WM_COL_DESKTOP);
				/* 画新边框 */
				drag_box_x = new_x;
				drag_box_y = new_y;
				wm_draw_drag_box(drag_box_x, drag_box_y, win->w, win->h);
				gfx_show_mouse_cursor();
			}
		}
		wm_s_prev_mx = wm_s_mx;
		wm_s_prev_my = wm_s_my;
	}

	/* 左键按下边沿 */
	if ((wm_s_btns & 0x01) && !(wm_s_prev_btns & 0x01)) {
		/* 优先处理 "打开方式" 弹窗 */
		{
			int ow = -1;
			for (i = 0; i < WM_MAX_WINDOWS; i++) {
				if (windows[i].visible &&
				    windows[i].type == WM_WT_OPENWITH) {
					ow = i; break;
				}
			}
			if (ow >= 0) {
				ow_handle_click(ow, wm_s_mx, wm_s_my);
				wm_s_prev_btns = wm_s_btns;
				goto step_done;
			}
		}
		int hit;
		hit = wm_hit_test_taskbar(wm_s_mx, wm_s_my);
		if (hit >= 0) {
			wm_set_active(hit);
		} else {
			int clicked = wm_find_window_at(wm_s_mx, wm_s_my);
			if (clicked >= 0) {
				if (clicked != wm_active) {
					wm_set_active(clicked);
				}
				{
					int btn = wm_hit_test_btn(wm_active, wm_s_mx, wm_s_my);
					if (btn == 1) {
						wm_maximize(wm_active);
					} else if (btn == 2) {
						wm_minimize(wm_active);
					} else if (btn == 3) {
						if (wm_active == main_window) {
							wm_s_running = 0;
							return 0;
						} else if (windows[wm_active].type == WM_WT_USERCONSOLE) {
							wm_kill_user_proc(windows[wm_active].uc_owner);
						} else if (windows[wm_active].type == WM_WT_USERWINDOW) {
							if (wm_uw_get_closable(wm_active)) {
								wm_uw_push_event(wm_active, UW_EV_CLOSE, 0, 0);
								wm_uw_close(wm_active);
							}
						} else {
							wm_close(wm_active);
						}
					} else if (wm_hit_test_title(wm_active, wm_s_mx, wm_s_my)) {
						/* 拖拽开始: 记录原位置, 隐藏窗口内容, 只画橡皮筋边框.
						 * 拖动期间不再调用 wm_move_window (避免每帧完整重绘导致卡顿). */
						WINDOW* dw = &windows[wm_active];
						drag_active = 1;
						drag_off_x = wm_s_mx - dw->x;
						drag_off_y = wm_s_my - dw->y;
						drag_orig_x = dw->x;
						drag_orig_y = dw->y;
						drag_box_x  = dw->x;
						drag_box_y  = dw->y;
						gfx_hide_mouse_cursor();
						/* 用桌面背景擦除原窗口矩形 */
						fill_rect(dw->x, dw->y, dw->w, dw->h, WM_COL_DESKTOP);
						/* 重绘该区域下的其他可见窗口 (保持 Z-order) */
						{
							int k;
							for (k = 0; k < WM_MAX_WINDOWS; k++) {
								WINDOW* w2 = &windows[k];
								if (!w2->visible || k == wm_active ||
								    w2->state == WM_WS_MINIMIZED) continue;
								if (w2->x < dw->x + dw->w &&
								    w2->x + w2->w > dw->x &&
								    w2->y < dw->y + dw->h &&
								    w2->y + w2->h > dw->y) {
									wm_draw_window(k);
								}
							}
						}
						/* 画初始橡皮筋边框 */
						wm_draw_drag_box(drag_box_x, drag_box_y, dw->w, dw->h);
						gfx_show_mouse_cursor();
					} else {
						/* 客户区点击 */
						if (windows[wm_active].type == WM_WT_MAIN) {
							int app = wm_hit_test_main_icon(wm_active, wm_s_mx, wm_s_my);
							if (app >= 0) {
								if (main_dclick_app == app &&
								    (ticks - main_dclick_ticks) < WM_DCLICK_TICKS) {
									wm_open_main_app(app);
									main_dclick_app = -1;
								} else {
									main_dclick_app = app;
									main_dclick_ticks = ticks;
								}
							}
						} else if (windows[wm_active].type == WM_WT_FILEMANAGER) {
							int row = fm_hit_test_row(wm_active, wm_s_mx, wm_s_my);
							if (row >= 0) {
								if (fm_dclick_win == wm_active &&
								    fm_dclick_row == row &&
								    (ticks - fm_dclick_ticks) < WM_DCLICK_TICKS) {
									fm_open_entry(wm_active, row);
									fm_dclick_win = -1;
								} else {
									windows[wm_active].fm_selected = row;
									fm_dclick_win = wm_active;
									fm_dclick_row = row;
									fm_dclick_ticks = ticks;
									wm_draw_window_only(wm_active);
								}
							}
						} else if (windows[wm_active].type == WM_WT_CALCULATOR) {
							int act = calc_hit_test(wm_active, wm_s_mx, wm_s_my);
							if (act >= 0) {
								calc_do_action(wm_active, act);
								wm_draw_window_only(wm_active);
							}
						} else if (windows[wm_active].type == WM_WT_EDITOR) {
							ed_handle_click(wm_active, wm_s_mx, wm_s_my);
							wm_draw_window_only(wm_active);
						} else if (windows[wm_active].type == WM_WT_USERWINDOW) {
							WINDOW* w = &windows[wm_active];
							int cx = w->x + WM_BORDER_3D;
							int cy = w->y + WM_TITLE_H;
							int rx = wm_s_mx - cx;
							int ry = wm_s_my - cy;
							if (rx >= 0 && ry >= 0) {
								wm_uw_push_event(wm_active, UW_EV_LDOWN, rx, ry);
							}
						}
					}
				}
			}
		}
	}

	/* 左键释放 */
	if (!(wm_s_btns & 0x01) && (wm_s_prev_btns & 0x01)) {
		if (drag_active && wm_active >= 0) {
			/* 拖拽结束: 擦除橡皮筋边框, 把窗口实际移动到最终位置, 全量重绘.
			 * 此处一次性 wm_redraw_all 恢复所有被擦除的底层窗口内容. */
			WINDOW* win = &windows[wm_active];
			gfx_hide_mouse_cursor();
			/* 擦除最后边框矩形 */
			fill_rect(drag_box_x, drag_box_y, win->w, win->h,
			          WM_COL_DESKTOP);
			/* 更新窗口坐标到最终位置 (含边界裁剪保护) */
			win->x = drag_box_x;
			win->y = drag_box_y;
			drag_active = 0;
			/* 全量重绘: 桌面 + 所有窗口 (按 Z-order) + 任务栏 */
			wm_redraw_all();
			gfx_show_mouse_cursor();
		} else {
			drag_active = 0;
		}
		if (wm_active >= 0 &&
		    windows[wm_active].type == WM_WT_USERWINDOW &&
		    windows[wm_active].state == WM_WS_NORMAL) {
			WINDOW* w = &windows[wm_active];
			int cx = w->x + WM_BORDER_3D;
			int cy = w->y + WM_TITLE_H;
			int rx = wm_s_mx - cx;
			int ry = wm_s_my - cy;
			if (rx >= 0 && ry >= 0 && rx < w->w - 2*WM_BORDER_3D && ry < w->h - WM_TITLE_H - WM_BORDER_3D) {
				wm_uw_push_event(wm_active, UW_EV_LUP, rx, ry);
			}
		}
	}

	wm_s_prev_btns = wm_s_btns;

step_done:
	/* 3. 每帧更新平滑鼠标 + 绘制光标 */
	mouse_draw_cursor();
	return 1;
}

PUBLIC void wm_run(void)
{
	int saved_prio;  /* 保存 task_tty 原优先级, 退出时恢复 */

	wm_init();
	/* 主窗口尺寸: 占屏幕 3/4 宽度, 2/3 高度 (随分辨率缩放) */
	main_window = wm_create(g_screen_width / 8, g_screen_height / 8,
	                         g_screen_width * 3 / 4,
	                         g_screen_height * 2 / 3, "CatOS Main Window");
	if (main_window >= 0) {
		windows[main_window].type = WM_WT_MAIN;
	}
	gfx_set_cursor(-1);
	gfx_hide_cursor();
	wm_redraw_all();
	gfx_show_mouse_cursor();

	/* 降低 task_tty 优先级到与用户进程相同 (5), 让用户进程公平调度 */
	saved_prio = proc_table[0].priority;
	proc_table[0].priority = 5;
	proc_table[0].ticks = 5;

	/* 重置 wm_step 状态 */
	wm_s_prev_mx = -1;
	wm_s_prev_my = -1;
	wm_s_prev_btns = 0;
	wm_s_running = 1;
	wm_s_cursor_blast = 0;

	while (wm_s_running) {
		wm_step();
	}

	/* 退出: 清屏, 恢复文本光标 + 恢复 task_tty 优先级 */
	gfx_hide_mouse_cursor();
	fill_rect(0, 0, g_screen_width, g_screen_height, 0);
	gfx_set_cursor(0);
	gfx_show_cursor();
	proc_table[0].priority = saved_prio;
	proc_table[0].ticks = saved_prio;
}
