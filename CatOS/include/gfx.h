/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                              gfx.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                         CatOS Graphics Primitives
                                VGA Mode 13h (320x200x256)
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#ifndef _TINIX_GFX_H_
#define _TINIX_GFX_H_

#include "type.h"

/* ===== 全局图形参数 ===== */
EXTERN t_8  g_video_mode;      /* 0=TEXT, 1=VGA13, 2=VBE_LFB */
EXTERN t_32 g_vmem_base;      /* 显存基址 */
EXTERN t_16 g_screen_width;   /* 宽度 (像素或字符) */
EXTERN t_16 g_screen_height;  /* 高度 (像素或字符) */
EXTERN t_8  g_bytes_per_pixel;/* TEXT=2, VGA13=1 */
EXTERN t_16 g_screen_pitch;   /* 每行字节数 (pitch/stride) */

/* ===== 文本模式字符网格参数 ===== */
/* 文本模式: 80x25, 图形模式 Mode13h: 40x25 (8x8字体) */
EXTERN int  g_text_cols;       /* 字符列数 */
EXTERN int  g_text_rows;       /* 字符行数 */
EXTERN int  g_font_width;      /* 字体宽度 (像素) */
EXTERN int  g_font_height;     /* 字体高度 (像素) */

/* ===== 图形模式滚屏 ===== */
PUBLIC void gfx_scroll_up(int lines);
PUBLIC void gfx_clear_screen(t_8 color);
PUBLIC void gfx_clear_last_line(t_8 bg_color);

/* ===== VGA Mode 13h 绘制原语 ===== */
PUBLIC void put_pixel(int x, int y, t_8 color);
PUBLIC void draw_hline(int x1, int x2, int y, t_8 color);
PUBLIC void draw_vline(int x, int y1, int y2, t_8 color);
PUBLIC void fill_rect(int x, int y, int w, int h, t_8 color);
PUBLIC void clear_screen_gfx(t_8 color);

/* ===== 8x16 位图字体绘制 ===== */
PUBLIC void draw_char(int x, int y, char ch, t_8 fg, t_8 bg);
PUBLIC void draw_string(int x, int y, const char *str, t_8 fg, t_8 bg);
/* 绘制到任意像素缓冲区 (用于用户窗口画布) */
PUBLIC void draw_char_to_buf(t_8 *canvas, int cw, int ch,
                             int x, int y, char c, t_8 fg, t_8 bg);
PUBLIC void draw_string_to_buf(t_8 *canvas, int cw, int ch,
                               int x, int y, const char *str, t_8 fg, t_8 bg);

/* ===== 初始化 ===== */
PUBLIC void gfx_init(void);

/* ===== 软件光标 (图形模式) ===== */
PUBLIC void gfx_set_cursor(int byte_pos);   /* 设置光标位置 (逻辑字节位置) */
PUBLIC void gfx_show_cursor(void);          /* 显示光标 */
PUBLIC void gfx_hide_cursor(void);          /* 隐藏光标 */
PUBLIC void gfx_toggle_cursor(void);        /* 翻转光标状态 (用于闪烁) */
PUBLIC int  gfx_get_cursor_pos(void);       /* 获取当前光标位置 */

/* ===== 鼠标光标 (图形模式) ===== */
PUBLIC void gfx_set_mouse_cursor(int col, int row);  /* 设置鼠标光标位置 (字符坐标) */
PUBLIC void gfx_set_mouse_pixel_cursor(int px, int py); /* 设置鼠标光标位置 (像素坐标, WM 用) */
PUBLIC void gfx_hide_mouse_cursor(void);             /* 隐藏鼠标光标 */
PUBLIC void gfx_show_mouse_cursor(void);             /* 显示鼠标光标 */

#endif /* _TINIX_GFX_H_ */