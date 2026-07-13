/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                              wm.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
              CatOS Window Manager (Enhanced UI)
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _WM_H_
#define _WM_H_

#include "type.h"

/* ===== 窗口管理器常量 ===== */
/* g_font_height/g_font_width 由 gfx.c 根据屏幕分辨率自动设置,
 * 标题栏和按钮尺寸随字体大小动态调整, 确保高分辨率下 UI 比例协调 */
extern int g_font_height;
extern int g_font_width;

#define WM_MAX_WINDOWS  8       /* 最多窗口数 */
#define WM_TITLE_H      (g_font_height + 6)  /* 标题栏高度 (随字体缩放) */
#define WM_BORDER       2       /* 边框宽度 (像素) */
#define WM_BORDER_3D    3       /* 3D 边框总宽度 (像素) */
#define WM_BTN_W        (g_font_height - 4)  /* 标题栏按钮宽度 (随字体缩放) */
#define WM_BTN_H        (g_font_height - 4)  /* 标题栏按钮高度 (随字体缩放) */
#define WM_BTN_GAP      2       /* 按钮间距 */
#define WM_ICON_W       (g_font_height - 4)  /* 标题栏图标宽度 */
#define WM_ICON_H       (g_font_height - 4)  /* 标题栏图标高度 */

/* ===== 主窗口大图标 (随字体缩放, 确保文字不超出单元格) ===== */
/* 基础尺寸基于 8x16 字体: 图标 48x48, 单元格留出文字空间
 * 高分辨率 (16x32) 时按字体比例放大, 文字最多 12 字符 */
#define WM_ICON_LARGE_W    (6 * g_font_width)       /* 大图标宽度 (6 个字符宽) */
#define WM_ICON_LARGE_H    (3 * g_font_height)      /* 大图标高度 (3 个字符高) */
#define WM_ICON_CELL_W     (14 * g_font_width)      /* 单元格宽度 (最多 12 字符 + 间距) */
#define WM_ICON_CELL_H     (WM_ICON_LARGE_H + g_font_height + 6)  /* 图标 + 文字 + 间距 */

/* ===== 窗口状态标志 ===== */
#define WM_WS_NORMAL    0   /* 正常 */
#define WM_WS_MINIMIZED 1   /* 最小化 (到左下角) */
#define WM_WS_MAXIMIZED 2   /* 最大化 (全屏) */

/* ===== 窗口类型 ===== */
#define WM_WT_NORMAL       0   /* 普通窗口 (空客户区) */
#define WM_WT_MAIN         1   /* 主窗口 (含大图标排列) */
#define WM_WT_FILEMANAGER  2   /* 文件管理器 */
#define WM_WT_CALCULATOR   3   /* 计算器 */
#define WM_WT_ABOUT        4   /* 关于 */
#define WM_WT_TERMINAL     5   /* 终端 */
#define WM_WT_EDITOR       6   /* 记事本 (文本编辑器) */
#define WM_WT_OPENWITH     7   /* "打开方式" 选择弹窗 */
#define WM_WT_USERCONSOLE  8   /* 用户程序控制台窗口 (自动分配) */
#define WM_WT_USERWINDOW   9   /* 用户程序自定义窗口 (基础窗口 API) */

/* ===== 文件管理器常量 ===== */
/* 文件管理器布局 (随字体缩放, 确保文字不超出行高) */
#define WM_FM_ROW_H       (g_font_height + 4)  /* 文件列表行高 (字高+间距) */
#define WM_FM_ICON_W      g_font_width         /* 文件/文件夹图标宽 (1 个字符宽) */
#define WM_FM_ICON_H      g_font_height        /* 文件/文件夹图标高 (1 个字符高) */
#define WM_FM_MAX_ROWS    40                   /* 最多显示行数 */

/* ===== 双击检测 ===== */
#define WM_DCLICK_TICKS  40   /* 双击间隔 (系统 ticks, 100Hz = 400ms) */

/* ===== 主窗口应用图标 ID ===== */
#define WM_APP_FILEMANAGER  0  /* File Manager */
#define WM_APP_CALCULATOR   1  /* Calculator */
#define WM_APP_TERMINAL     2  /* Terminal */
#define WM_APP_ABOUT        3  /* About */

/* ===== 窗口结构体 ===== */
typedef struct {
	int x, y;           /* 左上角坐标 (像素, 最大化时忽略) */
	int w, h;           /* 宽高 (像素, 含标题栏和边框, 最大化时忽略) */
	int prev_x, prev_y; /* 最大化前的位置 (用于还原) */
	int prev_w, prev_h; /* 最大化前的大小 */
	char title[32];     /* 标题文字 */
	int visible;        /* 1=可见, 0=隐藏 */
	int state;          /* WM_WS_NORMAL / MINIMIZED / MAXIMIZED */
	int type;           /* 窗口类型 WM_WT_* */
	t_32 fm_dir_cluster; /* 文件管理器: 当前目录簇号 (0=根目录) */
	int fm_selected;    /* 文件管理器: 选中项索引 */
	int fm_count;        /* 文件管理器: 条目总数 (绘制时更新) */
	/* 计算器状态 */
	char calc_display[24]; /* 计算器显示文本 */
	t_32 calc_accum;     /* 累加器 */
	t_32 calc_operand;    /* 当前操作数 */
	int calc_operator;    /* 0=none 1=+ 2=- 3=* 4=/ */
	int calc_new_input;   /* 1=等待新输入 */
	/* 编辑器状态 */
	char ed_filename[16]; /* 编辑器: 文件名 (8.3格式) */
	int  ed_modified;      /* 编辑器: 是否已修改 */
	int  ed_cur_row;       /* 编辑器: 光标行 */
	int  ed_cur_col;       /* 编辑器: 光标列 */
	int  ed_top_row;       /* 编辑器: 可见区域顶部行 */
	int  ed_buf_len;       /* 编辑器: 缓冲区有效长度 */
	int  ed_menu_open;      /* 编辑器: 下拉菜单是否打开 (0=关, 1=File) */
	t_32 ed_cluster;       /* 编辑器: 文件所在目录簇号 (0=根目录) */
	/* "打开方式" 弹窗状态 */
	int  ow_target_cluster; /* 弹窗: 目标文件首簇 */
	/* 渲染状态 */
	int  dirty;             /* 脏标志: 1=需要重绘, 0=无需重绘 */
	/* 用户窗口/控制台窗口状态 */
	int  uc_owner;          /* 用户进程槽位 (0..NR_USER_PROCS-1), -1=无 */
	int  uw_owner;          /* 用户自定义窗口所有者槽位, -1=无 */
	int  uw_canvas_idx;     /* 用户画布索引 (0..UW_MAX-1), -1=无 */
} WINDOW;

/* ===== 颜色方案 (增强版) ===== */
#define WM_COL_DESKTOP      0x01        /* 桌面背景: 深蓝 */
#define WM_COL_TITLE_ACT   0x09        /* 活动标题栏: 亮蓝 */
#define WM_COL_TITLE_INACT 0x08        /* 非活动标题栏: 深灰 */
#define WM_COL_TITLE_TEXT  0x0F        /* 标题文字: 白 */
#define WM_COL_BORDER      0x0F        /* 窗口边框: 白 */
#define WM_COL_BORDER_DARK 0x08        /* 窗口内边框: 深灰 */
#define WM_COL_CLIENT      0x0F        /* 客户区背景: 白 */
#define WM_COL_ICON        0x0E        /* 图标: 黄 */
#define WM_COL_BTN        0x07        /* 按钮背景: 浅灰 */
#define WM_COL_BTN_TEXT   0x00        /* 按钮符号: 黑 */
#define WM_COL_TASKBAR     0x07        /* 任务栏背景: 浅灰 */
#define WM_COL_TASKBAR_TEXT 0x00      /* 任务栏文字: 黑 */

/* ===== 增强颜色 ===== */
#define WM_COL_SHADOW      0x08        /* 窗口阴影: 深灰 */
#define WM_COL_3D_HIGHLIGHT 0x0F       /* 3D 高光: 白 */
#define WM_COL_3D_DARK     0x00        /* 3D 暗边: 黑 */
#define WM_COL_TITLE_ACCENT 0x0B       /* 标题栏强调色: 亮青 */
#define WM_COL_ICON_BG     0x0B        /* 大图标背景: 亮青 */
#define WM_COL_ICON_BORDER 0x01        /* 大图标边框: 蓝 */
#define WM_COL_ICON_TEXT   0x00        /* 大图标文字: 黑 */
#define WM_COL_TASKBAR_CLOCK 0x0F      /* 任务栏时钟: 白 */

/* ===== 文件管理器颜色 ===== */
#define WM_COL_FM_SELECTED  0x01      /* 选中项背景: 蓝 */
#define WM_COL_FM_SEL_TEXT 0x0F      /* 选中项文字: 白 */
#define WM_COL_FM_DIR_TEXT 0x0B      /* 文件夹文字: 青 */
#define WM_COL_FM_FILE_TEXT 0x00     /* 文件文字: 黑 */
#define WM_COL_FM_DIR_ICON  0x0E     /* 文件夹图标: 黄 */
#define WM_COL_FM_FILE_ICON 0x07    /* 文件图标: 浅灰 */
#define WM_COL_FM_BTN       0x07    /* 主窗口按钮: 浅灰 */
#define WM_COL_FM_BTN_TEXT  0x00    /* 主窗口按钮文字: 黑 */

/* ===== 编辑器颜色 ===== */
#define WM_COL_ED_BG        0x0F    /* 编辑器背景: 白 */
#define WM_COL_ED_TEXT      0x00    /* 编辑器文字: 黑 */
#define WM_COL_ED_CURSOR    0x01    /* 光标线: 蓝 */
#define WM_COL_ED_STATUS    0x17    /* 状态栏: 白字蓝底 */
#define WM_COL_ED_MODIFIED  0x0C    /* 修改标记: 红 */
#define WM_COL_ED_SCROLL    0x08    /* 滚动条轨道: 深灰 */
#define WM_COL_ED_THUMB     0x07    /* 滚动条滑块: 浅灰 */
#define WM_COL_ED_MENU_BG   0x07    /* 菜单栏背景: 浅灰 */
#define WM_COL_ED_MENU_TEXT 0x00    /* 菜单栏文字: 黑 */
#define WM_COL_ED_MENU_SEL  0x01    /* 菜单选中: 蓝 */
#define WM_COL_ED_MENU_STXT 0x0F    /* 菜单选中文字: 白 */

/* ===== "打开方式" 弹窗颜色 ===== */
#define WM_COL_OW_BG        0x07    /* 弹窗背景: 浅灰 */
#define WM_COL_OW_TEXT      0x00    /* 弹窗文字: 黑 */
#define WM_COL_OW_SEL       0x01    /* 选中项: 蓝 */
#define WM_COL_OW_SEL_TEXT  0x0F    /* 选中项文字: 白 */

/* ===== API ===== */

/* 初始化窗口管理器 (清空所有窗口) */
PUBLIC void wm_init(void);

/* 创建窗口, 返回索引 (-1=失败: 已满) */
PUBLIC int wm_create(int x, int y, int w, int h, const char* title);

/* 创建文件管理器窗口, dir_cluster=0 表示根目录
   返回窗口索引 (-1=失败) */
PUBLIC int wm_create_fm(t_32 dir_cluster);

/* 绘制单个窗口 */
PUBLIC void wm_draw_window(int idx);

/* 重绘整个桌面 + 所有可见窗口 (按 Z-order) */
PUBLIC void wm_redraw_all(void);

/* 设置活动窗口 (提到 Z-order 顶层) */
PUBLIC void wm_set_active(int idx);

/* 获取活动窗口索引 */
PUBLIC int wm_get_active(void);

/* 移动窗口 (dx, dy 像素偏移) */
PUBLIC void wm_move_window(int idx, int dx, int dy);

/* 窗口管理器主循环 (输入 cat 进入, ESC 退出) */
PUBLIC void wm_run(void);
PUBLIC int  wm_step(void);  /* 单步处理事件, 供 shell 前台等待时调用 */

/* 标记单个窗口为脏 (需要重绘), 不立即绘制 */
PUBLIC void wm_invalidate(int idx);

/* 只重绘指定窗口 (含恢复其覆盖的桌面/底层窗口区域).
 * 用于终端/编辑器等需要频繁局部更新的场景, 避免全屏重绘. */
PUBLIC void wm_draw_window_only(int idx);

/* ===== 用户程序控制台窗口 API (内核内部) ===== */

/* 为用户进程 slot 创建控制台窗口, 返回窗口索引 (-1=失败) */
PUBLIC int  wm_create_user_console(int slot, const char *title);

/* 关闭用户进程 slot 的控制台窗口 (标记退出, 显示 Press any key 等待按键) */
PUBLIC void wm_close_user_console(int slot);

/* 强制关闭用户进程 slot 的控制台窗口 (立即释放窗口资源) */
PUBLIC void wm_force_close_user_console(int slot);

/* 强制终止指定槽位的用户进程, 并关闭其所有窗口 (控制台 + 子窗口)
 * 用于点击用户控制台窗口关闭按钮时直接结束进程 */
PUBLIC void wm_kill_user_proc(int slot);

/* 向用户进程 slot 的控制台写入字符 ch (attr 属性) */
PUBLIC void wm_uc_putc(int slot, char ch, t_8 attr);

/* 清空用户进程 slot 的控制台 */
PUBLIC void wm_uc_clear(int slot);

/* 向用户进程 slot 的控制台键盘队列推入按键 */
PUBLIC void wm_uc_push_key(int slot, t_32 key);

/* 从用户进程 slot 的控制台键盘队列弹出按键 (0=无按键) */
PUBLIC t_32 wm_uc_pop_key(int slot);

/* 获取用户进程 slot 的控制台窗口索引 (-1=无) */
PUBLIC int  wm_uc_get_win_idx(int slot);

/* 标记用户进程 slot 的控制台窗口需要重绘 */
PUBLIC void wm_uc_invalidate(int slot);

/* ===== 用户程序自定义窗口 API (内核内部) ===== */

/* 为用户进程 slot 创建自定义窗口, 返回窗口索引 (-1=失败) */
PUBLIC int  wm_uw_create(int slot, int x, int y, int w, int h, const char *title);

/* 关闭用户自定义窗口 (win_id 为 wm_uw_create 返回值) */
PUBLIC void wm_uw_close(int win_id);

/* 设置/查询用户窗口是否允许点击叉号关闭 (1=允许, 0=禁止) */
PUBLIC void wm_uw_set_closable(int win_id, int closable);
PUBLIC int  wm_uw_get_closable(int win_id);

/* 关闭指定 slot 的所有用户自定义窗口 (供 sys_exit 调用) */
PUBLIC void wm_uw_close_all_for_slot(int slot);

/* 在用户窗口画布上绘制文本 */
PUBLIC void wm_uw_draw_text(int win_id, int x, int y, const char *str, t_8 fg, t_8 bg);

/* 在用户窗口画布上绘制实心矩形 */
PUBLIC void wm_uw_draw_rect(int win_id, int x, int y, int w, int h, t_8 color);

/* 清空用户窗口画布 */
PUBLIC void wm_uw_clear(int win_id, t_8 color);

/* 标记用户窗口需要重绘 */
PUBLIC void wm_uw_invalidate(int win_id);

/* 向用户窗口推入事件 (鼠标点击等) */
PUBLIC void wm_uw_push_event(int win_id, int ev_type, int ev_x, int ev_y);

/* 从用户窗口弹出事件 (返回 0=无事件, >0=事件类型, 数据写入 *px/*py) */
PUBLIC int  wm_uw_pop_event(int win_id, int *px, int *py);

/* 设置画布上单个像素 */
PUBLIC void wm_uw_set_pixel(int win_id, int x, int y, t_8 color);

/* 在画布上画线 (Bresenham 算法) */
PUBLIC void wm_uw_draw_line(int win_id, int x1, int y1, int x2, int y2, t_8 color);

/* 设置窗口标题 */
PUBLIC void wm_uw_set_title(int win_id, const char *title);

/* 获取用户窗口客户区尺寸 (宽高写入 *pw/*ph) */
PUBLIC void wm_uw_get_size(int win_id, int *pw, int *ph);

#endif /* _WM_H_ */