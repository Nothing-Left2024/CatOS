
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                clock.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
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



/*======================================================================*
                           clock_handler
 *======================================================================*/
PUBLIC void clock_handler(int irq)
{
	ticks++;
	p_proc_ready->ticks--;

	/* 图形模式光标闪烁 (每 50 个tick = 500ms 翻转一次) */
	if (g_video_mode != 0 && (ticks % 50 == 0)) {
		gfx_toggle_cursor();
	}

	if (k_reenter != 0) {
		return;
	}

	if (p_proc_ready->ticks > 0) {
		return;
	}

	schedule();
}

/*======================================================================*
                              milli_delay
 *======================================================================*/
PUBLIC void milli_delay(int milli_sec)
{
	int t = get_ticks();

	while(((get_ticks() - t) * 1000 / HZ) < milli_sec) {}
}

/*======================================================================*
                           init_clock
 *======================================================================*/
PUBLIC void init_clock()
{
	/* 图形模式下跳过文本输出, 避免在图形显存中写入垃圾像素 */
	if (*((volatile t_8*)0x504) == 0) {
		disp_str("[INFO] init_clock(): configuring 8253 PIT timer (");
		disp_int(HZ);
		disp_str(" Hz)\n");
	}

	/* 初始化 8253 PIT */
	out_byte(TIMER_MODE, RATE_GENERATOR);
	out_byte(TIMER0, (t_8) (TIMER_FREQ/HZ) );
	out_byte(TIMER0, (t_8) ((TIMER_FREQ/HZ) >> 8));

	put_irq_handler(CLOCK_IRQ, clock_handler);	/* 设定时钟中断处理程序 */
	enable_irq(CLOCK_IRQ);			/* 让8259A可以接受时钟中断 */
}


