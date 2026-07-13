# CatOS 用户程序开发指南

本指南面向希望为 CatOS 编写独立软件的开发者。CatOS 支持通过 CE（CatOS Executable）格式的可执行文件加载并运行用户程序，开发者无需重新编译内核即可分发软件。

---

## 目录

1. [快速开始](#1-快速开始)
2. [工具链要求](#2-工具链要求)
3. [CE 可执行文件格式](#3-ce-可执行文件格式)
4. [内存布局与段隔离](#4-内存布局与段隔离)
5. [系统调用 API](#5-系统调用-api)
6. [编写用户程序](#6-编写用户程序)
7. [构建与打包](#7-构建与打包)
8. [在 CatOS 中运行](#8-在-catos-中运行)
9. [限制与注意事项](#9-限制与注意事项)
10. [示例程序详解](#10-示例程序详解)
11. [常见问题](#11-常见问题)
12. [未来路线图](#12-未来路线图)

---

## 1. 快速开始

```bash
cd sdk
make           # 编译生成 hello.ce
```

将 `hello.ce` 放入 CatOS 的 FAT 文件系统镜像后，在 CatOS shell 中执行：

```
CatOS> run hello.ce
```

预期输出：

```
Running: hello.ce
Hello, CatOS!
This is a user program running in ring 3.
System call test: ABC
Int:  -42 0 2147483647
UInt: 4000000000
Hex:  0xDEADBEEF / 0xdeadbeef
Bin:  10110100
Delay 500ms...
Ticks elapsed: 50
Program exited (code 42)
```

---

## 2. 工具链要求

| 工具 | 用途 | 最低版本 | 备注 |
|------|------|----------|------|
| GCC | 编译 C 源码 | 任意支持 `-m32` 的版本 | 推荐 `i686-w64-mingw32-gcc` |
| NASM | 汇编 crt0.asm | 2.13+ | 使用 `-f win32` 格式 |
| objcopy | 提取 flat binary | 随 binutils | 由 `mkcate.py` 自动调用 |
| Python | 运行 mkcate.py | 3.6+ | 用于生成 CE 头 |

### 安装提示

**Windows (MinGW)**:
```bash
# 确认 gcc 支持 32 位编译
gcc -m32 -c test.c -o test.o
```

**Linux (交叉编译)**:
```bash
sudo apt install gcc-mingw-w64-i686 nasm python3 binutils-mingw-w64-i686
```

Makefile 会自动检测 `i686-w64-mingw32-gcc`，若不存在则回退到系统 `gcc`。

---

## 3. CE 可执行文件格式

CE（CatOS Executable）是 CatOS 自定义的简单可执行文件格式，专为裸机环境设计，避免解析复杂 ELF/PE 的开销。

### 文件结构

```
┌──────────────────────────┐  偏移 0
│       CE Header        │  64 字节
├──────────────────────────┤  偏移 64
│   text + data 原始字节    │  (text_size + data_size) 字节
└──────────────────────────┘
```

**注意**：BSS 段不占文件空间，加载时由内核清零。

### 头部字段（64 字节，小端序）

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | magic | 固定 `0x00014543`（小端 `'CE\x01\x00'`） |
| 4 | 2 | version | 格式版本，当前为 `1` |
| 6 | 2 | flags | 标志位，bit0=32位 |
| 8 | 4 | entry | 入口偏移（相对段基址，即 `_start` 的地址） |
| 12 | 4 | text_size | 代码段字节数（含 .rodata/.rdata） |
| 16 | 4 | data_size | 数据段字节数 |
| 20 | 4 | bss_size | BSS 段字节数（加载时清零） |
| 24 | 4 | stack_size | 请求的栈大小（当前未使用） |
| 28 | 4 | total_size | text+data+bss 总大小（用于校验） |
| 32 | 28 | name | 程序名称（ASCII，以 `\0` 结尾，最多27字符） |
| 60 | 4 | header_crc | 头部 CRC32（预留，当前为 0） |

### 校验规则

内核加载时会进行以下校验，任一失败则拒绝执行：

1. 文件前 4 字节必须等于 `0x00014543`（CE v1 魔数）
2. `version` 必须等于 `1`
3. 若 `total_size != 0`，必须等于 `text_size + data_size + bss_size`
4. `text_size + data_size + bss_size` 不得超过 124KB（0x1F000）
5. 文件总长度至少 64 字节（头部大小）
6. 文件实际内容至少 `64 + text_size + data_size` 字节

---

## 4. 内存布局与段隔离

### 用户程序地址空间

CatOS 使用 x86 LDT（局部描述符表）实现段级内存隔离，每个用户程序拥有独立的 128KB 地址空间：

```
线性地址              段内偏移         内容
0x100000  ┄┄┄┄┄┄┄┄┄   0x00000   ┄┄ 用户程序加载基址 (USER_PROC_BASE)
         │                        │
         │  .text + .rodata       │  ← 从文件加载
         │                        │
         ├────────────────────────┤
         │  .data                 │  ← 从文件加载
         ├────────────────────────┤
         │  .bss                  │  ← 加载时清零
         ├────────────────────────┤
         │  (空闲)                │
         ├────────────────────────┤   0x10000   ← esp 初始值
         │  stack ↓               │
0x120000  ┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄   ┄┄ 128KB 边界 (USER_PROC_SIZE)
```

### 段描述符

| 段 | 选择子 | base | limit | DPL | 类型 |
|----|--------|------|-------|-----|------|
| CS (LDT[0]) | 0x07 | 0x100000 | 128KB | 3 | 代码段（可执行、可读） |
| DS/SS (LDT[1]) | 0x0F | 0x100000 | 128KB | 3 | 数据段（可读写） |
| GS | 0x00 | — | — | — | 空选择子（禁止访问显存） |

### 链接地址

用户程序链接地址为 **0**（相对段基址）。`user.ld` 中 `. = 0`，所有符号地址都是段内偏移。内核加载时设置 LDT base=0x100000，CPU 自动将段内偏移映射到线性地址。

### 特权级

用户程序运行在 **Ring 3**（最低特权级）：

- 禁止执行特权指令：`cli`、`sti`、`hlt`、`in`、`out`（无 IOPL）
- 禁止直接访问硬件（显存、端口、硬盘等）
- 所有 I/O 必须通过系统调用完成
- 违规操作触发 #GP（General Protection Fault），内核会终止程序并恢复 shell

---

## 5. 系统调用 API

用户程序通过 `int 0x90` 软中断发起系统调用。参数通过寄存器传递：

- `eax` = 系统调用号
- `ebx` = 第一个参数
- `ecx` = 第二个参数
- 返回值在 `eax`

### 可用系统调用（按功能分组）

共 55 个系统调用（NR 0~54）+ 5 个 crt0 内置字符串工具函数，下面按功能类别分组列出。

#### 控制台输出

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 1 | `write` | `int write(char* buf, int len)` | 写入缓冲区到控制台，返回写入字节数 |
| 2 | `putc` | `void putc(int ch)` | 输出单个字符（按 `set_color` 设定的颜色） |
| 3 | `puts` | `void puts(char* str)` | 输出以 `\0` 结尾的字符串 |
| 12 | `set_color` | `void set_color(int color)` | 设置后续 `putc`/`puts`/`write` 的默认属性 |
| 13 | `putc_color` | `void putc_color(int ch, int color)` | 输出一个带指定颜色的字符（不改默认） |
| 18 | `put_int` | `void put_int(int n)` | 输出有符号十进制整数（如 -42、0、2147483647） |
| 19 | `put_uint` | `void put_uint(unsigned int n)` | 输出无符号十进制整数（如 4000000000） |
| 20 | `put_hex` | `void put_hex(unsigned int n, int upper)` | 输出十六进制，upper=1 大写 A-F，0 小写 a-f |
| 21 | `put_bin` | `void put_bin(unsigned int n)` | 输出二进制（不含前导零，0 输出 "0"） |

#### 键盘输入

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 4 | `getc` | `int getc(void)` | 非阻塞读键盘，无按键返回 -1（只返回低 8 位 ASCII） |
| 23 | `getch` | `int getch(void)` | 阻塞读取一个键，返回 **16 位键值**（含扩展键标志，见下文） |
| 24 | `kbhit` | `int kbhit(void)` | 检测键盘是否有按键（不消费），返回 1=有 / 0=无 |
| 34 | `get_key_flags` | `int get_key_flags(void)` | 获取修饰键状态位掩码（Shift/Ctrl/Alt/Lock） |

#### 屏幕控制

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 6 | `clrscr` | `void clrscr(void)` | 清屏并将光标复位到左上角 |
| 7 | `gotoxy` | `void gotoxy(int x, int y)` | 移动光标到字符坐标 (x, y) |
| 8 | `get_xy` | `int get_xy(void)` | 返回 `(x & 0xFFFF) \| (y << 16)`，当前光标坐标 |
| 9 | `get_cols` | `int get_cols(void)` | 屏幕字符列数（文本=80，图形=40） |
| 10 | `get_rows` | `int get_rows(void)` | 屏幕字符行数（通常 25） |
| 11 | `get_vmode` | `int get_vmode(void)` | 当前视频模式（0=文本，1=图形 VGA13） |
| 25 | `scroll` | `void scroll(int lines)` | 屏幕向上滚动 N 行 |

#### 文件 IO

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 26 | `set_io_size` | `void set_io_size(int size)` | 设置后续 `file_read`/`file_write`/`file_list`/`file_append`/`int_to_str` 的第三参数 |
| 27 | `file_read` | `int file_read(char* name, char* buf, int bufsize)` | 读取文件内容到 buf，返回读取字节数 / -1 失败 |
| 28 | `file_write` | `int file_write(char* name, char* buf, int size)` | 写入文件（不存在则创建，已存在则覆盖），返回写入字节数 / -1 |
| 29 | `file_delete` | `int file_delete(char* name)` | 删除文件，返回 0 成功 / -1 失败 |
| 30 | `file_exists` | `int file_exists(char* name)` | 检查文件是否存在，返回 1=存在 / 0=不存在 |
| 31 | `file_size` | `int file_size(char* name)` | 获取文件大小（字节数），返回 -1 表示文件不存在 |
| 32 | `file_list` | `int file_list(char* buf, int bufsize)` | 列出根目录所有文件名（`\n` 分隔），返回文件数 |
| 35 | `file_append` | `int file_append(char* name, char* buf, int size)` | 追加数据到文件末尾（不覆盖原内容），返回 0 成功 / -1 失败 |
| 38 | `file_stat` | `int file_stat(char* name, int* info)` | 获取文件详细信息，info[0]=大小, info[1]=属性；返回 0 成功 / -1 失败 |

> **文件名格式**：FAT 8.3 短文件名（如 `"hello.ce"`、`"data.txt"`）。文件名不区分大小写，内核内部转大写匹配。
>
> **`file_append`**：向文件末尾追加数据，不覆盖原有内容。文件不存在时自动创建。适合日志写入等场景。
>
> **`file_stat`**：info 为 `int[2]` 数组：`info[0]`=文件大小（字节），`info[1]`=FAT 属性字节（0x20=普通归档文件）。

#### 系统信息

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 0 | `get_ticks` | `int get_ticks(void)` | 获取系统时钟滴答数（100Hz，1 tick = 10ms） |
| 14 | `get_pid` | `int get_pid(void)` | 获取当前进程的 PID |
| 15 | `get_name` | `int get_name(char* buf)` | 把进程名复制到 buf，返回长度（不含 `\0`） |
| 16 | `rand` | `int rand(void)` | 伪随机数，返回 [0, 32767] |
| 17 | `srand` | `void srand(int seed)` | 设置随机种子 |

#### 系统控制

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 5 | `exit` | `void exit(int code)` | 终止程序（不返回），code 为退出码 |
| 22 | `msleep` | `void msleep(int ms)` | 毫秒级延时（基于 100Hz 时钟，精度 10ms） |
| 33 | `reboot` | `void reboot(void)` | 重启系统（通过 8042 键盘控制器复位，不返回） |

#### 进程管理（Phase 3 新增）

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 39 | `spawn` | `int spawn(char* name)` | 启动另一个 CE 程序（总是非阻塞，返回槽位号） |

> **`spawn`** 参数说明：
> - `name`：要启动的 CE 可执行文件名（如 `"hello.ce"`）
> - **总是非阻塞**：启动后立即返回，返回值 = 槽位号（`>=0` 成功，`<0` 失败）
> - **安全策略**：阻塞/非阻塞的选择不对应用开放（避免恶意程序用阻塞模式卡住系统），改由 shell `run` 命令的 `&` 参数控制
> - 失败返回值：`-1`=文件系统错误/文件不存在，`-2`=格式错误，`-3`=过大，`-4`=槽位已满
>
> **使用示例**：
> ```c
> /* 应用程序启动另一个程序（非阻塞，两者并发运行） */
> int slot = spawn("worker.ce");
> if (slot < 0) { puts("spawn failed"); }
> ```
>
> **阻塞/非阻塞由 shell `run` 命令控制**（操作员决定）：
> ```
> CatOS> run hello.ce       # 前台阻塞：等待程序退出后返回 shell
> CatOS> run hello.ce &     # 后台非阻塞：立即返回 shell，程序并发运行
> ```

#### 用户控制台（NR 40-41）

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 40 | `uc_putc` | `void uc_putc(int ch, int attr)` | 向控制台窗口输出字符（带属性） |
| 41 | `uc_clear` | `void uc_clear(void)` | 清空控制台窗口 |

#### 窗口 API（NR 42-54）

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 42 | `win_create` | `int win_create(int x, int y, int w, int h, char *title)` | 创建窗口，返回 win_id |
| 43 | `win_close` | `void win_close(int win_id)` | 关闭窗口 |
| 44 | `win_draw_text` | `void win_draw_text(int win_id, int x, int y, char *str, int fg, int bg)` | 绘制文本 |
| 45 | `win_draw_rect` | `void win_draw_rect(int win_id, int x, int y, int w, int h, int color)` | 绘制实心矩形 |
| 46 | `win_get_event` | `int win_get_event(int win_id, int *type, int *x, int *y)` | 阻塞获取事件 |
| 47 | `win_clear` | `void win_clear(int win_id, int color)` | 清空画布 |
| 48 | `win_set_title` | `void win_set_title(int win_id, char *title)` | 设置标题 |
| 49 | `win_draw_line` | `void win_draw_line(int win_id, int x1, int y1, int x2, int y2, int color)` | 画线 |
| 50 | `win_set_pixel` | `void win_set_pixel(int win_id, int x, int y, int color)` | 设置像素 |
| 51 | `win_peek_event` | `int win_peek_event(int win_id, int *type, int *x, int *y)` | 非阻塞获取事件 |
| 52 | `win_get_size` | `int win_get_size(int win_id, int *w, int *h)` | 获取客户区尺寸 |
| 53 | `win_set_closable` | `void win_set_closable(int win_id, int closable)` | 设置窗口是否允许叉号关闭 |
| 54 | `win_get_closable` | `int win_get_closable(int win_id)` | 查询窗口是否允许叉号关闭 |

#### 类型转换

| NR | 函数 | 原型 | 说明 |
|----|------|------|------|
| 36 | `int_to_str` | `int int_to_str(int value, char* buf, int radix)` | 整数转字符串，返回字符串长度（不含 `\0`） |
| 37 | `atoi` | `int atoi(char* str)` | 字符串转整数（十进制），返回解析结果 |

> **`int_to_str`** 参数说明：
> - `value`：要转换的整数（radix=10 时支持负数，其他进制按无符号处理）
> - `buf`：输出缓冲区，需至少 33 字节（二进制 32 位 + `\0`）
> - `radix`：进制（2/8/10/16 等，0 默认为 10）；hex 用小写 a-f
> - 返回值：写入的字符数（不含 `\0`）
>
> **`atoi`** 行为：跳过前导空白，处理可选的 `+`/`-` 符号，解析到第一个非数字字符停止。无有效数字时返回 0。

### 窗口 API（NR 40-54）

#### 概述

窗口 API 允许用户程序创建自己的 GUI 窗口，进行绘图和事件处理。窗口仅在图形桌面模式（`get_vmode() != 0`）下可用。每个用户进程最多创建 4 个窗口。

#### 用户控制台 API

`uc_putc` 和 `uc_clear` 操作的是"当前进程控制台窗口"——即程序在图形桌面下启动时自动分配的控制台窗口。

- `uc_putc(ch, attr)`：向控制台窗口输出字符。`attr` 低 4 位=前景色，高 4 位=背景色（如 `0x0F`=黑底亮白）
- `uc_clear()`：清空控制台并重置光标到左上角

#### 窗口创建与销毁

- `win_create(x, y, w, h, title)`：创建窗口
  - `x, y`：窗口左上角屏幕坐标
  - `w, h`：客户区尺寸（不含标题栏边框）
  - `title`：标题字符串
  - 返回 `win_id`（`>=0`）或 `-1`（失败）
  - 客户区最大 200x140 像素
- `win_close(win_id)`：关闭并释放窗口资源

#### 绘图 API

所有绘图坐标都是相对于客户区左上角 `(0,0)`，会自动裁剪到画布范围。

- `win_draw_text(win_id, x, y, str, fg, bg)`：绘制文本，8x16 像素字体，`fg`/`bg` 为颜色索引
- `win_draw_rect(win_id, x, y, w, h, color)`：绘制实心矩形
- `win_draw_line(win_id, x1, y1, x2, y2, color)`：Bresenham 画线
- `win_set_pixel(win_id, x, y, color)`：设置单个像素
- `win_clear(win_id, color)`：清空整个画布为指定颜色

#### 事件处理

- `win_get_event(win_id, &type, &x, &y)`：**阻塞**等待事件。`type`=事件类型，`x`/`y`=坐标或按键值
- `win_peek_event(win_id, &type, &x, &y)`：**非阻塞**，无事件时返回 0

事件类型常量：

| 常量 | 值 | 说明 |
|------|----|------|
| `EV_NONE` | 0 | 无事件 |
| `EV_LDOWN` | 1 | 左键按下 |
| `EV_LUP` | 2 | 左键释放 |
| `EV_MOUSE` | 3 | 鼠标移动 |
| `EV_KEY` | 4 | 键盘按键 |
| `EV_CLOSE` | 5 | 关闭请求 |

- **鼠标事件**：`x, y` = 客户区相对坐标
- **键盘事件**：`x` = 按键值（与 `getch` 相同的 16 位键值），`y` = 0
- **关闭事件**：用户点击窗口关闭按钮时触发，程序应调用 `win_close()` 响应

#### 窗口信息

- `win_set_title(win_id, title)`：动态修改窗口标题
- `win_get_size(win_id, &w, &h)`：获取客户区宽高

#### win_set_closable (NR 53)

```c
void win_set_closable(int win_id, int closable);
```

- 功能: 设置用户窗口是否允许通过点击标题栏叉号(×)关闭
- 参数:
  - `win_id`: 窗口 ID (`win_create` 返回值)
  - `closable`: 0=禁止叉号关闭, 1=允许叉号关闭 (默认)
- 返回值: 无
- 说明:
  - 默认情况下，所有窗口都允许通过叉号关闭（会推送 `EV_CLOSE` 事件）
  - 设置 `closable=0` 后，点击叉号不会产生任何效果，窗口无法通过叉号关闭
  - 应用仍可通过 `win_close()` 主动关闭窗口
  - 典型用途: 强制用户完成操作后才能关闭窗口（如表单未提交时禁止关闭）

#### win_get_closable (NR 54)

```c
int win_get_closable(int win_id);
```

- 功能: 查询用户窗口是否允许通过点击叉号关闭
- 参数:
  - `win_id`: 窗口 ID
- 返回值: 1=允许叉号关闭, 0=禁止叉号关闭 (或窗口不存在)

#### 窗口关闭行为

- 应用主窗口（用户控制台窗口）的叉号会直接终止进程并关闭所有子窗口
- 子窗口（`win_create` 创建的）的叉号默认推送 `EV_CLOSE` 事件，应用可通过 `win_set_closable(win_id, 0)` 禁用
- 应用调用 `exit(code)` 退出时，会自动关闭所有子窗口并终止进程

#### 颜色常量

VGA 256 色调色板，常用颜色：

| 值 | 颜色 | 值 | 颜色 |
|----|------|----|------|
| 0x00 | 黑 | 0x08 | 暗灰 |
| 0x01 | 蓝 | 0x09 | 亮蓝 |
| 0x02 | 绿 | 0x0A | 亮绿 |
| 0x03 | 青 | 0x0B | 亮青 |
| 0x04 | 红 | 0x0C | 亮红 |
| 0x05 | 品红 | 0x0D | 亮品红 |
| 0x06 | 棕 | 0x0E | 黄 |
| 0x07 | 浅灰 | 0x0F | 白 |

#### 示例程序

```c
int main(void)
{
    int win = win_create(30, 30, 160, 100, "My App");
    if (win < 0) exit(1);

    /* 背景填充 */
    win_clear(win, 0x01);  /* 蓝色背景 */

    /* 绘制内容 */
    win_draw_rect(win, 10, 10, 50, 30, 0x0E);  /* 黄色矩形 */
    win_draw_text(win, 10, 50, "Hello!", 0x0F, 0x01);  /* 白字蓝底 */
    win_draw_line(win, 0, 0, 159, 99, 0x0A);  /* 绿色对角线 */

    /* 事件循环 */
    int type, x, y;
    while (1) {
        type = win_get_event(win, &type, &x, &y);
        if (type == EV_CLOSE) break;  /* 关闭按钮 */
        if (type == EV_KEY) {
            if (x == 0x101) break;  /* ESC */
        }
        if (type == EV_LDOWN) {
            /* 在点击位置画点 */
            win_set_pixel(win, x, y, 0x0E);
        }
    }

    win_close(win);
    exit(0);
}
```

#### 字符串工具（crt0 内置，无需系统调用）

以下函数由 crt0.asm 提供，纯用户态实现，不经过系统调用：

| 函数 | 原型 | 说明 |
|------|------|------|
| `strlen` | `int strlen(char* str)` | 返回字符串长度（不含 `\0`） |
| `strcmp` | `int strcmp(char* a, char* b)` | 比较两个字符串，返回 0=相等 / <0 / >0 |
| `strcpy` | `void strcpy(char* dst, char* src)` | 复制字符串（含 `\0`） |
| `memcpy` | `void memcpy(char* dst, char* src, int n)` | 复制 n 字节内存 |
| `memset` | `void memset(char* dst, int val, int n)` | 用 val 填充 n 字节 |

> **属性字节格式**（`set_color` / `putc_color` 的 `color`）：高 4 位背景，低 4 位前景。
> 例：`0x0F`=黑底亮白，`0x1E`=蓝底黄字，`0x4E`=红底黄字，`0x0A`=黑底亮绿。
> 可用 `MAKE_COLOR(bg, fg)` 宏（见 `const.h`）组合。

### 扩展键与 `getch` 返回值

`getch` 返回 **16 位键值**，不同类型的按键返回值不同：

- **普通可见字符**（0x20~0x7E）：返回 ASCII 码本身，如 `'A'`=0x41、`'1'`=0x31
- **扩展键**（Enter/ESC/Tab/Backspace/F1~F12/方向键等）：返回 `FLAG_EXT(0x100) + code`
- **修饰键**（Shift/Ctrl/Alt/CapsLock 等）：在内部自动跳过，不会返回

判断扩展键：`(key & 0x100)` 非 0 即为扩展键。常见扩展键值（定义在 `keyboard.h`）：

| 键 | 返回值 | 键 | 返回值 |
|----|--------|----|--------|
| ESC | 0x101 | F1~F12 | 0x111~0x11C |
| Tab | 0x102 | Home / End | 0x121 / 0x122 |
| Enter | 0x103 | PageUp / PageDown | 0x123 / 0x124 |
| Backspace | 0x104 | Up / Down | 0x125 / 0x126 |
| Insert | 0x11F | Left / Right | 0x127 / 0x128 |
| Delete | 0x120 | Pause/Break | 0x11E |

> **注意**：`getc`（NR 4）只返回低 8 位（`key & 0xFF`），无法获取扩展键码。需要处理扩展键时请使用 `getch`。
> 图形模式下，`putc` 对 <32 或 >127 的字符输出 `?`，因此扩展键的低位字节不会被当作可见字符打印。

### `get_key_flags` 修饰键状态

返回 16 位状态掩码，可同时检测多个修饰键：

| 位 | 掩码 | 含义 |
|----|------|------|
| 0 | 0x001 | 左 Shift |
| 1 | 0x002 | 右 Shift |
| 2 | 0x004 | 左 Ctrl |
| 3 | 0x008 | 右 Ctrl |
| 4 | 0x010 | 左 Alt |
| 5 | 0x020 | 右 Alt |
| 6 | 0x040 | Caps Lock |
| 7 | 0x080 | Num Lock |
| 8 | 0x100 | Scroll Lock |

### 使用方式

SDK 在 `crt0.asm` 中提供了汇编包装函数，C 代码中只需 `extern` 声明即可调用：

```c
/* === 控制台输出 === */
extern int  write(char* buf, int len);
extern void putc(int ch);
extern void puts(char* str);
extern void set_color(int color);
extern void putc_color(int ch, int color);
extern void put_int(int n);
extern void put_uint(unsigned int n);
extern void put_hex(unsigned int n, int upper);
extern void put_bin(unsigned int n);

/* === 键盘输入 === */
extern int  getc(void);
extern int  getch(void);
extern int  kbhit(void);
extern int  get_key_flags(void);

/* === 屏幕控制 === */
extern void clrscr(void);
extern void gotoxy(int x, int y);
extern int  get_xy(void);
extern int  get_cols(void);
extern int  get_rows(void);
extern int  get_vmode(void);
extern void scroll(int lines);

/* === 文件 IO === */
extern void set_io_size(int size);
extern int  file_read(char* name, char* buf, int bufsize);
extern int  file_write(char* name, char* buf, int size);
extern int  file_delete(char* name);
extern int  file_exists(char* name);
extern int  file_size(char* name);
extern int  file_list(char* buf, int bufsize);
extern int  file_append(char* name, char* buf, int size);
extern int  file_stat(char* name, int* info);

/* === 系统信息 === */
extern int  get_ticks(void);
extern int  get_pid(void);
extern int  get_name(char* buf);
extern int  rand(void);
extern void srand(int seed);

/* === 系统控制 === */
extern void exit(int code);
extern void msleep(int ms);
extern void reboot(void);

/* === 进程管理 (Phase 3) === */
extern int  spawn(char* name);  /* 总是非阻塞, 返回槽位号 */

/* === 用户控制台 === */
extern void uc_putc(int ch, int attr);
extern void uc_clear(void);

/* === 窗口 API === */
extern int  win_create(int x, int y, int w, int h, char *title);
extern void win_close(int win_id);
extern void win_draw_text(int win_id, int x, int y, char *str, int fg, int bg);
extern void win_draw_rect(int win_id, int x, int y, int w, int h, int color);
extern int  win_get_event(int win_id, int *type, int *x, int *y);
extern void win_clear(int win_id, int color);
extern void win_set_title(int win_id, char *title);
extern void win_draw_line(int win_id, int x1, int y1, int x2, int y2, int color);
extern void win_set_pixel(int win_id, int x, int y, int color);
extern int  win_peek_event(int win_id, int *type, int *x, int *y);
extern int  win_get_size(int win_id, int *w, int *h);
extern void win_set_closable(int win_id, int closable);
extern int  win_get_closable(int win_id);

/* === 窗口事件常量 === */
#define EV_NONE    0
#define EV_LDOWN   1
#define EV_LUP     2
#define EV_MOUSE   3
#define EV_KEY     4
#define EV_CLOSE   5

/* === 类型转换 === */
extern int  int_to_str(int value, char* buf, int radix);
extern int  atoi(char* str);

/* === 字符串工具 (crt0 内置) === */
extern int  strlen(char* str);
extern int  strcmp(char* a, char* b);
extern void strcpy(char* dst, char* src);
extern void memcpy(char* dst, char* src, int n);
extern void memset(char* dst, int val, int n);
```

### 字符串指针注意事项

`puts` 和 `write` 的参数是**段内偏移**（相对 LDT base）。由于链接地址为 0，C 代码中的字符串指针就是段内偏移，无需手动转换。内核会自动将其转换为线性地址（`USER_PROC_BASE + 偏移`）来读取。

### 图形模式支持

系统调用输出自动适配文本模式和图形模式（VGA Mode 13h）：
- 文本模式：写入 VGA 文本显存（0xB8000）
- 图形模式：使用位图字体绘制，支持滚屏和光标

---

## 6. 编写用户程序

### 基本结构

一个 CatOS 用户程序需要：

1. **`extern` 声明** SDK 提供的函数
2. **`main` 函数** 作为程序入口（由 crt0 调用）
3. **调用 `exit`** 终止程序（虽然 crt0 会在 main 返回后自动调用 exit，但建议显式调用）

### 模板

```c
/* 声明系统调用 */
extern void puts(char* str);
extern void putc(int ch);
extern void exit(int code);
extern int  get_ticks(void);
extern int  getc(void);

/* 全局变量（放入 .data 段） */
int counter = 0;

/* BSS 变量（加载时清零） */
char buffer[256];

int main(void)
{
    static char msg[] = "My first CatOS program!\n";
    puts(msg);

    /* 显示数字 0-9 */
    for (int i = 0; i < 10; i++) {
        putc('0' + i);
    }
    putc('\n');

    /* 获取系统滴答数 */
    int t = get_ticks();
    /* 注意：没有 printf，需要自己实现数字转字符串 */

    exit(0);
    return 0; /* 不会到达 */
}
```

### 字符串使用规则

建议使用全局数组定义字符串，确保稳定可靠：

```c
/* 推荐：使用全局数组 */
static char msg[] = "hello world\n";
void good_example(void) {
    puts(msg);
}
```

### 实现数字输出

CatOS SDK 内置了 `put_int` / `put_uint` / `put_hex` / `put_bin` 系统调用，可直接使用：

```c
extern void put_int(int n);
extern void put_hex(unsigned int n, int upper);

put_int(42);          /* 输出: 42 */
put_int(-7);          /* 输出: -7 */
put_hex(0xBEEF, 1);   /* 输出: BEEF */
```

如需更复杂的格式化输出（类似 `printf`），可基于上述函数自行封装。

### 键盘输入

- `getc`：非阻塞，无按键时返回 -1，**只返回低 8 位 ASCII**
- `getch`：阻塞，等待按键后返回 **16 位键值**（含扩展键标志，见下文）
- `kbhit`：检测是否有按键（不消费数据），返回 1=有 / 0=无
- `get_key_flags`：获取修饰键（Shift/Ctrl/Alt/Lock）状态

```c
extern int  getc(void);         /* 非阻塞，无按键返回 -1 */
extern int  getch(void);        /* 阻塞，返回 16 位键值 */
extern int  kbhit(void);        /* 返回 1=有按键, 0=无 (不消费) */
extern int  get_key_flags(void);/* 修饰键状态掩码 */
extern void putc(int ch);
extern void exit(int code);

/* 扩展键值常量 (见 keyboard.h, 此处直接用数值避免依赖头文件) */
#define KEY_ESC      0x101
#define KEY_ENTER    0x103
#define KEY_BACKSPACE 0x104
#define KEY_UP       0x125
#define KEY_DOWN     0x126
#define KEY_LEFT     0x127
#define KEY_RIGHT    0x128
#define IS_EXT(k)    ((k) & 0x100)  /* 判断是否为扩展键 */

int main(void) {
    /* 方式 1: 非阻塞轮询 (只处理 ASCII) */
    while (1) {
        int ch = getc();
        if (ch >= 0) {
            if (ch == 27) { exit(0); }  /* ESC 的 ASCII 码 */
            putc(ch);
        }
    }
}

/* 方式 2: 阻塞等待 + 扩展键处理 (适合交互式程序/游戏) */
int interactive_loop(void) {
    while (1) {
        int key = getch();           /* 阻塞直到有按键 */

        if (IS_EXT(key)) {
            /* 扩展键: 方向键/功能键等 */
            switch (key) {
            case KEY_ESC:       exit(0);          /* ESC 退出 */
            case KEY_UP:        /* 处理上移 */     break;
            case KEY_DOWN:      /* 处理下移 */     break;
            case KEY_LEFT:      /* 处理左移 */     break;
            case KEY_RIGHT:     /* 处理右移 */     break;
            case KEY_ENTER:     /* 处理确认 */     break;
            case KEY_BACKSPACE: /* 处理退格 */     break;
            }
        } else {
            /* 普通可见字符 (0x20~0x7E) */
            if (key == 'q') break;
            putc(key);
        }
    }
}

/* 方式 3: 游戏循环 (kbhit 非阻塞 + getch 消费) */
int game_loop(void) {
    while (1) {
        if (kbhit()) {
            int key = getch();
            if (key == 'q') break;
        }
        /* 更新游戏状态... */
    }
}

/* 检测修饰键组合 (如 Ctrl+C) */
void check_modifiers(void) {
    int flags = get_key_flags();
    if (flags & 0x004) {  /* 左 Ctrl 按下 */
        /* ... */
    }
}
```

> **重要**：`getch` 返回的扩展键值高位带 `0x100` 标志，**不要**用 `& 0xFF` 截断，否则会丢失扩展键信息导致方向键等功能键无法识别。

### 扩展 API 示例

下面演示清屏、定位、彩色输出、随机数与进程信息等扩展系统调用：

```c
extern void clrscr(void);
extern void gotoxy(int x, int y);
extern void set_color(int color);
extern void putc_color(int ch, int color);
extern void puts(char* str);
extern void putc(int ch);
extern int  get_cols(void);
extern int  get_pid(void);
extern int  get_name(char* buf);
extern int  rand(void);
extern void srand(int seed);
extern int  get_ticks(void);
extern void exit(int code);

static void put_int(int n)
{
    char buf[16];
    int i = 0;
    if (n == 0) { putc('0'); return; }
    if (n < 0) { putc('-'); n = -n; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) putc(buf[--i]);
}

int main(void)
{
    char name[16];
    int cols;

    clrscr();
    cols = get_cols();

    /* 顶部红底黄字标题 */
    gotoxy(0, 0);
    set_color(0x4E);             /* 红底黄字 */
    puts("=== CatOS Extended API Demo ===");
    set_color(0x0F);             /* 恢复默认 */

    /* 第二行显示进程信息 */
    gotoxy(0, 2);
    puts("PID = "); put_int(get_pid());
    puts("  Name = ");
    get_name(name);
    puts(name);
    puts("  Cols = "); put_int(cols);

    /* 用 ticks 做种，输出 5 个随机数 */
    srand(get_ticks());
    gotoxy(0, 4);
    puts("Random: ");
    {
        int i;
        for (i = 0; i < 5; i++) {
            put_int(rand());
            putc(' ');
        }
    }

    /* 不同颜色单字 */
    gotoxy(0, 6);
    putc_color('C', 0x0C);  /* 亮红 */
    putc_color('a', 0x0A);  /* 亮绿 */
    putc_color('t', 0x0B);  /* 亮青 */
    putc_color('O', 0x0E);  /* 黄 */
    putc_color('S', 0x0D);  /* 亮洋红 */
    putc('\n');

    exit(0);
    return 0;
}
```

### 文件 IO

CatOS 提供 FAT12/16/32 文件系统访问接口。由于系统调用接口只有两个参数寄存器（ebx/ecx），三参数函数（`file_read`/`file_write`/`file_list`/`file_append`/`int_to_str`）会先调用 `set_io_size` 传递第三参数，再调用实际系统调用——这些已由 crt0 自动处理，C 代码中直接调用即可。

```c
extern void set_io_size(int size);
extern int  file_read(char* name, char* buf, int bufsize);
extern int  file_write(char* name, char* buf, int size);
extern int  file_append(char* name, char* buf, int size);
extern int  file_delete(char* name);
extern int  file_exists(char* name);
extern int  file_size(char* name);
extern int  file_list(char* buf, int bufsize);
extern int  file_stat(char* name, int* info);
extern void puts(char* str);
extern void putc(int ch);
extern void put_int(int n);
extern void exit(int code);

/* 缓冲区: 文件最大读取量受用户程序内存限制 (128KB 段空间) */
static char buf[4096];

int main(void)
{
    /* --- 写文件 --- */
    static char content[] = "Hello from CatOS file IO!\nLine 2\n";
    int written = file_write("test.txt", content, 25);
    if (written < 0) {
        puts("Write failed\n");
        exit(1);
    }
    puts("Wrote "); put_int(written); puts(" bytes\n");

    /* --- 追加数据到文件末尾 --- */
    static char more[] = "Line 3 (appended)\n";
    if (file_append("test.txt", more, 18) == 0) {
        puts("Appended successfully\n");
    }

    /* --- file_stat: 获取文件详细信息 --- */
    int info[2];
    if (file_stat("test.txt", info) == 0) {
        puts("Size=");     put_int(info[0]); putc('\n');
        puts("Attr=0x");   put_int(info[1]); putc('\n');
    }

    /* --- 读文件 --- */
    int n = file_read("test.txt", buf, sizeof(buf));
    if (n > 0) {
        puts("Read "); put_int(n); puts(" bytes:\n");
        buf[n] = '\0';
        puts(buf);
    }

    /* --- 列出根目录文件 --- */
    static char list_buf[2048];
    int count = file_list(list_buf, sizeof(list_buf));
    puts("Files ("); put_int(count); puts("):\n");
    puts(list_buf);

    /* --- 删除文件 --- */
    if (file_delete("test.txt") == 0) {
        puts("Deleted test.txt\n");
    }

    exit(0);
    return 0;
}
```

> **注意**：
> - 文件名为 FAT 8.3 短文件名格式（如 `"test.txt"`、`"hello.ce"`），最长 8 字符主名 + 3 字符扩展名。
> - `file_write` 会创建或覆盖文件；`file_append` 在文件末尾追加（不覆盖原内容），文件不存在时自动创建。
> - `file_read` 的 `bufsize` 决定最多读取字节数。
> - 读写缓冲区在用户程序段内，受 128KB 段大小限制，单次读写不宜过大。
> - `file_list` 返回的文件名以 `\n` 分隔，可直接 `puts` 输出。
> - `file_stat` 的 info 是 `int[2]`：`info[0]`=大小, `info[1]`=属性。

### 类型转换

`int_to_str` 将整数转为字符串，`atoi` 将字符串转为整数。这两个函数让用户程序无需手动实现数字↔字符串转换。

```c
extern int int_to_str(int value, char* buf, int radix);
extern int atoi(char* str);
extern void puts(char* str);
extern void putc(int ch);
extern void exit(int code);

static char numbuf[40];

int main(void)
{
    /* --- int_to_str: 整数转字符串 --- */
    int_to_str(42, numbuf, 10);          /* "42"          */
    puts("Dec: "); puts(numbuf); putc('\n');

    int_to_str(-123, numbuf, 10);        /* "-123"        */
    puts("Neg: "); puts(numbuf); putc('\n');

    int_to_str(255, numbuf, 16);         /* "ff"          */
    puts("Hex: "); puts(numbuf); putc('\n');

    int_to_str(10, numbuf, 2);           /* "1010"        */
    puts("Bin: "); puts(numbuf); putc('\n');

    int_to_str(511, numbuf, 8);          /* "777"         */
    puts("Oct: "); puts(numbuf); putc('\n');

    /* --- atoi: 字符串转整数 --- */
    int a = atoi("42");            /* 42  */
    int b = atoi("  -7");          /* -7  */
    int c = atoi("0x1F");          /* 0   (非十进制, 遇 'x' 停止) */
    int d = atoi("abc");           /* 0   (无有效数字) */

    /* 组合使用: int_to_str + puts 替代 put_int, 可控制输出到文件 */
    static char log[64];
    int_to_str(a + b, log, 10);
    /* file_append("log.txt", log, strlen(log)); */

    exit(0);
    return 0;
}
```

> **提示**：`int_to_str` 比 `put_int` 更灵活——`put_int` 只能输出到屏幕，而 `int_to_str` 把结果写入缓冲区，可用于写文件、字符串拼接等场景。

---

## 7. 构建与打包

### 构建流程

```
hello.c ──gcc -m32──→ hello.o ─┐
                                ├─ gcc -m32 -nostdlib -T user.ld ──→ hello.elf
crt0.asm ──nasm -f win32──→ crt0.o ─┘                                         │
                                                                              ▼
                                                           mkcate.py ──→ hello.ce
                                                              │
                                              ┌───────────────┴───────────────┐
                                              │  解析 ELF/PE 段大小和入口地址   │
                                              │  objcopy 提取 flat binary       │
                                              │  生成 64 字节 CE v1 头          │
                                              │  输出: header(64) + flat_binary │
                                              └───────────────────────────────────┘
```

### 使用 Makefile

```bash
cd sdk
make           # 构建 hello.ce
make clean     # 清理中间文件
```

### 构建自定义程序

如果要构建名为 `myapp.c` 的程序，修改 Makefile：

```makefile
TARGET := myapp.ce
ELF    := myapp.elf
APP_O  := myapp.o

$(APP_O): myapp.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(ELF): $(CRT0) $(APP_O) user.ld
	$(CC) $(LDFLAGS) -o $@ $(CRT0) $(APP_O)
```

### 手动构建（不使用 Makefile）

```bash
# 1. 编译 C 源码
gcc -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-pie \
    -fno-asynchronous-unwind-tables -c myapp.c -o myapp.o

# 2. 汇编 crt0
nasm -f win32 crt0.asm -o crt0.o

# 3. 链接
gcc -m32 -nostdlib -Wl,-T,user.ld crt0.o myapp.o -o myapp.elf

# 4. 打包 CE
python3 mkcate.py myapp.elf myapp.ce myapp
```

### mkcate.py 输出说明

```
$ python3 mkcate.py hello.elf hello.ce hello
  entry      = 0x00000001
  text_size  = 128 bytes
  data_size  = 32 bytes
  bss_size   = 16 bytes
  total_size = 176 bytes
  name       = hello
  Output: hello.ce (240 bytes)
  Format: CE v1 (64B header + 176B body)
```

- **entry**: 程序入口偏移（`_start` 符号的地址）
- **text_size**: `.text` + `.rodata`/`.rdata` 段大小
- **data_size**: `.data` 段大小
- **bss_size**: `.bss` 段大小（不在文件中，加载时清零）
- **total_size**: text+data+bss 总大小（校验用）

---

## 8. 在 CatOS 中运行

### 将程序放入文件系统

#### 方法 1：构建时集成（推荐）

修改构建脚本，将 `.ce` 文件写入 FAT 文件系统镜像。

#### 方法 2：使用 CatOS 内置命令

在 CatOS 中使用 `touch` 创建空文件，然后用 `edit` 编辑。但此方法不适合二进制文件。

### 运行命令

**文本模式 Shell**：

```
CatOS> run hello.ce
```

**图形桌面**：

1. 在 Shell 中输入 `cat` 进入图形桌面
2. 双击文件管理器中的 `.ce` 文件，或打开终端执行 `run hello.ce`
3. 系统会为程序分配独立的控制台窗口，程序输出显示在窗口中
4. 程序退出后窗口显示 "Press any key to continue..."，按任意键关闭

### 错误信息

| 错误信息 | 原因 |
|----------|------|
| `Usage: run <filename>` | 未提供文件名 |
| `File not found: xxx` | 文件不存在于 FAT 文件系统 |
| `Bad executable format (not CE)` | 文件头 magic 不匹配或文件过小 |
| `Program too large (max 124KB)` | 程序段总和超过 124KB |
| `A program is already running` | 已有用户程序在运行（单槽位限制） |
| `Corrupted executable` | 文件损坏（大小校验失败） |
| `Program crashed (exception #N)` | 程序触发异常（如 #GP=13, #PF=14） |

### 退出码

- 文本模式 Shell：`exit(N)` → 显示 `Program exited (code N)`
- 图形桌面（控制台窗口）：程序退出后窗口显示 `Press any key to continue...`，按任意键关闭窗口
- 异常崩溃 → 显示 `Program exited (code -N-1)`（N 为异常向量号）

> **注意**：图形桌面下，程序退出后控制台窗口不会立即关闭。内核会显示 "Press any key to continue..." 提示，等待用户按键后才关闭窗口并释放资源。这样用户可以查看程序的最终输出。
>
> **`exit(code)` 与子窗口**：当应用调用 `exit(code)` 时，系统会先关闭该应用创建的所有子窗口（UW 窗口），再关闭控制台窗口，最后终止进程。退出码会记录在进程的 `exit_code` 字段中。

---

## 9. 限制与注意事项

### 当前限制（MVP 阶段）

| 限制 | 说明 | 未来计划 |
|------|------|----------|
| 单程序运行 | 同一时刻只能运行一个用户程序 | Phase 3: 多进程并发 |
| 128KB 内存上限 | text+data+bss+stack ≤ 124KB | Phase 3: 动态内存分配 |
| 无进程创建 | 无法 fork/exec | Phase 5: fork/exec |
| 无浮点运算 | FPU 未启用 | 暂无计划 |
| 段隔离（非分页） | 段限之外的访问触发 #GP | Phase 6: 分页隔离 |
| 单次整文件读写 | 无 seek/流式读写，需整文件读入内存 | 未来支持 open/read/write/close |

> **已解决的限制**：
> - ~~非阻塞键盘输入~~ → 现已有阻塞 `getch`（返回 16 位键值含扩展键）+ `kbhit` + `get_key_flags`
> - ~~无文件系统调用~~ → 现已有 `file_read`/`file_write`/`file_delete`/`file_exists`/`file_size`/`file_list`

### 编程注意事项

1. **不要使用标准库**：`#include <stdio.h>` 不可用，使用 `extern` 声明 SDK 函数
2. **不要使用浮点**：`float`/`double` 需要 FPU，CatOS 未初始化
3. **不要访问硬件**：`in`/`out` 指令会触发 #GP
4. **栈大小**：栈从 0x10000 向下生长，可用约 64KB
5. **全局变量**：BSS 变量在加载时自动清零，无需手动初始化
6. **字符串**：使用全局数组定义字符串
7. **不返回 main**：虽然 crt0 会在 main 返回后调用 `exit(eax)`，但建议显式调用 `exit`
8. **图形模式**：系统调用输出自动适配图形模式，无需特殊处理
9. **扩展键处理**：`getch` 返回 16 位键值，用 `(key & 0x100)` 判断扩展键，**不要** `& 0xFF` 截断
10. **文件 IO 缓冲区**：`file_read`/`file_write` 的缓冲区在用户段内，受 128KB 段大小限制

---

## 10. 示例程序详解

### hello.c（Hello World + 高级 API 演示）

```c
/* CatOS Hello World 示例程序 — 演示整数输出、十六进制、延时等高级 API */

extern void puts(char* str);
extern void putc(int ch);
extern void put_int(int n);
extern void put_uint(unsigned int n);
extern void put_hex(unsigned int n, int upper);
extern void put_bin(unsigned int n);
extern void msleep(int ms);
extern int  get_ticks(void);
extern void exit(int code);

static char msg1[] = "Hello, CatOS!\n";
static char msg2[] = "This is a user program running in ring 3.\n";
static char msg3[] = "System call test: ";

int main(void)
{
    puts(msg1);
    puts(msg2);
    puts(msg3);

    putc('A');
    putc('B');
    putc('C');
    putc('\n');

    /* 整数输出演示 */
    puts("Int:  ");
    put_int(-42);
    putc(' ');
    put_int(0);
    putc(' ');
    put_int(2147483647);
    putc('\n');

    /* 无符号整数演示 */
    puts("UInt: ");
    put_uint(4000000000u);
    putc('\n');

    /* 十六进制演示 (大写和小写) */
    puts("Hex:  0x");
    put_hex(0xDEADBEEFu, 1);
    puts(" / 0x");
    put_hex(0xdeadbeefu, 0);
    putc('\n');

    /* 二进制演示 */
    puts("Bin:  ");
    put_bin(0b10110100);
    putc('\n');

    /* 延时 + ticks 演示 */
    puts("Delay 500ms...\n");
    int t1 = get_ticks();
    msleep(500);
    int t2 = get_ticks();
    puts("Ticks elapsed: ");
    put_int(t2 - t1);
    putc('\n');

    exit(42);
    return 0;
}
```

**解析**：
- `put_int(-42)` 直接输出有符号整数，无需自行实现数字转字符串
- `put_hex(0xDEADBEEF, 1)` 输出大写十六进制，第二参数为 0 时输出小写
- `put_bin(n)` 输出二进制表示，适合底层调试
- `msleep(500)` 延时 500 毫秒（基于 100Hz 时钟，精度 10ms）
- `get_ticks()` 获取系统滴答数，可用于测量耗时

### crt0.asm（C 运行时启动代码）

```nasm
; CatOS User Program C Runtime
; 入口 _start: 设置栈, 调用 main, 调用 exit

INT_VECTOR_SYS_CALL equ 0x90
NR_exit equ 5

bits 32
section .text

global _start
extern _main

_start:
    mov     esp, 0x1000    ; 设置栈顶（段内偏移）
    call    _main          ; 调用 main()
    push    eax            ; main 返回值作为 exit code
    call    _exit          ; 调用 exit（不返回）
.hang:
    jmp     .hang          ; exit 不返回, 但以防万一
```

**解析**：
- `_start` 是程序入口（CE header.entry 指向它）
- 设置 `esp = 0x1000`（栈顶，段内偏移）
- 调用 C 语言的 `main` 函数
- main 返回后，将返回值压栈并调用 `exit`
- `exit` 通过 `int 0x90` 触发系统调用，内核终止进程并切换回 shell

### user.ld（链接脚本）

```
ENTRY(_start)

SECTIONS
{
    . = 0;                    /* 链接地址 = 0（段内偏移） */

    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.eh_frame_hdr)
    }
}
```

**解析**：
- `. = 0` 确保所有符号地址是段内偏移
- `.rodata` 合并到 `.text` 段
- 丢弃 `.comment`、`.note`、`.eh_frame` 等无用段

---

## 11. 常见问题

### Q: 程序没有输出？

**A**: 请检查以下几点：
1. 确保使用 `static char msg[] = "..."` 全局数组定义字符串
2. 确保调用了 `puts` 或 `putc`
3. 确认 CE 文件正确打包（使用 `mkcate.py`）
4. 确认内核已重新编译（`bash build.sh all`）

### Q: 程序崩溃显示 "Program crashed (exception #13)"？

**A**: 异常 #13 是 #GP（General Protection），常见原因：
- 访问了段外的内存地址（超过 128KB）
- 执行了特权指令（`cli`、`in`、`out` 等）
- 访问了空选择子（GS=0）指向的段
- 栈溢出（esp 超出段限）

### Q: 编译报错 "unknown type name 'size_t'"？

**A**: CatOS SDK 不提供标准头文件。自行定义所需类型：

```c
typedef unsigned int size_t;
```

### Q: 如何输出数字？

**A**: CatOS SDK 现在内置了整数输出函数，无需自行实现：

```c
extern void put_int(int n);             /* 有符号十进制: -42, 0, 2147483647 */
extern void put_uint(unsigned int n);   /* 无符号十进制: 4000000000 */
extern void put_hex(unsigned int n, int upper); /* 十六进制: put_hex(0xDEAD, 1) -> "DEAD" */
extern void put_bin(unsigned int n);    /* 二进制: put_bin(5) -> "101" */

/* 示例 */
put_int(-123);        /* 输出: -123 */
put_hex(0xFF, 0);     /* 输出: ff */
put_hex(0xFF, 1);     /* 输出: FF */
put_bin(10);          /* 输出: 1010 */
```

这些函数使用当前 `set_color` 设定的颜色输出。

### Q: 按方向键/Enter 后程序输出 `?` 或无反应？

**A**: `getch` 返回的是 **16 位键值**，扩展键（方向键、Enter、ESC、F1~F12 等）高位带 `0x100` 标志。如果直接 `putc(key)` 会把扩展键的低字节当作字符输出，图形模式下非可见字符（<32 或 >127）会显示 `?`。正确做法是先判断是否为扩展键：

```c
int key = getch();
if (key & 0x100) {
    /* 扩展键: 0x101=ESC, 0x103=Enter, 0x125=Up ... */
    switch (key) {
    case 0x103: /* Enter */ break;
    case 0x125: /* Up */    break;
    /* ... */
    }
} else {
    putc(key);  /* 普通可见字符才直接输出 */
}
```

参见前文「扩展键与 `getch` 返回值」获取完整键值表。

### Q: 如何读写文件？

**A**: 使用 `file_read`/`file_write` 等文件 IO 系统调用（NR 26~32）。文件名为 FAT 8.3 短文件名：

```c
extern int file_write(char* name, char* buf, int size);
extern int file_read(char* name, char* buf, int bufsize);

static char data[] = "hello";
file_write("data.txt", data, 5);           /* 写入 5 字节 */

char buf[256];
int n = file_read("data.txt", buf, 256);   /* 返回读取字节数 */
```

完整示例见第 6 章「文件 IO」小节。注意：仅支持根目录下的文件操作。

### Q: 程序大小超过 124KB 怎么办？

**A**: 当前 MVP 阶段不支持超过 124KB 的程序。优化建议：
- 使用 `-Os` 编译选项优化代码大小
- 避免大型查找表
- 将数据拆分到外部文件（未来支持文件系统调用后）

### Q: 可以用 C++ 吗？

**A**: 理论上可以（使用 `g++ -m32 -fno-exceptions -fno-rtti`），但需要额外提供 `__cxa_atexit` 等运行时函数。当前 SDK 仅支持 C。

### Q: make 找不到 gcc？

**A**: 确认 gcc 在 PATH 中，或修改 Makefile 手动指定路径：

```makefile
CC := /path/to/i686-w64-mingw32-gcc
```

---

## 12. 未来路线图

CatOS 可执行文件生态的后续发展规划：

| 阶段 | 功能 | 状态 |
|------|------|------|
| **Phase 1** | CE v1 格式 + 基础系统调用 + 单程序运行 | ✅ 已完成 |
| **Phase 2** | 键盘输入：`getc`/`getch`/`kbhit` + 扩展键 + 修饰键状态 | ✅ 已完成 |
| **Phase 4** | 文件系统系统调用：`file_read`/`file_write`/`file_delete`/`file_exists`/`file_size`/`file_list`/`file_append`/`file_stat` + `reboot` | ✅ 已完成 |
| **Phase 4b** | 类型转换：`int_to_str`（多进制整数转字符串）+ `atoi`（字符串转整数） | ✅ 已完成 |
| **Phase 3** | 多程序并发 + 多槽位（4 进程独立 128KB 内存区 + 非阻塞 `run` + `ps` 命令） | ✅ 已完成 |
| Phase 3b | 动态内存分配（进程内堆区 sys_alloc/sys_free） | 📋 计划中 |
| Phase 4+ | 流式读写（open/read/write/close/seek） | 📋 计划中 |
| Phase 5 | fork/exec 进程创建 | 📋 计划中 |
| Phase 6 | 分页隔离（虚拟内存，替换段隔离） | 📋 计划中 |

---

## 附录：SDK 文件清单

```
sdk/
├── DEVELOPER.md   ← 本文档
├── Makefile       ← 构建脚本
├── mkcate.py      ← CE v1 格式打包工具
├── crt0.asm       ← C 运行时启动代码
├── user.ld        ← 链接脚本
├── app.c          ← 用户程序模板（含全部 API 声明，包括窗口 API）
├── hello.c        ← 示例程序
└── hello_asm.asm  ← 纯汇编示例程序
```

如需帮助或报告问题，请查阅 CatOS 项目文档或联系开发者。
