/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                               mouse.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  CatOS PS/2 鼠标驱动 + 文本模式光标贴图

  功能:
    - 初始化 PS/2 鼠标 (通过 8042 控制器, IRQ 12)
    - 中断处理: 读取 3 字节数据包, 更新鼠标坐标
    - 文本模式光标: 反色显示鼠标位置下的字符 (贴图效果)

  注意:
    - IRQ 12 在从片, 必须同时 enable CASCADE_IRQ(2) 解除主片级联屏蔽
    - GS 段已由 tty.c 设置为 Video 段选择器 (0x1B)
    - 光标通过保存/恢复字符+属性, 反色显示实现, 不破坏原字符
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "const.h"
#include "protect.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "mouse.h"
#include "proto.h"
#include "gfx.h"


/*======================================================================
  鼠标状态 (全局变量, 供其他模块查询)
======================================================================*/
PRIVATE volatile int mouse_x     = 0;   /* 鼠标 X 坐标 (初始值会被 init_mouse 覆盖) */
PRIVATE volatile int mouse_y     = 0;   /* 鼠标 Y 坐标 (初始值会被 init_mouse 覆盖) */
PRIVATE volatile int mouse_btns  = 0;    /* 按键状态: bit0=左, bit1=右, bit2=中 */
PRIVATE volatile int mouse_cycle = 0;    /* 数据包字节计数 (0,1,2) */
PRIVATE volatile t_8 mouse_packet[3];    /* 3 字节数据包缓冲区 */
PRIVATE volatile int mouse_ready = 0;    /* 鼠标是否初始化完成 */

/* 灵敏度缩放: 原始 delta 较大, 文本模式 80x25 屏幕太小, 需要降速
   MOUSE_SCALE 越大越慢; sub_x/sub_y 保存亚像素余量, 避免慢速移动被截断丢失 */
#define MOUSE_SCALE  32
PRIVATE volatile int sub_x = 0;   /* X 亚像素累加器 */
PRIVATE volatile int sub_y = 0;   /* Y 亚像素累加器 */

/* 图形模式像素级坐标 (独立维护, 不再受字符粒度限制) */
PRIVATE volatile int mouse_px = 0;       /* 像素 X 坐标 */
PRIVATE volatile int mouse_py = 0;       /* 像素 Y 坐标 */
#define MOUSE_PX_SCALE  3               /* 图形模式降速因子 (3=每3个delta更新1像素, 降低灵敏度使移动更平滑) */

/* 平滑鼠标显示位置 (自适应指数平滑)
 * 根据距离动态调整跟随速率: 距离大时快速跟随, 距离小时慢速收敛
 * - 距离>32: 1/2 (快速跟随, 减少滞后)
 * - 距离8~32: 1/4 (平衡)
 * - 距离<8: 1/8 (慢速收敛, 消除微抖动) */
PRIVATE volatile int smooth_px = 0;
PRIVATE volatile int smooth_py = 0;

/* 点击事件检测 (左键按下边沿) */
PRIVATE volatile int mouse_prev_btns = 0;   /* 上次按键状态 (用于边沿检测) */
PRIVATE volatile int mouse_click_event = 0;  /* 点击事件标志: 1=有新点击事件 */

/* 光标渲染状态 */
PRIVATE int  cur_old_x    = -1;          /* 上次光标位置 */
PRIVATE int  cur_old_y    = -1;
PRIVATE t_8  cur_saved_ch = 0;           /* 保存的光标处原字符 */
PRIVATE t_8  cur_saved_at = 0;           /* 保存的光标处原属性 */
PRIVATE int  cur_active   = 0;           /* 光标是否处于激活(反色)状态 */


/*======================================================================
  读写 VGA 显存 (统一封装, 便于后续图形化适配)
======================================================================*/
static inline t_8 vga_read_byte(int pos)
{
	return vm_getc(pos);
}

static inline void vga_write_word(int pos, t_8 ch, t_8 attr)
{
	vm_putc(pos, ch, attr);
}


/*======================================================================
  8042 控制器辅助函数

  8042 状态寄存器 (端口 0x64) 关键位:
    bit0: 输出缓冲区满 (0x60 有数据可读)
    bit1: 输入缓冲区满 (0x60/0x64 暂不可写)
    bit5: aux (鼠标) 输出缓冲区满 (1=鼠标数据, 0=键盘数据)
======================================================================*/

/* 等待输入缓冲区空 (可以写命令/数据到 0x60/0x64) */
static void mouse_wait_write(void)
{
	int timeout = 100000;
	while (timeout-- > 0) {
		if (!(in_byte(MOUSE_PORT_STATUS) & 0x02)) return;
	}
}

/* 等待鼠标输出缓冲区满 (bit0=1 且 bit5=1, 确保读到的是鼠标数据而非键盘数据)
   若缓冲区里是键盘数据 (bit5=0), 丢弃后继续等, 避免误读键盘字节 */
static void mouse_wait_read(void)
{
	int timeout = 100000;
	t_8 s;
	while (timeout-- > 0) {
		s = in_byte(MOUSE_PORT_STATUS);
		if (s & 0x01) {
			if (s & 0x20) return;  /* 鼠标数据, 返回 */
			/* 键盘数据, 丢弃 (init_mouse 期间键盘已禁用, 不应出现, 但防御性处理) */
			in_byte(MOUSE_PORT_DATA);
		}
	}
}

/* 发送命令到鼠标 (通过 0xD4 前缀) */
static void mouse_send(t_8 cmd)
{
	mouse_wait_write();
	out_byte(MOUSE_PORT_CMD, 0xD4);
	mouse_wait_write();
	out_byte(MOUSE_PORT_DATA, cmd);
}

/* 读取鼠标应答 (0xFA=ACK) */
static t_8 mouse_read_ack(void)
{
	mouse_wait_read();
	return in_byte(MOUSE_PORT_DATA);
}

/* 清空 8042 输出缓冲区: 丢弃所有残留的键盘/鼠标数据, 防止 mouse_read_ack 误读键盘字节 */
static void mouse_drain_buffer(void)
{
	int timeout = 1000;
	t_8 s;
	while (timeout-- > 0) {
		s = in_byte(MOUSE_PORT_STATUS);
		if (!(s & 0x01)) return;  /* 输出缓冲区空 */
		in_byte(MOUSE_PORT_DATA); /* 丢弃一个字节 */
	}
}


/*======================================================================
  PS/2 鼠标中断处理 (IRQ 12)
  读取 3 字节数据包, 更新鼠标坐标

  数据包格式:
    Byte 0: [Y overflow][X overflow][Y sign][X sign][1][Mid][Right][Left]
    Byte 1: X delta (有符号)
    Byte 2: Y delta (有符号, 向上为正, 需取反)
======================================================================*/
PUBLIC void mouse_handler(int irq)
{
	t_8 s = in_byte(MOUSE_PORT_STATUS);
	t_8 data;

	/* 仅处理鼠标数据 (bit5=1); 若为键盘数据则丢弃, 不窃取键盘字节 */
	if (!(s & 0x20)) {
		/* 缓冲区里不是鼠标数据, 不读 (留给 keyboard_handler) */
		return;
	}
	data = in_byte(MOUSE_PORT_DATA);

	mouse_packet[mouse_cycle] = data;
	mouse_cycle++;

	if (mouse_cycle >= 3) {
		mouse_cycle = 0;

		/* 检查同步位 (bit3 必须为 1) */
		if (mouse_packet[0] & 0x08) {
			int dx, dy;
			int step_x, step_y;

			/* 解析 X 偏移量 */
			dx = (int)(signed char)mouse_packet[1];
			if (mouse_packet[0] & 0x10) {
				/* X sign bit 已包含在 signed char 转换中 */
			}

			/* 解析 Y 偏移量 (Y 向上为正, 屏幕向下为正, 取反) */
			dy = (int)(signed char)mouse_packet[2];
			dy = -dy;

			/* 溢出时忽略该轴的偏移 */
			if (mouse_packet[0] & 0x40) dx = 0;  /* X overflow */
			if (mouse_packet[0] & 0x80) dy = 0;  /* Y overflow */

			/* 灵敏度缩放: 累加亚像素余量, 整除 SCALE 后才移动一格
			   这样既降低速度, 又不会因慢速移动丢失 (余量会累积) */
			sub_x += dx;
			step_x = sub_x / MOUSE_SCALE;
			sub_x -= step_x * MOUSE_SCALE;

			sub_y += dy;
			step_y = sub_y / MOUSE_SCALE;
			sub_y -= step_y * MOUSE_SCALE;

			/* 更新坐标并限幅 (图形模式下使用动态 g_text_cols/rows) */
			mouse_x += step_x;
			mouse_y += step_y;

			int scr_w = (g_video_mode != 0) ? g_text_cols : MOUSE_SCR_W;
			int scr_h = (g_video_mode != 0) ? g_text_rows : MOUSE_SCR_H;

			if (mouse_x < 0) { mouse_x = 0; sub_x = 0; }
			if (mouse_x >= scr_w) { mouse_x = scr_w - 1; sub_x = 0; }
			if (mouse_y < 0) { mouse_y = 0; sub_y = 0; }
			if (mouse_y >= scr_h) { mouse_y = scr_h - 1; sub_y = 0; }

			/* 图形模式: 同时维护像素级坐标 (独立降速, 精度更高) */
			if (g_video_mode != 0) {
				int pstep_x, pstep_y;
				static volatile int psub_x = 0;
				static volatile int psub_y = 0;
				psub_x += dx;
				pstep_x = psub_x / MOUSE_PX_SCALE;
				psub_x -= pstep_x * MOUSE_PX_SCALE;
				psub_y += dy;
				pstep_y = psub_y / MOUSE_PX_SCALE;
				psub_y -= pstep_y * MOUSE_PX_SCALE;

				mouse_px += pstep_x;
				mouse_py += pstep_y;
				if (mouse_px < 0) mouse_px = 0;
				if (mouse_px >= g_screen_width)  mouse_px = g_screen_width - 1;
				if (mouse_py < 0) mouse_py = 0;
				if (mouse_py >= g_screen_height) mouse_py = g_screen_height - 1;

				/* 同步字符坐标 (供文本模式光标兼容) */
				mouse_x = mouse_px / g_font_width;
				mouse_y = mouse_py / g_font_height;
			}

			/* 更新按键状态 + 点击边沿检测 */
			mouse_btns = mouse_packet[0] & 0x07;
			/* 左键按下边沿 (之前未按下, 现在按下) */
			if ((mouse_btns & 0x01) && !(mouse_prev_btns & 0x01)) {
				mouse_click_event = 1;
			}
			mouse_prev_btns = mouse_btns;
		}
	}
}


/*======================================================================
  初始化 PS/2 鼠标
  1. 通过 8042 控制器启用辅助设备 (鼠标)
  2. 配置 8042 允许鼠标中断 (IRQ 12)
  3. 复位鼠标, 设置默认参数, 启用数据流模式
  4. 注册 IRQ 12 中断处理, 启用级联 IRQ 2
======================================================================*/
PUBLIC void init_mouse(void)
{
	t_8 config;
	int i;

	/* 步骤0: 清空 8042 输出缓冲区, 丢弃残留的键盘/鼠标数据
	   防止后续 mouse_read_ack 误读键盘字节导致键盘失灵 */
	mouse_drain_buffer();

	/* 步骤1: 禁用两个设备 (避免初始化期间产生干扰) */
	mouse_wait_write();
	out_byte(MOUSE_PORT_CMD, 0xAD);  /* 禁用键盘 */
	mouse_wait_write();
	out_byte(MOUSE_PORT_CMD, 0xA7);  /* 禁用鼠标 */

	/* 步骤2: 读取当前 8042 配置字节 */
	mouse_wait_write();
	out_byte(MOUSE_PORT_CMD, 0x20);
	mouse_wait_read();
	config = in_byte(MOUSE_PORT_DATA);

	/* 步骤3: 修改配置: 启用鼠标中断 (bit1), 保持键盘中断 (bit0) */
	config |= 0x02;    /* 启用 aux IRQ (IRQ12) */
	config &= ~0x20;   /* 禁用 aux 时钟屏蔽 */

	mouse_wait_write();
	out_byte(MOUSE_PORT_CMD, 0x60);
	mouse_wait_write();
	out_byte(MOUSE_PORT_DATA, config);

	/* 步骤4: 启用鼠标辅助设备 */
	mouse_wait_write();
	out_byte(MOUSE_PORT_CMD, 0xA8);

	/* 步骤4b: 再次清空缓冲区 (A8 可能让鼠标发字节) */
	mouse_drain_buffer();

	/* 步骤5: 复位鼠标 (0xFF), 鼠标回复 0xFA(ACK) + 0xAA(自测) + 0x00(ID) */
	mouse_send(0xFF);
	/* 复位会发 3 个字节, 用循环读取并丢弃非鼠标字节 */
	for (i = 0; i < 3; i++) {
		mouse_read_ack();
	}

	/* 步骤5b: 复位后再清一次缓冲区 */
	mouse_drain_buffer();

	/* 步骤6: 设置默认参数 */
	mouse_send(0xF6);
	mouse_read_ack();    /* 0xFA */

	/* 步骤7: 启用数据流模式 (鼠标开始发送数据包) */
	mouse_send(0xF4);
	mouse_read_ack();    /* 0xFA */

	/* 步骤8: 重新启用键盘 */
	mouse_wait_write();
	out_byte(MOUSE_PORT_CMD, 0xAE);

	/* 步骤9: 注册 IRQ 12 中断处理 */
	put_irq_handler(MOUSE_IRQ, mouse_handler);
	enable_irq(CASCADE_IRQ);  /* 解除主片 IR2 级联屏蔽 (关键!) */
	enable_irq(MOUSE_IRQ);    /* 解除从片 IR4 鼠标屏蔽 */

	/* 初始化鼠标位置到屏幕中央 (图形模式下使用动态尺寸) */
	mouse_x = (g_video_mode != 0 ? g_text_cols : MOUSE_SCR_W) / 2;
	mouse_y = (g_video_mode != 0 ? g_text_rows : MOUSE_SCR_H) / 2;
	/* 确保初始位置在有效范围内 */
	if (mouse_x < 0) mouse_x = 0;
	if (mouse_y < 0) mouse_y = 0;
	/* 图形模式: 初始化像素坐标到屏幕中央 */
	if (g_video_mode != 0) {
		mouse_px = g_screen_width / 2;
		mouse_py = g_screen_height / 2;
	}
	mouse_cycle = 0;
	mouse_btns = 0;
	mouse_prev_btns = 0;
	mouse_click_event = 0;
	sub_x = 0;
	sub_y = 0;
	smooth_px = mouse_px;
	smooth_py = mouse_py;
	mouse_ready = 1;
}


/*======================================================================
  mouse_update_smooth: 每帧更新平滑鼠标位置 (自适应指数平滑)
  在鼠标绘制前调用, 使光标平滑跟随原始位置

  自适应算法: 根据距离动态选择平滑系数
    - 距离>32: shift=1 (1/2 快速跟随, 减少大幅移动的滞后)
    - 距离8~32: shift=2 (1/4 平衡跟随与平滑)
    - 距离<8: shift=3 (1/8 慢速收敛, 消除停止时的微抖动)
  带四舍五入掩码确保慢速时也能逐步到达目标
======================================================================*/
PUBLIC void mouse_update_smooth(void)
{
	if (!mouse_ready) return;
	if (g_video_mode == 0) return;

	int dx = mouse_px - smooth_px;
	int dy = mouse_py - smooth_py;
	int abs_dx = (dx >= 0) ? dx : -dx;
	int abs_dy = (dy >= 0) ? dy : -dy;
	int shift_x, shift_y;

	/* 自适应选择 X 轴平滑系数 */
	if (abs_dx > 32) shift_x = 1;       /* 快速: 1/2 */
	else if (abs_dx > 8) shift_x = 2;   /* 中速: 1/4 */
	else shift_x = 3;                    /* 慢速: 1/8 */

	/* 自适应选择 Y 轴平滑系数 */
	if (abs_dy > 32) shift_y = 1;
	else if (abs_dy > 8) shift_y = 2;
	else shift_y = 3;

	/* 应用平滑 (带四舍五入掩码, 确保慢速时也能到达目标) */
	if (dx > 0) {
		int mask = (1 << shift_x) - 1;
		smooth_px += (dx + mask) >> shift_x;
	} else if (dx < 0) {
		int mask = (1 << shift_x) - 1;
		smooth_px += (dx - mask) >> shift_x;
	}

	if (dy > 0) {
		int mask = (1 << shift_y) - 1;
		smooth_py += (dy + mask) >> shift_y;
	} else if (dy < 0) {
		int mask = (1 << shift_y) - 1;
		smooth_py += (dy - mask) >> shift_y;
	}
}


/*======================================================================
  鼠标光标渲染
  - 文本模式: 反色字符贴图
  - 图形模式: 像素级方块光标 (gfx 模块提供)

  调用时机: TTY 主循环每次迭代时调用
  前提: 文本模式下 GS 段已设置为 Video 段选择器
======================================================================*/
PUBLIC void mouse_draw_cursor(void)
{
	if (!mouse_ready) return;

	/* 图形模式: 使用 gfx 模块的像素级鼠标光标 (用平滑坐标) */
	if (g_video_mode != 0) {
		mouse_update_smooth();
		gfx_set_mouse_pixel_cursor(smooth_px, smooth_py);
		cur_old_x = smooth_px;
		cur_old_y = smooth_py;
		cur_active = 1;
		return;
	}

	/* 文本模式: 反色字符贴图 */

	/* 如果光标位置没变, 不需要重绘 */
	if (cur_old_x == mouse_x && cur_old_y == mouse_y && cur_active) return;

	/* 擦除旧光标: 恢复保存的字符和属性 */
	if (cur_active && cur_old_x >= 0) {
		int old_pos = (cur_old_y * MOUSE_SCR_W + cur_old_x) * 2;
		vga_write_word(old_pos, cur_saved_ch, cur_saved_at);
		cur_active = 0;
	}

	/* 保存新位置的原字符和属性 */
	{
		int new_pos = (mouse_y * MOUSE_SCR_W + mouse_x) * 2;
		t_8 ch  = vga_read_byte(new_pos);
		t_8 at  = vga_read_byte(new_pos + 1);
		t_8 inv;

		cur_saved_ch = ch;
		cur_saved_at = at;

		/* 反色: 交换前景色和背景色 (高低 4 位互换) */
		inv = (t_8)(((at & 0x0F) << 4) | ((at & 0xF0) >> 4));
		vga_write_word(new_pos, ch, inv);

		cur_active = 1;
		cur_old_x = mouse_x;
		cur_old_y = mouse_y;
	}
}


/*======================================================================
  擦除鼠标光标 (恢复原字符/原像素)
  在 shell 输出或清屏前调用, 防止光标残留
======================================================================*/
PUBLIC void mouse_hide_cursor(void)
{
	/* 图形模式: 使用 gfx 模块隐藏 */
	if (g_video_mode != 0) {
		gfx_hide_mouse_cursor();
		cur_active = 0;
		return;
	}

	/* 文本模式: 恢复原字符 */
	if (cur_active && cur_old_x >= 0) {
		int old_pos = (cur_old_y * MOUSE_SCR_W + cur_old_x) * 2;
		vga_write_word(old_pos, cur_saved_ch, cur_saved_at);
		cur_active = 0;
	}
}


/*======================================================================
  PUBLIC 查询接口 (供窗口管理器等模块使用)

  - mouse_get_x / mouse_get_y: 字符坐标 (文本模式兼容)
  - mouse_get_x_px / mouse_get_y_px: 像素坐标 (图形模式)
  - mouse_get_buttons: 实时按键状态 (bit0=左, bit1=右, bit2=中)
  - mouse_get_click: 获取点击事件并清除 (1=有新点击, 0=无)
======================================================================*/
PUBLIC int mouse_get_x(void)
{
	return mouse_x;
}

PUBLIC int mouse_get_y(void)
{
	return mouse_y;
}

PUBLIC int mouse_get_x_px(void)
{
	return mouse_px;
}

PUBLIC int mouse_get_y_px(void)
{
	return mouse_py;
}

PUBLIC int mouse_get_buttons(void)
{
	return mouse_btns;
}

PUBLIC int mouse_get_click(void)
{
	int c = mouse_click_event;
	mouse_click_event = 0;   /* 读取即清除 */
	return c;
}

/* 获取平滑后的像素坐标 (供窗口管理器做命中测试, 与光标显示位置一致) */
PUBLIC int mouse_get_smooth_x(void)
{
	return smooth_px;
}

PUBLIC int mouse_get_smooth_y(void)
{
	return smooth_py;
}
