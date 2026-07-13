/* CatOS App */
// CE v1

// CatOS APIs
// Do NOT delete or edit
// functions from crt0.asm
extern int  write(char* buf, int len);
extern void putc(int ch);
extern void puts(char* str);
extern void set_color(int color);
extern void putc_color(int ch, int color);
extern void put_int(int n);
extern void put_uint(unsigned int n);
extern void put_hex(unsigned int n, int upper);
extern void put_bin(unsigned int n);
extern int  getc(void);
extern int  getch(void);
extern int  kbhit(void);
extern int  get_key_flags(void);
extern void clrscr(void);
extern void gotoxy(int x, int y);
extern int  get_xy(void);
extern int  get_cols(void);
extern int  get_rows(void);
extern int  get_vmode(void);
extern void scroll(int lines);
extern void set_io_size(int size);
extern int  file_read(char* name, char* buf, int bufsize);
extern int  file_write(char* name, char* buf, int size);
extern int  file_delete(char* name);
extern int  file_exists(char* name);
extern int  file_size(char* name);
extern int  file_list(char* buf, int bufsize);
extern int  get_ticks(void);
extern int  get_pid(void);
extern int  get_name(char* buf);
extern int  rand(void);
extern void srand(int seed);
extern void exit(int code);
extern void msleep(int ms);
extern void reboot(void);
extern int file_append(char* name, char* buf, int size);
extern int file_stat(char* name, int* info);
extern int  int_to_str(int value, char* buf, int radix);
extern int  atoi(char* str);
extern int  spawn(char* name);  /* 启动另一程序 (总是非阻塞, 返回槽位号>=0 或 错误码<0) */

/* === 用户控制台 API (NR 40-41) === */
extern void uc_putc(int ch, int attr);  /* 向控制台窗口输出字符 (带属性) */
extern void uc_clear(void);             /* 清空控制台窗口 */

/* === 用户窗口 API (NR 42-52) === */
extern int  win_create(int x, int y, int w, int h, char *title);  /* 创建窗口, 返回 win_id */
extern void win_close(int win_id);                                  /* 关闭窗口 */
extern void win_draw_text(int win_id, int x, int y, char *str, int fg, int bg);  /* 绘制文本 */
extern void win_draw_rect(int win_id, int x, int y, int w, int h, int color);    /* 绘制实心矩形 */
extern int  win_get_event(int win_id, int *type, int *x, int *y);  /* 阻塞获取事件 */
extern void win_clear(int win_id, int color);                       /* 清空画布 */
extern void win_set_title(int win_id, char *title);                 /* 设置标题 */
extern void win_draw_line(int win_id, int x1, int y1, int x2, int y2, int color);  /* 画线 */
extern void win_set_pixel(int win_id, int x, int y, int color);     /* 设置像素 */
extern int  win_peek_event(int win_id, int *type, int *x, int *y);  /* 非阻塞获取事件 */
extern int  win_get_size(int win_id, int *w, int *h);               /* 获取客户区尺寸 */

/* === 窗口事件类型常量 === */
#define EV_NONE    0   /* 无事件 */
#define EV_LDOWN   1   /* 左键按下 (x,y=鼠标坐标) */
#define EV_LUP     2   /* 左键释放 (x,y=鼠标坐标) */
#define EV_MOUSE   3   /* 鼠标移动 (x,y=鼠标坐标) */
#define EV_KEY     4   /* 键盘按键 (x=按键值, y=0) */
#define EV_CLOSE   5   /* 关闭请求 (用户点击关闭按钮) */

/* === 颜色常量 (VGA 256 色调色板索引) === */
#define COL_BLACK    0x00
#define COL_BLUE     0x01
#define COL_GREEN    0x02
#define COL_CYAN     0x03
#define COL_RED      0x04
#define COL_MAGENTA  0x05
#define COL_BROWN    0x06
#define COL_LGRAY    0x07
#define COL_DGRAY    0x08
#define COL_LBLUE    0x09
#define COL_LGREEN   0x0A
#define COL_LCYAN    0x0B
#define COL_LRED     0x0C
#define COL_LMAGENTA 0x0D
#define COL_YELLOW   0x0E
#define COL_WHITE    0x0F

/* 字符串工具函数 (crt0.asm 提供, 无需系统调用) */
extern int  strlen(char* str);
extern int  strcmp(char* a, char* b);
extern void strcpy(char* dst, char* src);
extern void memcpy(char* dst, char* src, int n);
extern void memset(char* dst, int val, int n);

int main(void)
{
	// put your codes here.
	
	int wid = win_create(20, 20, 500, 700, "Window");
	win_draw_text(wid, 50, 50, "Hello, CatOS", 0x00, 0x0F);
	
	while (true){
		msleep(2000);
	}
	
	exit(42);
	return 0;
}
