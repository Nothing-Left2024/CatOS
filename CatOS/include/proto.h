
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            proto.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/* klib.asm */
PUBLIC void	out_byte(t_port port, t_8 value);
PUBLIC t_8	in_byte(t_port port);
PUBLIC void	disable_int();
PUBLIC void	enable_int();
PUBLIC void	disp_str(char * info);
PUBLIC void	disp_color_str(char * info, int color);
PUBLIC void	clear_screen(void);
PUBLIC void	vm_putc_asm(int pos, char ch, int color);
PUBLIC void	vm_putc(int pos, char ch, int color);
PUBLIC char	vm_getc(int pos);
PUBLIC unsigned char	vm_get_attr(int pos);

/* gfx.c */
PUBLIC void	put_pixel(int x, int y, t_8 color);
PUBLIC void	draw_hline(int x1, int x2, int y, t_8 color);
PUBLIC void	draw_vline(int x, int y1, int y2, t_8 color);
PUBLIC void	fill_rect(int x, int y, int w, int h, t_8 color);
PUBLIC void	clear_screen_gfx(t_8 color);
PUBLIC void	draw_char(int x, int y, char ch, t_8 fg, t_8 bg);
PUBLIC void	draw_string(int x, int y, const char *str, t_8 fg, t_8 bg);
PUBLIC void	gfx_init(void);
PUBLIC void	gfx_test(void);
PUBLIC void	disp_int(int input);

/* wm.c (窗口管理器) */
PUBLIC void	wm_run(void);

/* protect.c */
PUBLIC void	init_prot();
PUBLIC t_32	seg2phys(t_16 seg);
PUBLIC void	disable_irq(int irq);
PUBLIC void	enable_irq(int irq);

/* klib.c */
PUBLIC t_bool	is_alphanumeric(char ch);
PUBLIC void	delay(int time);
PUBLIC char *	itoa(char * str, int num);

/* kernel.asm */
PUBLIC void	restart();

/* main.c */
PUBLIC void	TestA();
PUBLIC void	TestB();
PUBLIC void	TestC();

/* i8259.c */
PUBLIC void	put_irq_handler(int iIRQ, t_pf_irq_handler handler);
PUBLIC void	spurious_irq(int irq);
PUBLIC void	init_8259A();

/* clock.c */
PUBLIC void	clock_handler(int irq);
PUBLIC void	milli_delay(int milli_sec);
PUBLIC void	init_clock();

/* proc.c */
PUBLIC void	schedule();

/* keyboard.c */
PUBLIC void	keyboard_handler(int irq);
PUBLIC void	keyboard_read(TTY* p_tty);
PUBLIC void	keyboard_read_simple(t_32* p_key);
PUBLIC t_bool	keyboard_has_key(void);
PUBLIC void	init_keyboard();
PUBLIC int	keyboard_get_flags(void);

/* mouse.c */
PUBLIC void	mouse_handler(int irq);
PUBLIC void	init_mouse();
PUBLIC void	mouse_draw_cursor();
PUBLIC void	mouse_hide_cursor();
PUBLIC int	mouse_get_x(void);          /* 字符坐标 X */
PUBLIC int	mouse_get_y(void);          /* 字符坐标 Y */
PUBLIC int	mouse_get_x_px(void);      /* 像素坐标 X (图形模式) */
PUBLIC int	mouse_get_y_px(void);      /* 像素坐标 Y (图形模式) */
PUBLIC int	mouse_get_buttons(void);   /* 按键状态: bit0=左,bit1=右,bit2=中 */
PUBLIC int	mouse_get_click(void);     /* 获取点击事件并清除 (1=有新点击) */
PUBLIC int	mouse_get_smooth_x(void);  /* 平滑像素坐标 X (与光标显示一致) */
PUBLIC int	mouse_get_smooth_y(void);  /* 平滑像素坐标 Y (与光标显示一致) */
PUBLIC void	mouse_update_smooth(void); /* 每帧更新平滑鼠标位置 */

/* tty.c */
PUBLIC void	task_tty();
PUBLIC void	in_process(TTY* p_tty, t_32 key);
PUBLIC void	tty_write(TTY* p_tty, char* buf, int len);

/* shell.c */
PUBLIC void	shell_parse_and_execute(char* cmdline, int *p_cursor);
PUBLIC void	shell_run(TTY* p_tty);

/* taskmgr.c */
PUBLIC void	taskmgr_run(int *p_cursor);

/* installer.c */
PUBLIC void	installer_run(int *p_cursor);

/* exec.c */
PUBLIC int	exec_user_program(const char* filename);
PUBLIC int	exec_user_program_in(t_32 dir_cluster, const char* filename);

/* console.c */
PUBLIC void	init_screen(TTY* p_tty);
PUBLIC void	out_char(CONSOLE* p_con, char ch);
PUBLIC void	out_char_color(CONSOLE* p_con, char ch, t_8 attr);
PUBLIC void	console_clrscr(CONSOLE* p_con);
PUBLIC void	console_gotoxy(CONSOLE* p_con, int x, int y);
PUBLIC void	scroll_screen(CONSOLE* p_con, int direction);
PUBLIC t_bool	is_current_console(CONSOLE* p_con);
PUBLIC void	select_console(int nr_console);

/* printf.c */
PUBLIC	int	printf(const char *fmt, ...);

/* vsprintf.c */
PUBLIC	int	vsprintf(char *buf, const char *fmt, va_list args);



/************************************************************************/
/*                        下面是系统调用相关                            */
/************************************************************************/


/*------------*/
/* 系统调用 */
/*------------*/

/* proc.c / syscall.c */
PUBLIC	int	sys_get_ticks	();
PUBLIC	int	sys_write	(char* buf, int len, PROCESS* p_proc);
PUBLIC	int	sys_putc	(int ch, int unused, PROCESS* p_proc);
PUBLIC	int	sys_puts	(int str_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_getc	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_exit	(int code, int unused, PROCESS* p_proc);
PUBLIC	int	sys_clrscr	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_gotoxy	(int x, int y, PROCESS* p_proc);
PUBLIC	int	sys_get_xy	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_get_cols	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_get_rows	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_get_vmode	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_set_color	(int color, int unused, PROCESS* p_proc);
PUBLIC	int	sys_putc_color	(int ch, int color, PROCESS* p_proc);
PUBLIC	int	sys_get_pid	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_get_name	(int buf_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_rand	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_srand	(int seed, int unused, PROCESS* p_proc);
PUBLIC	int	sys_put_int	(int n, int unused, PROCESS* p_proc);
PUBLIC	int	sys_put_uint	(int n, int unused, PROCESS* p_proc);
PUBLIC	int	sys_put_hex	(int n, int upper, PROCESS* p_proc);
PUBLIC	int	sys_put_bin	(int n, int unused, PROCESS* p_proc);
PUBLIC	int	sys_delay	(int ms, int unused, PROCESS* p_proc);
PUBLIC	int	sys_getch	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_kbhit	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_scroll	(int lines, int unused, PROCESS* p_proc);
PUBLIC	int	sys_set_io_size(int size, int unused, PROCESS* p_proc);
PUBLIC	int	sys_file_read	(int name_off, int buf_off, PROCESS* p_proc);
PUBLIC	int	sys_file_write	(int name_off, int buf_off, PROCESS* p_proc);
PUBLIC	int	sys_file_delete	(int name_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_file_exists	(int name_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_file_size	(int name_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_file_list	(int buf_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_reboot	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_get_key_flags(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_file_append	(int name_off, int buf_off, PROCESS* p_proc);
PUBLIC	int	sys_int_to_str	(int value, int buf_off, PROCESS* p_proc);
PUBLIC	int	sys_atoi	(int str_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_file_stat	(int name_off, int buf_off, PROCESS* p_proc);
PUBLIC	int	sys_spawn	(int name_off, int blocking, PROCESS* p_proc);
PUBLIC	int	sys_uc_putc	(int ch, int attr, PROCESS* p_proc);
PUBLIC	int	sys_uc_clear	(int unused1, int unused2, PROCESS* p_proc);
PUBLIC	int	sys_win_create	(int args_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_close	(int win_id, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_draw_text (int args_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_draw_rect (int args_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_get_event (int win_id, int ev_off, PROCESS* p_proc);
PUBLIC	int	sys_win_clear    (int args_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_set_title(int args_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_draw_line(int args_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_set_pixel(int args_off, int unused, PROCESS* p_proc);
PUBLIC	int	sys_win_peek_event(int win_id, int ev_off, PROCESS* p_proc);
PUBLIC	int	sys_win_get_size (int win_id, int size_off, PROCESS* p_proc);
PUBLIC	int	sys_win_set_closable(int win_id, int closable, PROCESS* p_proc);
PUBLIC	int	sys_win_get_closable(int win_id, int unused, PROCESS* p_proc);

/* syscall_c.c: 统一字符输出 (按 attr 属性), 供 sys_putc/puts/write/putc_color 共用 */
PUBLIC	void	user_put_char	(char ch, t_8 attr);

/* syscall.asm */
PUBLIC	void	sys_call();	/* t_pf_int_handler */


/*------------*/
/* 用户调用 */
/*------------*/

/* syscall.asm (内核侧包装, 用户程序实际使用 crt0.asm 中的对应版本) */
PUBLIC	int	get_ticks();
PUBLIC	void	write(char* buf, int len);
PUBLIC	void	putc(int ch);
PUBLIC	void	puts(char* str);
PUBLIC	int	getc(void);
PUBLIC	void	exit(int code);
PUBLIC	void	clrscr(void);
PUBLIC	void	gotoxy(int x, int y);
PUBLIC	int	get_xy(void);
PUBLIC	int	get_cols(void);
PUBLIC	int	get_rows(void);
PUBLIC	int	get_vmode(void);
PUBLIC	void	set_color(int color);
PUBLIC	void	putc_color(int ch, int color);
PUBLIC	int	get_pid(void);
PUBLIC	int	get_name(char* buf);
PUBLIC	int	rand(void);
PUBLIC	void	srand(int seed);
PUBLIC	void	put_int(int n);
PUBLIC	void	put_uint(unsigned int n);
PUBLIC	void	put_hex(unsigned int n, int upper);
PUBLIC	void	put_bin(unsigned int n);
PUBLIC	void	msleep(int ms);
PUBLIC	int	getch(void);
PUBLIC	int	kbhit(void);
PUBLIC	void	scroll(int lines);

/* 文件 IO (syscall_c.c) */
PUBLIC	void	set_io_size(int size);
PUBLIC	int	file_read(char* name, char* buf, int bufsize);
PUBLIC	int	file_write(char* name, char* buf, int size);
PUBLIC	int	file_delete(char* name);
PUBLIC	int	file_exists(char* name);
PUBLIC	int	file_size(char* name);
PUBLIC	int	file_list(char* buf, int bufsize);

/* 系统 (syscall_c.c) */
PUBLIC	void	reboot(void);
PUBLIC	int	get_key_flags(void);

/* 文件 IO 扩展 (syscall_c.c) */
PUBLIC	int	file_append(char* name, char* buf, int size);
PUBLIC	int	file_stat(char* name, int* info);

/* 类型转换 (syscall_c.c) */
PUBLIC	int	int_to_str(int value, char* buf, int radix);
PUBLIC	int	atoi(char* str);

/* 窗口 API (syscall.asm / crt0.asm 包装, NR 40-52) */
/* 用户控制台: 显式操作当前进程的控制台窗口 */
PUBLIC	void	uc_putc(int ch, int attr);
PUBLIC	void	uc_clear(void);
/* 用户自定义窗口: 基础窗口 API */
PUBLIC	int	win_create(int x, int y, int w, int h, char *title);
PUBLIC	void	win_close(int win_id);
PUBLIC	void	win_draw_text(int win_id, int x, int y, char *str, int fg, int bg);
PUBLIC	void	win_draw_rect(int win_id, int x, int y, int w, int h, int color);
PUBLIC	int	win_get_event(int win_id, int *type, int *x, int *y);
PUBLIC	void	win_clear(int win_id, int color);
PUBLIC	void	win_set_title(int win_id, char *title);
PUBLIC	void	win_draw_line(int win_id, int x1, int y1, int x2, int y2, int color);
PUBLIC	void	win_set_pixel(int win_id, int x, int y, int color);
PUBLIC	int	win_peek_event(int win_id, int *type, int *x, int *y);
PUBLIC	int	win_get_size(int win_id, int *w, int *h);

