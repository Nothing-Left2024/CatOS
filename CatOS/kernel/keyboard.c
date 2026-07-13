
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            keyboard.c
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
#include "keyboard.h"
#include "keymap.h"
#include "proto.h"

PRIVATE	KB_INPUT	kb_in;
PRIVATE	t_bool		code_with_E0	= FALSE;
PRIVATE	t_bool		shift_l;		/* l shift state	*/
PRIVATE	t_bool		shift_r;		/* r shift state	*/
PRIVATE	t_bool		alt_l;			/* l alt state		*/
PRIVATE	t_bool		alt_r;			/* r left state		*/
PRIVATE	t_bool		ctrl_l;			/* l ctrl state		*/
PRIVATE	t_bool		ctrl_r;			/* l ctrl state		*/
PRIVATE	t_bool		caps_lock;		/* Caps Lock		*/
PRIVATE	t_bool		num_lock;		/* Num Lock		*/
PRIVATE	t_bool		scroll_lock;		/* Scroll Lock		*/
PRIVATE	int		column		= 0;	/* keyrow[column] 指向 keymap 的某一行值 */

/* 本文件内函数声明 */
PRIVATE t_8	get_byte_from_kb_buf();
PRIVATE void	set_leds();
PRIVATE void	kb_wait();
PRIVATE void	kb_ack();

/*======================================================================*
                            keyboard_handler
 *======================================================================*/
PUBLIC void keyboard_handler(int irq)
{
	t_8 scan_code = in_byte(KB_DATA);

	if (kb_in.count < KB_IN_BYTES) {
		*(kb_in.p_head) = scan_code;
		kb_in.p_head++;
		if (kb_in.p_head == kb_in.buf + KB_IN_BYTES) {
			kb_in.p_head = kb_in.buf;
		}
		kb_in.count++;
	}
}


/*======================================================================*
                           init_keyboard
 *======================================================================*/
PUBLIC void init_keyboard()
{
	kb_in.count = 0;
	kb_in.p_head = kb_in.p_tail = kb_in.buf;

	code_with_E0	= FALSE;
	shift_l		= FALSE;
	shift_r		= FALSE;
	alt_l		= FALSE;
	alt_r		= FALSE;
	ctrl_l		= FALSE;
	ctrl_r		= FALSE;
	caps_lock	= FALSE;
	num_lock	= TRUE;
	scroll_lock	= FALSE;
	column		= 0;

	set_leds();

	put_irq_handler(KEYBOARD_IRQ, keyboard_handler);	/* 设定键盘中断处理程序 */
	enable_irq(KEYBOARD_IRQ);				/* 开启键盘中断 */
}


/*======================================================================*
                           keyboard_read
 *======================================================================*/
PUBLIC void keyboard_read(TTY* p_tty)
{
	t_8	scan_code;
	t_bool	make;	/* TRUE : make  */
			/* FALSE: break */
	t_32	key = 0;/* 用一个整数的各个位来表示一个按键 */
			/* 例如，当 Home 键按下，key 值变为定义在 keyboard.h 中的 'HOME'。*/
	t_32*	keyrow;	/* 指向 keymap[] 的某一行 */

	if(kb_in.count > 0){
		code_with_E0 = FALSE;
		scan_code = get_byte_from_kb_buf();

		/* 下面开始解析扫描码 */
		if (scan_code == 0xE1) {
			int i;
			t_8 pausebreak_scan_code[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
			t_bool is_pausebreak = TRUE;
			for(i=1;i<6;i++){
				if (get_byte_from_kb_buf() != pausebreak_scan_code[i]) {
					is_pausebreak = FALSE;
					break;
				}
			}
			if (is_pausebreak) {
				key = PAUSEBREAK;
			}
		}
		else if (scan_code == 0xE0) {
			code_with_E0 = TRUE;
			scan_code = get_byte_from_kb_buf();

			/* PrintScreen 按下键 */
			if (scan_code == 0x2A) {
				code_with_E0 = FALSE;
				if ((scan_code = get_byte_from_kb_buf()) == 0xE0) {
					code_with_E0 = TRUE;
					if ((scan_code = get_byte_from_kb_buf()) == 0x37) {
						key = PRINTSCREEN;
						make = TRUE;
					}
				}
			}
			/* PrintScreen 释放键 */
			else if (scan_code == 0xB7) {
				code_with_E0 = FALSE;
				if ((scan_code = get_byte_from_kb_buf()) == 0xE0) {
					code_with_E0 = TRUE;
					if ((scan_code = get_byte_from_kb_buf()) == 0xAA) {
						key = PRINTSCREEN;
						make = FALSE;
					}
				}
			}
		} /* 处理其它 PrintScreen，当按下时 scan_code 为 0xE0 而不是那个值时 */
		if ((key != PAUSEBREAK) && (key != PRINTSCREEN)) {
			/* 首先判断Make Code 还是 Break Code */
			make = (scan_code & FLAG_BREAK ? FALSE : TRUE);
			
			/* 先定位到 keymap 中的行 */
			keyrow = &keymap[(scan_code & 0x7F) * MAP_COLS];

			column = 0;

			t_bool caps = shift_l || shift_r;
			if (caps_lock) {
				if ((keyrow[0] >= 'a') && (keyrow[0] <= 'z')){
					caps = !caps;
				}
			}
			if (caps) {
				column = 1;
			}

			if (code_with_E0) {
				column = 2;
			}

			key = keyrow[column];

			switch(key) {
			case SHIFT_L:
				shift_l	= make;
				break;
			case SHIFT_R:
				shift_r	= make;
				break;
			case CTRL_L:
				ctrl_l	= make;
				break;
			case CTRL_R:
				ctrl_r	= make;
				break;
			case ALT_L:
				alt_l	= make;
				break;
			case ALT_R:
				alt_l	= make;
				break;
			case CAPS_LOCK:
				if (make) {
					caps_lock   = !caps_lock;
					set_leds();
				}
				break;
			case NUM_LOCK:
				if (make) {
					num_lock    = !num_lock;
					set_leds();
				}
				break;
			case SCROLL_LOCK:
				if (make) {
					scroll_lock = !scroll_lock;
					set_leds();
				}
				break;
			default:
				break;
			}
		}

		if(make){ /* 只处理 Break Code */
			t_bool pad = FALSE;

			/* 先处理小键盘 */
			if ((key >= PAD_SLASH) && (key <= PAD_9)) {
				pad = TRUE;
				switch(key) {	/* '/', '*', '-', '+', and 'Enter' in num pad  */
				case PAD_SLASH:
					key = '/';
					break;
				case PAD_STAR:
					key = '*';
					break;
				case PAD_MINUS:
					key = '-';
					break;
				case PAD_PLUS:
					key = '+';
					break;
				case PAD_ENTER:
					key = ENTER;
					break;
				default:	/* keys whose value depends on the NumLock */
					if (num_lock) {	/* '0' ~ '9' and '.' in num pad */
						if ((key >= PAD_0) && (key <= PAD_9)) {
							key = key - PAD_0 + '0';
						}
						else if (key == PAD_DOT) {
							key = '.';
						}
					}
					else{
						switch(key) {
						case PAD_HOME:
							key = HOME;
							break;
						case PAD_END:
							key = END;
							break;
						case PAD_PAGEUP:
							key = PAGEUP;
							break;
						case PAD_PAGEDOWN:
							key = PAGEDOWN;
							break;
						case PAD_INS:
							key = INSERT;
							break;
						case PAD_UP:
							key = UP;
							break;
						case PAD_DOWN:
							key = DOWN;
							break;
						case PAD_LEFT:
							key = LEFT;
							break;
						case PAD_RIGHT:
							key = RIGHT;
							break;
						case PAD_DOT:
							key = DELETE;
							break;
						default:
							break;
						}
					}
					break;
				}
			}
			key |= shift_l	? FLAG_SHIFT_L	: 0;
			key |= shift_r	? FLAG_SHIFT_R	: 0;
			key |= ctrl_l	? FLAG_CTRL_L	: 0;
			key |= ctrl_r	? FLAG_CTRL_R	: 0;
			key |= alt_l	? FLAG_ALT_L	: 0;
			key |= alt_r	? FLAG_ALT_R	: 0;
			key |= pad	? FLAG_PAD	: 0;

			in_process(p_tty, key);
		}
	}
}


/*======================================================================*
                           get_byte_from_kb_buf
 *======================================================================*/
PRIVATE t_8 get_byte_from_kb_buf()	/* 从键盘缓冲区中读取下一个字节 */
{
	t_8	scan_code;

	while (kb_in.count <= 0) {}	/* 等待下一个字节的到来 */

	disable_int();
	scan_code = *(kb_in.p_tail);
	kb_in.p_tail++;
	if (kb_in.p_tail == kb_in.buf + KB_IN_BYTES) {
		kb_in.p_tail = kb_in.buf;
	}
	kb_in.count--;
	enable_int();

#ifdef __TINIX_DEBUG__
	disp_color_str("[", MAKE_COLOR(WHITE,BLUE));
	disp_int(scan_code);
	disp_color_str("]", MAKE_COLOR(WHITE,BLUE));
#endif

	return scan_code;
}


/*======================================================================*
                                 kb_wait
 *======================================================================*/
PRIVATE void kb_wait()	/* 等待 8042 输入缓冲区空 */
{
	t_8 kb_stat;

	do {
		kb_stat = in_byte(KB_CMD);
	} while (kb_stat & 0x02);
}


/*======================================================================*
                                 kb_ack
 *======================================================================*/
PRIVATE void kb_ack()
{
	t_8 kb_read;

	do {
		kb_read = in_byte(KB_DATA);
	} while (kb_read =! KB_ACK);
}


/*======================================================================*
                                 set_leds
 *======================================================================*/
PRIVATE void set_leds()
{
	t_8 leds = (caps_lock << 2) | (num_lock << 1) | scroll_lock;

	kb_wait();
	out_byte(KB_DATA, LED_CODE);
	kb_ack();

	kb_wait();
	out_byte(KB_DATA, leds);
	kb_ack();
}


/*======================================================================*
                           keyboard_has_key
 *----------------------------------------------------------------------*
 * 查询键盘缓冲区是否有数据 (不消费数据), 供 sys_kbhit 使用.
 * 返回: TRUE = 有按键数据, FALSE = 无
 *======================================================================*/
PUBLIC t_bool keyboard_has_key(void)
{
	return (kb_in.count > 0) ? TRUE : FALSE;
}

/*======================================================================*
                           keyboard_read_simple
 *----------------------------------------------------------------------*
 * 简化版键盘读取: 非阻塞, 直接返回key值(不经过TTY缓冲区)
 * 返回值通过 *p_key 输出, 无按键时 *p_key = 0
 *======================================================================*/
PUBLIC void keyboard_read_simple(t_32* p_key)
{
	static t_bool pending_E0 = FALSE; /* 保存 0xE0 前缀状态, 跨调用保持 */

	*p_key = 0;

	if (kb_in.count <= 0) return; /* 无数据, 立即返回 */

	t_8 scan_code;
	t_32 key = 0;
	t_32* keyrow;
	int column = 0;

	code_with_E0 = FALSE;

	/* 如果上次收到了 0xE0 但第二字节未到, 这次继续处理 */
	if (pending_E0) {
		pending_E0 = FALSE;
		code_with_E0 = TRUE;
		scan_code = get_byte_from_kb_buf();
	} else {
		scan_code = get_byte_from_kb_buf();

		/* 处理 0xE0 扩展键前缀 (方向键/PageUp/PageDown等) */
		if (scan_code == 0xE0) {
			if (kb_in.count <= 0) {
				/* 第二字节未到, 保存状态等下次调用 */
				pending_E0 = TRUE;
				return;
			}
			code_with_E0 = TRUE;
			scan_code = get_byte_from_kb_buf();
		}
		/* 忽略 0xE1 (Pause/Break), 直接丢弃 */
		else if (scan_code == 0xE1) {
			return;
		}
	}

	{
		t_bool make = (scan_code & FLAG_BREAK ? FALSE : TRUE);
		t_8 code = scan_code & 0x7F;

		keyrow = &keymap[code * MAP_COLS];

		if (shift_l || shift_r) column = 1;
		if (code_with_E0) column = 2;

		key = keyrow[column];

		/* 处理修饰键状态 */
		switch(key) {
		case SHIFT_L: shift_l = make; return;
		case SHIFT_R: shift_r = make; return;
		case CTRL_L: ctrl_l = make; return;
		case CTRL_R: ctrl_r = make; return;
		case ALT_L: alt_l = make; return;
		case ALT_R: alt_r = make; return;
		case CAPS_LOCK: if(make){caps_lock=!caps_lock;set_leds();} return;
		case NUM_LOCK: if(make){num_lock=!num_lock;set_leds();} return;
		case SCROLL_LOCK: if(make){scroll_lock=!scroll_lock;set_leds();} return;
		default: break;
		}

		if (make) {
			key |= shift_l ? FLAG_SHIFT_L : 0;
			key |= shift_r ? FLAG_SHIFT_R : 0;
			key |= ctrl_l ? FLAG_CTRL_L : 0;
			key |= ctrl_r ? FLAG_CTRL_R : 0;
			key |= alt_l ? FLAG_ALT_L : 0;
			key |= alt_r ? FLAG_ALT_R : 0;
			*p_key = key;
		}
	}
}

/*======================================================================*
                           keyboard_get_flags
 *----------------------------------------------------------------------*
 * 返回修饰键状态位掩码:
 *   bit0  Shift-L    bit1  Shift-R
 *   bit2  Ctrl-L     bit3  Ctrl-R
 *   bit4  Alt-L      bit5  Alt-R
 *   bit6  CapsLock   bit7  NumLock
 *   bit8  ScrollLock
 *======================================================================*/
PUBLIC int keyboard_get_flags(void)
{
	int flags = 0;
	if (shift_l)     flags |= 0x001;
	if (shift_r)     flags |= 0x002;
	if (ctrl_l)      flags |= 0x004;
	if (ctrl_r)      flags |= 0x008;
	if (alt_l)       flags |= 0x010;
	if (alt_r)       flags |= 0x020;
	if (caps_lock)   flags |= 0x040;
	if (num_lock)    flags |= 0x080;
	if (scroll_lock) flags |= 0x100;
	return flags;
}


